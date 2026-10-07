// comm_module.c

#include "p30f6014a.h"
#include "stdio.h"
#include "string.h"

#include "motor_led/e_epuck_ports.h"
#include "motor_led/e_init_port.h"
#include "motor_led/e_led.h"
#include "uart/e_uart_char.h"
#include "contrib/radio_swis/v2/e_radio_swis.h"
#include "contrib/robot_id/e_robot_id.h"

// Replace 00 with your group ID (MICAz ID) here when using the MICAz.
#define GROUP_ID        0x00

#define SEND_INTERVAL_MS 1000
#define PACKET_SIZE      2
#define MAX_ROBOT_ID     255
#define MAX_RECEIVED_IDS 16
#define NUM_RING_LEDS    8
#define NUM_ATTENUATION_LEVELS 8

#ifndef ROBOT_ID_OVERRIDE
#define ROBOT_ID_OVERRIDE 0
#endif

static unsigned int robot_id;
static unsigned int radio_id;
static unsigned int eeprom_id;
static volatile int interval_due;
static int current_selector;
static int current_attenuation_db;
static sw_att_state current_tx_power;
static unsigned int received_ids[MAX_RECEIVED_IDS];
static int received_id_count;
static char serial_command[16];
static int serial_command_len;

static const sw_att_state selector_tx_powers[NUM_ATTENUATION_LEVELS] = {
    RADIO_SWIS_SW_ATTENUATOR_0DB,
    RADIO_SWIS_SW_ATTENUATOR_1DB,
    RADIO_SWIS_SW_ATTENUATOR_3DB,
    RADIO_SWIS_SW_ATTENUATOR_5DB,
    RADIO_SWIS_SW_ATTENUATOR_7DB,
    RADIO_SWIS_SW_ATTENUATOR_10DB,
    RADIO_SWIS_SW_ATTENUATOR_15DB,
    RADIO_SWIS_SW_ATTENUATOR_25DB
};

static const int selector_attenuation_db[NUM_ATTENUATION_LEVELS] = {
    0, 1, 3, 5, 7, 10, 15, 25
};

void wait(unsigned long num)
{
    while (num > 0) {
        num--;
    }
}

void uart_send_line(char *buffer)
{
    e_send_uart1_char(buffer, strlen(buffer));
    while (e_uart1_sending());
}

