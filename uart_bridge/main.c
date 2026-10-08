/* Hardware UART2 command slave for the original dsPIC30F6014A e-puck.
 * UART1 supplies independent Bluetooth diagnostic status at 115200 baud.
 * No software UART or pin reversal is implemented.
 */
#include <p30f6014a.h>
#include <stdio.h>
#include <string.h>
#include "motor_led/e_epuck_ports.h"
#include "motor_led/e_init_port.h"
#include "motor_led/e_led.h"
#include "motor_led/e_motors.h"
#include "a_d/e_ad_conv.h"
#include "a_d/e_prox.h"
#include "protocol.h"

#define RX_SIZE 256u
#define RX_MASK (RX_SIZE - 1u)
#define TX_SIZE 512u
#define TX_MASK (TX_SIZE - 1u)

static volatile unsigned int tick_ms;
static volatile unsigned int rx_head, rx_tail, rx_fault;
static volatile unsigned int rx_bytes, rx_errors, rx_drops;
static volatile unsigned char rx_buffer[RX_SIZE];
static char tx_buffer[2][TX_SIZE];
static unsigned int tx_head[2], tx_tail[2], tx_drops[2], tx_bytes[2];
static unsigned int commands, armed, motor_deadline_start;
static unsigned int stream_ms, last_sample;

void __attribute__((interrupt, auto_psv)) _T1Interrupt(void)
{
    IFS0bits.T1IF = 0;
    ++tick_ms;
}

void __attribute__((interrupt, auto_psv)) _U2RXInterrupt(void)
{
    unsigned int next, error;
    unsigned char byte;
    IFS1bits.U2RXIF = 0;
    if (U2STAbits.OERR) {
        U2STAbits.OERR = 0;
        ++rx_errors;
        rx_fault = 1;
    }
    while (U2STAbits.URXDA) {
        error = U2STAbits.FERR || U2STAbits.PERR;
        byte = (unsigned char)U2RXREG;
        ++rx_bytes;
        if (error) {
            ++rx_errors;
            rx_fault = 1;
            continue;
        }
        next = (rx_head + 1u) & RX_MASK;
        if (next == rx_tail) {
            ++rx_drops;
            rx_fault = 1;
        } else {
            rx_buffer[rx_head] = byte;
            rx_head = next;
        }
    }
}

