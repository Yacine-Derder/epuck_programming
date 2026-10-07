//********************************************************************************
//  Name:   main.c (Dead reckoning navigation)
//  Author: 
//  Date:   
//  Rev:    October 5, 2015 by Florian Maushart
//  Descr:  Demo of navigation using the e-puck motor steps for dead reckoning
//	    The current state of the e-puck can be read via the minicom terminal
//********************************************************************************

#include "p30f6014a.h"
#include "stdio.h"
#include "string.h"

#include "./codec/e_sound.h"
#include "./motor_led/e_init_port.h"
#include "./motor_led/e_led.h"
#include "./motor_led/e_motors.h"
#include "./uart/e_uart_char.h"
#include "./a_d/advance_ad_scan/e_prox.h"
#include "./a_d/advance_ad_scan/e_ad_conv.h"


// Waits for a certain amount of time
// Note that the effective waiting time is not always the same (because of possible interrupts).
void wait(unsigned long num) {
	while (num > 0) {num--;}
}

// Main program
int main() {
	char buffer[80];
		
	long int i;
	
	// Initialize dead reckoning variables at (0,0,0)
	double dr_x = 0;
	double dr_y = 0;
	double dr_theta = 0;

	// Initialize system and sensors
	e_init_port();
	e_init_uart1();
	e_init_motors();
	e_init_ad_scan();

	// Reset if Power on (some problem for few robots)
	if (RCONbits.POR) {
		RCONbits.POR = 0;
		__asm__ volatile ("reset");
	}

	

	// init dead reckoning
	e_set_dr_position(dr_x,dr_y,dr_theta);
	
	// The following messages are displayed in the minicom terminal after a bluetooth	//
	// connection to the e-pcuk has been established					//
	// To launch minicom enter "minicom" in your Linux terminal				//
	
	// clear screen
	sprintf(buffer, "\x1B[2J");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// set cursor to upper left corner	
	sprintf(buffer, "\x1B[1;1H");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// print a welcome message
	sprintf(buffer, "Demonstrating dead-reckoning (going in 200mm forward, ");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;
	sprintf(buffer, "turning 180 degrees, returning and turning 180 degrees):\n\r\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// inform user
	sprintf(buffer, "Going 200mm forward\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// FIRST leg - go forward
	e_set_speed_left(300);
	e_set_speed_right(300);

	// go forward until dr_y is 200mm
	while(dr_y<=200)
	{
		// update dead reckoning via motor steps since last call
		e_do_dr();
		// retrieve new position
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);

		for(i=0;i<20000;i++);//wait 10ms;
	}

	// STOP
	e_set_speed_left(0);
	e_set_speed_right(0);

	// inform user
	sprintf(buffer, "Turning 180 degrees clock-wise\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// FIRST turn
	e_set_speed_left(100);
	e_set_speed_right(-100);

	// Turn until at approximately 180-190 degrees
	while((dr_theta<180) || (dr_theta>190))
	{
		e_do_dr();
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);

		for(i=0;i<20000;i++);//wait 10ms;
	}

	// inform user	
	sprintf(buffer, "Going 200mm forward\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// SECOND leg (go back)
	e_set_speed_left(300);
	e_set_speed_right(300);

	while(dr_y>0)
	{
		// update dead reckoning via motor steps since last call
		e_do_dr();
		// retrieve new position
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);

		for(i=0;i<20000;i++);//wait 10ms;
	}

	// STOP
	e_set_speed_left(0);
	e_set_speed_right(0);

	// inform user
	sprintf(buffer, "Turning 180 degrees clock-wise\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// SECOND turn
	e_set_speed_left(100);
	e_set_speed_right(-100);
	
	// Turn until at approximately 1-5 degrees
	while((dr_theta<1) || (dr_theta>5))
	{
		e_do_dr();
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);

		for(i=0;i<20000;i++);//wait 10ms;
	}

	// inform user that program has finished
	sprintf(buffer, "Done!\n\r");
	e_send_uart1_char(buffer, strlen(buffer));
	for(i=0;i<20000;i++);//wait 10ms;

	// STOP
	e_set_speed_left(0);
	e_set_speed_right(0);

	// Do nothing until reset manually
	while(1)
	{
	}	

	return 0;
}