void start_timer1(int interval_ms)
{
    T1CON = 0;
    T1CONbits.TCKPS = 3;
    TMR1 = 0;
    PR1 = (interval_ms * MILLISEC) / 256.0;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

void __attribute__((interrupt, auto_psv, shadow)) _T1Interrupt(void)
{
    IFS0bits.T1IF = 0;
    interval_due = 1;
}

static void write_u16(unsigned char *packet, int offset, unsigned int value)
{
    packet[offset] = value & 0xff;
    packet[offset + 1] = (value >> 8) & 0xff;
}

static unsigned int read_u16(unsigned char *packet, int offset)
{
    return packet[offset] | ((unsigned int)packet[offset + 1] << 8);
}

static int selector_to_attenuation_index(int selector)
{
    int index;

    if (selector < 0) {
        return 0;
    }

    index = selector / 2;
    if (index >= NUM_ATTENUATION_LEVELS) {
        return NUM_ATTENUATION_LEVELS - 1;
    }

    return index;
}

static void read_selector_attenuation(void)
{
    int index;

    current_selector = e_get_selector();
    index = selector_to_attenuation_index(current_selector);
    current_tx_power = selector_tx_powers[index];
    current_attenuation_db = selector_attenuation_db[index];
}

void update_attenuation_from_selector(void)
{
    int selector;
    int index;
    int attenuation_db;
    sw_att_state tx_power;

    selector = e_get_selector();
    index = selector_to_attenuation_index(selector);
    tx_power = selector_tx_powers[index];
    attenuation_db = selector_attenuation_db[index];

    if ((selector == current_selector)
        && (tx_power == current_tx_power)
        && (attenuation_db == current_attenuation_db)) {
        return;
    }

    current_selector = selector;
    current_tx_power = tx_power;
    current_attenuation_db = attenuation_db;
    e_radio_swis_set_tx_pwr(current_tx_power);
}

static int robot_id_is_valid(unsigned int id)
{
    return (id > 0) && (id <= MAX_ROBOT_ID);
}

static unsigned int choose_robot_id(void)
{
    if (robot_id_is_valid(ROBOT_ID_OVERRIDE)) {
        return ROBOT_ID_OVERRIDE;
    }

    if (robot_id_is_valid(radio_id)) {
        return radio_id;
    }

    if (robot_id_is_valid(eeprom_id)) {
        return eeprom_id;
    }

    // Last resort: keep the raw radio address so the startup print exposes it.
    return radio_id;
}

static void set_robot_id(unsigned int id, char *source)
{
    char buffer[80];

    if (!robot_id_is_valid(id)) {
        sprintf(buffer, "ID rejected=%u source=%s\r\n", id, source);
        uart_send_line(buffer);
        return;
    }

    robot_id = id;
    e_radio_swis_set_address(robot_id);

    sprintf(buffer, "ID set=%u source=%s\r\n", robot_id, source);
    uart_send_line(buffer);
}

static int parse_decimal(char *text, unsigned int *value)
{
    unsigned int parsed = 0;
    int saw_digit = 0;

    while ((*text < '0') || (*text > '9')) {
        if (*text == '\0') {
            return 0;
        }
        text++;
    }

    while ((*text >= '0') && (*text <= '9')) {
        parsed = (parsed * 10) + (*text - '0');
        saw_digit = 1;
        text++;
    }

    *value = parsed;
    return saw_digit;
}

static void handle_serial_command(void)
{
    unsigned int requested_id;

    serial_command[serial_command_len] = '\0';
    if (parse_decimal(serial_command, &requested_id)) {
        set_robot_id(requested_id, "serial");
    }
    serial_command_len = 0;
}

void poll_serial_commands(void)
{
    char c;

    while (e_getchar_uart1(&c)) {
        if ((c == '\r') || (c == '\n')) {
            if (serial_command_len > 0) {
                handle_serial_command();
            }
        } else if (serial_command_len < (sizeof(serial_command) - 1)) {
            serial_command[serial_command_len++] = c;
        } else {
            serial_command_len = 0;
        }
    }
}

void broadcast_id(void)
{
    unsigned char packet[PACKET_SIZE];
    char buffer[80];

    write_u16(packet, 0, robot_id);

    e_radio_swis_send(GROUP_ID, RADIO_SWIS_BROADCAST, packet, PACKET_SIZE);

    sprintf(buffer, "TX id=%u\r\n", robot_id);
    uart_send_line(buffer);
}

static int received_id_seen(unsigned int id)
{
    int i;

    for (i = 0; i < received_id_count; i++) {
        if (received_ids[i] == id) {
            return 1;
        }
    }

    return 0;
}

static void remember_received_id(unsigned int id)
{
    if (received_id_seen(id)) {
        return;
    }

    if (received_id_count < MAX_RECEIVED_IDS) {
        received_ids[received_id_count++] = id;
    }
}

void collect_received_ids(void)
{
    int size;
    unsigned char packet[PACKET_SIZE];
    unsigned int sender_id;

    while (e_radio_swis_packet_ready(packet, &size)) {
        if (size != PACKET_SIZE) {
            continue;
        }

        sender_id = read_u16(packet, 0);

        if ((sender_id == robot_id) || !robot_id_is_valid(sender_id)) {
            continue;
        }

        remember_received_id(sender_id);
    }
}

void set_count_leds(int count)
{
    int i;
    int led_count;

    led_count = count;
    if (led_count > NUM_RING_LEDS) {
        led_count = NUM_RING_LEDS;
    }

    for (i = 0; i < NUM_RING_LEDS; i++) {
        e_set_led(i, i < led_count);
    }
}

void print_received_ids(void)
{
    int i;
    int offset;
    char buffer[128];

    offset = sprintf(buffer, "RX ids count=%d:", received_id_count);
    for (i = 0; i < received_id_count; i++) {
        offset += sprintf(buffer + offset, " %u", received_ids[i]);
    }
    sprintf(buffer + offset, "\r\n");
    uart_send_line(buffer);
}

void print_attenuation(void)
{
    char buffer[80];

    sprintf(buffer, "ATT selector=%d attenuation=%ddB tx_power=%u\r\n",
        current_selector,
        current_attenuation_db,
        (unsigned int)current_tx_power);
    uart_send_line(buffer);
}

void finish_receive_interval(void)
{
    print_received_ids();
    print_attenuation();
    set_count_leds(received_id_count);
    received_id_count = 0;
}

int main(void)
{
    char buffer[80];
    unsigned char radio_version;

    e_init_port();
    e_init_uart1();
    e_init_robot_id();
    read_selector_attenuation();

    radio_version = e_radio_swis_init_default(
        RADIO_SWIS_SLAVE_MASTER,
        NULL,
        RADIO_SWIS_HW_ATTENUATOR_25DB,
        current_tx_power);
    e_radio_swis_set_group(GROUP_ID);
    e_radio_swis_set_payload_length(PACKET_SIZE);
    radio_id = e_radio_swis_get_address();
    eeprom_id = e_get_robot_id();
    robot_id = choose_robot_id();
    if (robot_id_is_valid(robot_id)) {
        e_radio_swis_set_address(robot_id);
    }

    sprintf(buffer, "ID chosen=%u radio=%u eeprom=%u override=%u v=%u\r\n",
        robot_id,
        radio_id,
        eeprom_id,
        (unsigned int)ROBOT_ID_OVERRIDE,
        (unsigned int)radio_version);
    uart_send_line(buffer);

    interval_due = 1;
    received_id_count = 0;
    serial_command_len = 0;
    start_timer1(SEND_INTERVAL_MS);

    while (1) {
        poll_serial_commands();
        update_attenuation_from_selector();
        collect_received_ids();

        if (interval_due) {
            interval_due = 0;
            finish_receive_interval();
            broadcast_id();
        }

        wait(10000);
    }

    return 0;
}
