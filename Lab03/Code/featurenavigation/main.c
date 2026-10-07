/********************************************************************************

			Program to demonstrate the e-puck's open loop motor control capabilities
			2 February 2010
			Alexander Bahr

(c) 2010 Alexander Bahr

Distributed intelligent systems and algorithms laboratory http://disal.epfl.ch
EPFL Ecole polytechnique federale de Lausanne http://www.epfl.ch

Last revision: October 5, 2015 by Florian Maushart

**********************************************************************************/


/*! \file
 * \brief The main file of the open loop motor control demo.
 *
 * This file demonstrates the functionality of the two open loop motor control 
 * functions e_set_distance(distance,motor_speed) and e_set_turn(angle,motor_speed).
 * \n \n The program goes through the following sequence of open loop controlled motions:
 * - Go forward for 200 mm
 * - Turn CCW for 180 degrees
 * - Go forward for 200 mm
 * - Turn CCW for 180 degrees
 * After this sequence the robot should be in the same position and orientation from which it started
 * \warning This demo requires a new version of e_motors.c and e_motors.h
 * \author Code: AlexanderBahr \n Doc: Alexander Bahr
 */


#include "p30f6014a.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"
#include "main.h"
#include "./uart/e_uart_char.h"
#include "./codec/e_sound.h"
#include "./motor_led/e_init_port.h"
#include "./motor_led/e_led.h"
#include "./motor_led/e_motors.h"
#include "./uart/e_uart_char.h"
#include "./a_d/advance_ad_scan/e_prox.h"
#include "./a_d/advance_ad_scan/e_ad_conv.h"
#include "./a_d/advance_ad_scan/e_prox.h"
#include "./a_d/advance_ad_scan/e_ad_conv.h"
#define PI 3.14159265358979


char buffer[80];

int main()
{
	long ref_point = 0;
	int i;
 
	////////////////////////////////////////////////////
	// System Initialization ///////////////////////////
	////////////////////////////////////////////////////
	e_init_port();
	e_init_uart1();
	e_init_motors();
	e_init_ad_scan();

	//Reset if Power on (some problem for few robots)
	if (RCONbits.POR) {
		RCONbits.POR=0;
		__asm__ volatile ("reset");
	}
	e_set_dr_position(0.0, 0.0, 0.0); 

	////////////////////////////////////////////////////
	// Start-up delay //////////////////////////////////
	////////////////////////////////////////////////////

	for (i=0; i<255; i++) {
		wait_10ms();
		wait_10ms();
	}
	

	////////////////////////////////////////////////////
	// Sample for reference point (back of epuck) //////
	////////////////////////////////////////////////////

	for (i=0; i<32; i++) {
		ref_point += (e_get_prox(3)+e_get_prox(4))/2;
		wait_10ms();
	}

	ref_point =(ref_point>>5);


	sprintf(buffer, "Reference sensor value: %d, \n", (int)ref_point);
	e_send_uart1_char(buffer, strlen(buffer));
	wait_10ms();

	////////////////////////////////////////////////////
	// Main while-loop /////////////////////////////////
	////////////////////////////////////////////////////
	
	while (1) {
		///////////////////////////////////////
		// Dead reckoning 
		
		go(200);			// Go forward for 200 mm
		rotate(180);				// Turn 180 degrees CCW
		go(0);			// Go forward for 200 mm	
		rotate(0);				// Turn 180 degrees CCW. One complete lap!

		///////////////////////////////////////////
		// Uncomment the line below to perform ////
		// Dead reckoning with alignment Feature //
		///////////////////////////////////////////
		
		//align(ref_point);		// Do fine adjustments to align with the reference point (sampled from the epucks back sensors before it started to move)

	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
//// SUB FUNCTIONS ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void go(double y_coord){
	int i, leftwheel, rightwheel;
	int sensor[8], value;
	double dr_x =0;			// Dead Reckoning of X coordinate
	double dr_y = 0;		// Dead Reckoning of y coordinate
	double dr_theta = 0;	// Dead Reckoning of theta coordinate

	// Weights for the Braitenberg obstacle avoidance algorithm
	int weightleft[8] = {0, -11, 3, 0, 0, -3, 11, 0};
	int weightright[8] = {0, 11, -3, 0, 0, 3, -11, 0};
	
	e_get_dr_position(&dr_x,&dr_y,&dr_theta);	// Get estimated Dead Reckoning

	// Repeat until 200 mm have been travelled
	while(fabs(dr_y-y_coord)>=(double)5.0){
		// Get sensor values
		for (i = 0; i < 8; i++) 
			sensor[i] = e_get_prox(i);
		
		// Initial forward speed
		leftwheel = 400;
		rightwheel = 400;		

		// Add the braitenberg weighted sensors values
		for (i = 0; i < 8; i++) {
			value = (sensor[i] >> 4);
			leftwheel += weightleft[i] * value;
			rightwheel += weightright[i] * value;
		}

		set_speed(leftwheel, rightwheel);
			
		e_do_dr();					// Perform/update Dead Reckoning
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);	// Get estimated Dead Reckoning
		
		sprintf(buffer, "x = %f mm; y = %f mm; theta = %f mm\n\r",dr_x, dr_y, dr_theta);
		e_send_uart1_char(buffer, strlen(buffer));
		wait_10ms();
	}
	if(dr_theta>90 && dr_theta<270) e_set_dr_position(dr_x, dr_y, 180);
	else e_set_dr_position(dr_x, dr_y, 0);
}

