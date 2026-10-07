//******************************************************************************
//  Name:   main.c (Obstacle avoidance, linearized)
//  Author: 
//  Date:   
//  Rev:    October 5, 2015 by Florian Maushart
//  Descr:  Implementation of a simple, linearized Braitenberg controller
//	    for e-puck
//******************************************************************************

#include "p30f6014a.h"
#include "stdio.h"
#include "string.h"

#include "./codec/e_sound.h"
#include "./motor_led/e_init_port.h"
#include "./motor_led/e_led.h"
#include "./motor_led/e_motors.h"
#include "./uart/e_uart_char.h"
#include "./a_d/e_prox.h"
#include "math.h"
#include "sensorcalibrate.h"

// Weights for the Braitenberg obstacle avoidance algorithm
int weightleft[8] = {-5, -5, -5, 0, 0, 5, 5, 5};
int weightright[8] = {5, 5, 5, 0, 0, -5, -5, -5};


// Main program
int main() {
	char buffer[80];
	int leftwheel, rightwheel;
	int sensor[8];
	double sensorMean[8];
	int numberOfSamples;
	int i,n;

	// Initialize system and sensors
	e_init_port();
	e_init_uart1();
	e_init_motors();
	e_init_prox();

	//Calibrate sensors
	sensor_calibrate();


	// Reset if Power on (some problem for few robots)
	if (RCONbits.POR) {
		RCONbits.POR = 0;
		__asm__ volatile ("reset");
	}

	// Say hello
	// You'll receive these messages in Minicom. They are therefore well suited for debugging.
	// Note, however, that sending such messages is time-consuming and can slow down your program significantly.
	sprintf(buffer, "Braitenberg starting\r\n");
	e_send_uart1_char(buffer, strlen(buffer));

	//Set the number of samples used to compute the average of the sensor values
	numberOfSamples=1000;
	
	//Set the original speed of the robot
	e_set_speed_left(400);
	e_set_speed_right(400);

	// Run the braitenberg algorithm
	while (1) {
		// Forward speed
		leftwheel = 400;
		rightwheel = 400;
		for (i=0;i<8;i++)
			sensorMean[i]=0;	
		//Compute an average value of each sensor on multiple samples to reduce noise	
		for (n=0;n<numberOfSamples;n++)
		{	
			// Get sensor values
			for (i = 0; i < 8; i++) {
				// Use the sensorzero[i] value generated in sensor_calibrate() to zero sensorvalues
				sensor[i] = e_get_prox(i)-sensorzero[i];
				//linearize the sensor output and compute the average
				sensorMean[i]+=12.1514*log((double)sensor[i])/(double)numberOfSamples;
			}
		}

		// Add the weighted sensors values
		for (i = 0; i < 8; i++) {
			leftwheel += weightleft[i] * (int)sensorMean[i];
			rightwheel += weightright[i] * (int)sensorMean[i];
		}


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
		} else {
			e_set_led(1, 0);
			e_set_led(7, 1);
		}

	}

	return 0;
}
