// collectivedecision.c
//
/* 

	Version 1.1, November 2009
		Florian Vaussard / DISAL

	Version 1.0, ???
		Chirstopher Cianci / DISAL

*/ 	

#include "p30f6014a.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <libpic30.h>

#include "motor_led/e_epuck_ports.h"
#include "motor_led/e_init_port.h"
#include "motor_led/e_led.h"
#include "motor_led/e_motors.h"
#include "uart/e_uart_char.h"
#include "a_d/advance_ad_scan/e_prox.h"
#include "a_d/advance_ad_scan/e_ad_conv.h"
#include "contrib/radio_swis/v2/e_radio_swis.h"
#include "contrib/robot_id/e_robot_id.h"

// **********************************************************************
// Replace 00 with your group ID (MICAz ID) here when using the MICAz
#define GROUP_ID        0x00
// **********************************************************************

// General configuration
#define DECISION_INTERVAL   1000    // the decision interval in milliseconds
#define OPCHANGE_PROB       512     // the probability as a fraction of 1024 (512=0.5)
#define COM_POWER           RADIO_SWIS_SW_ATTENUATOR_15DB       // this is a value from 3 (lowest power) to 31 (full power)

// Constants for the position of the selector
enum {
	SELECTOR_RIGHT = 0,
	SELECTOR_REAR = 4,
	SELECTOR_LEFT = 8,
	SELECTOR_FRONT = 12,
};

// Weights for the Braitenberg algorithm
int weight_leftwallfollow[8] = {10, -10, -5, 0, 0, 5, 10, 10};
int weight_rightwallfollow[8] = {-10, -10, -5, 0, 0, 5, 10, -10};

// The state variables
int opinion;
#define OPINION_LEFT    0
#define OPINION_RIGHT   1
int message_counter_left;
int message_counter_right;
int have_to_broadcast_opinion;

int robot_id;

// Calibration function and data for the proximity sensors
void calibrate_sensors(void);
int _EEDATA(32) EEcalib[16];		// 16 bytes (EEPROM) in order to perform access to a row -> last bytes are null
int calib_sensor[16];				// Same, but stored in RAM

// Waits for a certain amount of time
// Note that the effective waiting time is not always the same (because of possible interrupts).
void wait(unsigned long num) {
    while (num > 0) {num--;}
}

// Initializes timer1
// This gives us an interrupt every DECISION_INTERVAL ms.
void start_timer1(int interval) {
    T1CON = 0;
    T1CONbits.TCKPS = 3;                // prescsaler = 256
    TMR1 = 0;                           // clear timer 1
    PR1 = (interval*MILLISEC)/256.0;    // interrupt after requested # of ms
    IFS0bits.T1IF = 0;                  // clear interrupt flag
    IEC0bits.T1IE = 1;                  // set interrupt enable bit
    T1CONbits.TON = 1;                  // start Timer1
}

void show_opinion(void)
{
    // Visualize the opinion
    e_set_led(8, 0);
    if (opinion==OPINION_RIGHT) {
        e_set_led(2, 1);
    } else if (opinion==OPINION_LEFT) {
        e_set_led(6, 1);
    }
}

// Timer 1 interrupt
// This code is executed every DECISION_INTERVAL ms.
void _ISRFAST _T1Interrupt(void) {
    int randomnumber;
    int majorityopinion;

    // Clear interrupt flag
    IFS0bits.T1IF = 0;

    // Take a decision
    switch (e_get_selector()) {
    case SELECTOR_RIGHT: 	// the robot is forced to go right
        opinion=OPINION_RIGHT;
        break;
    case SELECTOR_LEFT: 	// the robot is forced to go left
        opinion=OPINION_LEFT;
        break;
    case SELECTOR_FRONT: 	// normal operation
        if (message_counter_left > message_counter_right) {
            majorityopinion=OPINION_LEFT;
        } else if (message_counter_left < message_counter_right) {
            majorityopinion=OPINION_RIGHT;
        } else if (message_counter_left>0) {
            majorityopinion=((opinion==OPINION_LEFT) ? OPINION_RIGHT : OPINION_LEFT);
        } else {
            majorityopinion=opinion;
        }

        // With probability OPCHANGE_PROB, take the opinion of the majority
        randomnumber=rand() & 0x3ff;
        if (randomnumber < OPCHANGE_PROB) {
            opinion=majorityopinion;
        }

        break;
    default: // wrong position
        e_set_led(8, 1);
        return;
    }

	show_opinion();
	
    // Update the state variables
    message_counter_left=0;
    message_counter_right=0;
    have_to_broadcast_opinion=1;
}

// Broadcasts a packet with the current opinion
void broadcast_opinion_packet() {
    unsigned char packet[6];

    packet[0] = 0;                      // sourceMoteID
    packet[1] = 0;                      //  [high byte]

    packet[2] = opinion;                // lastSampleNumber
    packet[3] = 0;                      //  [high byte]

    packet[4] = (robot_id & 0xff);  // channel
    packet[5] = 0;                      //  [high byte]

    e_radio_swis_send(GROUP_ID, RADIO_SWIS_BROADCAST, packet, 6);
}

// We received a packet
void packet_received() {
    int size;
    unsigned char packet[6];
    int rid;
    int sourceflag;
    
    if (e_radio_swis_packet_ready(packet, &size)) {
        rid=packet[4];
        sourceflag=packet[0];
        if (rid != (robot_id & 0xff)) {
            if (packet[2]==OPINION_LEFT) {
                message_counter_left++;
            } else if (packet[2]==OPINION_RIGHT) {
                message_counter_right++;
            }
            e_set_body_led(1);
        }
    }
}