static void init_link(void)
{
    /* 7.3728 MHz crystal, PLL8 => FCY=14.7456 MHz; divisor 7 => 115200. */
    TRISFbits.TRISF2 = 1;
    TRISFbits.TRISF3 = 0;
    TRISFbits.TRISF4 = 1;
    TRISFbits.TRISF5 = 0;
    U1MODE = U2MODE = 0;            /* 8N1, normal pins */
    U1STA = U2STA = 0;
    U1BRG = U2BRG = 7;
    IEC0bits.U1RXIE = IEC0bits.U1TXIE = 0;
    IEC1bits.U2TXIE = 0;
    IPC6bits.U2RXIP = 5;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;
    U1MODEbits.UARTEN = U2MODEbits.UARTEN = 1;
    U1STAbits.UTXEN = U2STAbits.UTXEN = 1;

    T1CON = 0;
    T1CONbits.TCKPS = 1;            /* divide by 8 */
    TMR1 = 0;
    PR1 = 1842;                    /* approximately 1 ms */
    IPC0bits.T1IP = 6;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

/* Queue complete lines only. Do not block motor control on serial output. */
static int send_line(unsigned int port, const char *text)
{
    unsigned int n = (unsigned int)strlen(text);
    unsigned int free_bytes = (tx_tail[port] - tx_head[port] - 1u) & TX_MASK;
    unsigned int i;
    if (n > free_bytes) {
        ++tx_drops[port];
        return 0;
    }
    for (i = 0; i < n; ++i) {
        tx_buffer[port][tx_head[port]] = text[i];
        tx_head[port] = (tx_head[port] + 1u) & TX_MASK;
    }
    return 1;
}

static void pump_tx(void)
{
    while (tx_tail[0] != tx_head[0] && !U1STAbits.UTXBF) {
        U1TXREG = tx_buffer[0][tx_tail[0]];
        tx_tail[0] = (tx_tail[0] + 1u) & TX_MASK;
        ++tx_bytes[0];
    }
    while (tx_tail[1] != tx_head[1] && !U2STAbits.UTXBF) {
        U2TXREG = tx_buffer[1][tx_tail[1]];
        tx_tail[1] = (tx_tail[1] + 1u) & TX_MASK;
        ++tx_bytes[1];
    }
}

static void stop_motors(void)
{
    e_set_speed_left(0);
    e_set_speed_right(0);
    armed = 0;
}

static void status(unsigned int port, unsigned int now)
{
    char text[160];
    sprintf(text, "STATUS t=%u rx=%u err=%u drop=%u cmd=%u tx=%u txdrop=%u arm=%u stream=%u\r\n",
            now, rx_bytes, rx_errors, rx_drops, commands, tx_bytes[1],
            tx_drops[1], armed, stream_ms);
    send_line(port, text);
}

static void sample(unsigned int now)
{
    char text[256];
    unsigned int i, n, saved_t2, saved_t4, saved_t5;
    int ax, ay, az;
    long left, right;
    /* This basic proximity driver uses Timer2 + the shared ADC. */
    saved_t2 = IEC0bits.T2IE;
    IEC0bits.T2IE = 0;
    ax = e_read_ad(ACCX);
    ay = e_read_ad(ACCY);
    az = e_read_ad(ACCZ);
    IEC0bits.T2IE = saved_t2;

    /* The library's 32-bit step counters need an atomic snapshot on a 16-bit MCU. */
    saved_t4 = IEC1bits.T4IE;
    saved_t5 = IEC1bits.T5IE;
    IEC1bits.T4IE = IEC1bits.T5IE = 0;
    left = e_get_steps_left();
    right = e_get_steps_right();
    IEC1bits.T4IE = saved_t4;
    IEC1bits.T5IE = saved_t5;
    n = sprintf(text, "DATA %u", now);
    for (i = 0; i < 8; ++i) n += sprintf(text + n, " %d", e_get_prox(i));
    for (i = 0; i < 8; ++i) n += sprintf(text + n, " %d", e_get_ambient_light(i));
    sprintf(text + n, " %d %d %d %ld %ld\r\n", ax, ay, az, left, right);
    send_line(1, text);
}

static void execute(const char *line, unsigned int now)
{
    Command cmd = parse_command(line);
    if (cmd.type == CMD_ERROR) {
        send_line(1, "ERR COMMAND\r\n");
        return;
    }
    ++commands;
    switch (cmd.type) {
    case CMD_PING: send_line(1, "PONG\r\n"); break;
    case CMD_ARM:
        stop_motors();
        armed = 1;
        motor_deadline_start = now;
        send_line(1, "OK ARM\r\n");
        break;
    case CMD_MOTORS:
        if (!armed) { send_line(1, "ERR NOT_ARMED\r\n"); break; }
        e_set_speed_left(cmd.left);
        e_set_speed_right(cmd.right);
        motor_deadline_start = now;
        send_line(1, "OK M\r\n");
        break;
    case CMD_STOP: stop_motors(); send_line(1, "OK STOP\r\n"); break;
    case CMD_READ: sample(now); break;
    case CMD_STREAM:
        stream_ms = cmd.period_ms;
        last_sample = now;
        send_line(1, "OK STREAM\r\n");
        break;
    case CMD_HEARTBEAT:
        if (armed) motor_deadline_start = now;
        send_line(1, "OK HB\r\n");
        break;
    case CMD_STATUS: status(1, now); break;
    default: break;
    }
}

int main(void)
{
    char line[MAX_LINE];
    unsigned int length = 0, discard = 0;
    unsigned int last_beacon = 0, last_led = 0, now;
    unsigned int budget;
    unsigned char byte;
    e_init_motors();                /* also initializes ports/configuration */
    stop_motors();
    e_init_ad();
    e_init_prox();
    init_link();
    send_line(0, "BOOT EPUCK_UART_BRIDGE 1 UART2=115200,8N1\r\n");
    send_line(1, "BOOT EPUCK_UART_BRIDGE 1\r\n");
    for (;;) {
        now = tick_ms;
        if (armed && (unsigned int)(now - motor_deadline_start) >= MOTOR_TIMEOUT_MS) {
            stop_motors();
            send_line(1, "EVENT MOTOR_TIMEOUT\r\n");
        }
        if (rx_fault) {
            IEC1bits.U2RXIE = 0;
            rx_tail = rx_head;
            rx_fault = 0;
            IEC1bits.U2RXIE = 1;
            discard = 1;
            length = 0;
            stop_motors();
            send_line(1, "ERR RX_LOSS\r\n");
        }
        /* Bounded command work so continuous input cannot starve the timeout. */
        budget = 64;
        while (budget-- && rx_tail != rx_head && !rx_fault) {
            byte = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1u) & RX_MASK;
            if (byte == '\r') continue;
            if (byte == '\n') {
                if (discard) send_line(1, "ERR LINE\r\n");
                else if (length) {
                    line[length] = '\0';
                    execute(line, tick_ms);
                }
                length = discard = 0;
            } else if (!discard) {
                if (byte < 32 || byte > 126 || length >= MAX_LINE - 1) {
                    discard = 1;
                    length = 0;
                } else line[length++] = (char)byte;
            }
        }
        now = tick_ms;
        if (stream_ms && (unsigned int)(now - last_sample) >= stream_ms) {
            last_sample = now;
            sample(now);
        }
        if ((unsigned int)(now - last_beacon) >= 1000) {
            last_beacon = now;
            status(0, now);         /* independent Bluetooth diagnostic channel */
            send_line(1, "BEACON EPUCK_UART_BRIDGE 1\r\n");
        }
        if ((unsigned int)(now - last_led) >= 500) {
            last_led = now;
            e_set_led(0, 2);        /* LED0 is directly wired, no turret bridge */
        }
        pump_tx();
    }
}