void rotate(double angle){
	int leftwheel = -400; 
	int rightwheel = 400;
	double dr_x =0;					// Dead Reckoning of X coordinate
	double dr_y = 0;				// Dead Reckoning of y coordinate
	double dr_theta = 0;				// Dead Reckoning of theta coordinate

	e_get_dr_position(&dr_x,&dr_y,&dr_theta);	// Get estimated Dead Reckoning

	while(fabs(dr_theta-angle) >= (double)3.0){
		set_speed(leftwheel, rightwheel);

		e_do_dr();					// Perform/update Dead Reckoning
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);	// Get estimated Dead Reckoning
	
		sprintf(buffer, "x = %f mm; y = %f mm; theta = %f mm\n\r",dr_x, dr_y, dr_theta);
		e_send_uart1_char(buffer, strlen(buffer));
		wait_10ms();
	}

//	dr_x = 0;
//	dr_y = 0;
//	dr_theta = 0;
//	e_set_dr_position(dr_x, dr_y, dr_theta); 
}

// align(ref_point) checks the distance that the e-puck currently has from its reference point and sets the wheel speed accordingly
// until the e-puck is at its reference point again
// Note: The e-puck has to be put close to a wall in the beginning (distance sensor value >= 300) in order for it to be
// able to perform proper feature recognition
void align(int ref_point){
	int current_sensor_value = (e_get_prox(3)+e_get_prox(4))/2;
	double dr_x =0;
	double dr_y = 0;
	double dr_theta = 0;

	// align while too far away from reference
	while((current_sensor_value < 300) || (current_sensor_value > ref_point+50) ||(current_sensor_value < ref_point-50)){
		if(current_sensor_value > ref_point)
			set_speed(100, 100);
		else
			set_speed(-100, -100);
		
		current_sensor_value = (e_get_prox(3)+e_get_prox(4))/2;	// Current
		e_do_dr();						// Perform/update Dead Reckoning
		e_get_dr_position(&dr_x,&dr_y,&dr_theta);	// Get estimated Dead Reckoning
	
		sprintf(buffer, "x = %f mm; y = %f mm; theta = %f mm current %d\n\r",dr_x, dr_y, dr_theta, current_sensor_value);
		e_send_uart1_char(buffer, strlen(buffer));
		wait_10ms();
	}
	
	dr_x = 0;
	dr_y = 0;
	dr_theta = 0;
	e_set_dr_position(dr_x, dr_y, dr_theta); 		
}

void set_speed(int leftwheel, int rightwheel){
	// Speed bounds, to avoid setting to high speeds to the motor
	if (leftwheel > 1000) {leftwheel = 1000;}
	if (rightwheel > 1000) {rightwheel = 1000;}
	if (leftwheel < -1000) {leftwheel = -1000;}
	if (rightwheel < -1000) {rightwheel = -1000;}
	e_set_speed_left(leftwheel);
	e_set_speed_right(rightwheel);

	// Indicate with leds on which side we are turning (leds are great for debugging) 
	if (leftwheel>rightwheel) {
		e_set_led(1, 1);
		e_set_led(7, 0);
	} 
	else {
		e_set_led(1, 0);
		e_set_led(7, 1);
	}
}

void wait_10ms(void){
	long int i;
	for(i=0;i<20000;i++);//wait 10ms;
	return;
}