static inline void follow_and_avoid_wall(void)
{
    int leftwheel, rightwheel;
    int sensor[8], value, maxvalue;
    int i;
    
    // Forward speed
    leftwheel = 200;
    rightwheel = 200;

    // Get sensor values
    for (i=0; i<8; i++) {
         sensor[i] = e_get_prox(i) - calib_sensor[i];
    }

    // Add the weighted sensors values -> obstacle avoidance
    
    maxvalue=0;
    for (i=0; i<8; i++) {
        value = (sensor[i] >> 4);
        if (opinion==OPINION_RIGHT) {
            leftwheel += weight_rightwallfollow[i] * value;
            rightwheel += -weight_rightwallfollow[i] * value;
        } else {
            leftwheel += weight_leftwallfollow[i] * value;
            rightwheel += -weight_leftwallfollow[i] * value;
        }
        if (maxvalue<sensor[i]) {maxvalue=sensor[i];}
    }

    // Right or left wall follow
    if ((opinion==OPINION_RIGHT) && (sensor[2]>150)) {
    	// Wall on the right -> follow it
        leftwheel+=350;
        rightwheel-=350;
    } else if ((opinion==OPINION_RIGHT) && (maxvalue>300)) {
    	// Wall on the other side -> turn
        leftwheel = -600;
        rightwheel = +600;
    } else if ((opinion==OPINION_LEFT) && (sensor[5]>150)) {
    	// Wall on the left -> follow it
        leftwheel-=350;
        rightwheel+=350;
    } else if ((opinion==OPINION_LEFT) && (maxvalue>300)) {
    	// Wall on the other side -> turn
        leftwheel = +600;
        rightwheel = -600;
    } else if (maxvalue <= 300) {
    	// No wall 
        leftwheel=200;
        rightwheel=200;
    }

    // Speed bounds, to avoid setting to high speeds to the motor
    if (leftwheel > 1000) {leftwheel = 1000;}
    if (rightwheel > 1000) {rightwheel = 1000;}
    if (leftwheel < -1000) {leftwheel = -1000;}
    if (rightwheel < -1000) {rightwheel = -1000;}
    e_set_speed_left(leftwheel);
    e_set_speed_right(rightwheel);
}

// Main program
int main() {
    char buffer[80];

    // Initialize system and sensors
    e_init_port();
    e_init_uart1();
    e_init_motors();
    e_init_ad_scan();
    e_init_robot_id();
//    e_radio_swis_init_default(RADIO_SWIS_MULTIMASTER, packet_received, RADIO_SWIS_HW_ATTENUATOR_25DB, COM_POWER);
    e_radio_swis_init_default(RADIO_SWIS_SLAVE_MASTER, NULL, RADIO_SWIS_HW_ATTENUATOR_25DB, COM_POWER);
    e_radio_swis_set_group(GROUP_ID);
    e_radio_swis_set_payload_length(6);
    robot_id = e_radio_swis_get_address();
	
    // Calibrate the proximity sensors or retrive the calibration from EEPROM
    calibrate_sensors();

    // Reset if power on (this is necessary for some e-pucks)
    if (RCONbits.POR) {
        RCONbits.POR = 0;
        __asm__ volatile ("reset");
    }

    // Say hello
    // You'll receive these messages in Minicom. They are therefore well suited for debugging.
    // Note, however, that sending such messages is time-consuming and can slow down your program significantly.
    sprintf(buffer, "Collective decision starting\r\n");
    e_send_uart1_char(buffer, strlen(buffer));

    // Initialize the random number generator and choose an opinion at random
    srand(e_get_prox(0) + e_get_prox(2) + e_get_prox(4) + e_get_prox(6) );
    opinion=((rand() & 0x1) ? OPINION_LEFT : OPINION_RIGHT);
    show_opinion();
	
    // Start timer 1 (this takes a decision)
    start_timer1(DECISION_INTERVAL);

    // Run the braitenberg algorithm
    have_to_broadcast_opinion=0;
    message_counter_left=0;
    message_counter_right=0;
    while (1) {
        // Read the sensors and follow a wall, depending on our opinion
        follow_and_avoid_wall();
		
        // If we need to broadcast our opinion, do this
        if (have_to_broadcast_opinion) {
            have_to_broadcast_opinion=0;
            broadcast_opinion_packet();
        }

        // Check for new packets
        e_set_body_led(0);
//        e_radio_swis_refresh();
		packet_received();

        // Wait for some time
        wait(10000);
    }

    return 0;
}


void calibrate_sensors(void)
{
	int i;
	_prog_addressT EE_addr;
	_init_prog_address(EE_addr, EEcalib);
	
	// Read the calibration from EEPROM
	_memcpy_p2d16(calib_sensor, EE_addr, _EE_ROW);
	
	// Calibrate if there is no configuration in EEPROM or if the user wants to do so
	if ( (calib_sensor[8] != 0x5555) || (e_get_selector() == SELECTOR_REAR) ){
		// Calibrate the proximity sensors
		for (i=0; i<8; i++){
			e_set_led(i, 1);
		}
		wait(1000000l);
		for (i=0; i<8; i++) {
			calib_sensor[i] = e_get_prox(i);
		}
		
		calib_sensor[8] = 0x5555;	// Set a control value
		// Write to EEPROM		
		_erase_eedata(EE_addr, _EE_ROW);
		_wait_eedata();
		_write_eedata_row(EE_addr, calib_sensor);
		_wait_eedata();
		
		e_led_clear();
	}	
}
