//******************************************************************************
//  Name:   sensorcalibrate.c 
//  Author: 
//  Date:   
//  Rev:    October 5, 2015 by Florian Maushart
//  Descr:  Take the average of 32 samples of the e-puck distance sensors for 
//	    calibration and output them to sensorzero[i]
//******************************************************************************

#include "p30f6014a.h"
#include "stdio.h"
#include "string.h"

#include "uart/e_uart_char.h"
#include "a_d/e_prox.h"
#include "utility.h"
#include "sensorcalibrate.h"
int sensorzero[8];

void sensor_calibrate() {
	int i, j;
	char buffer[80];
	long sensor[8];

	for (i=0; i<8; i++) {
		sensor[i]=0;
	}

	for (j=0; j<32; j++) {
		for (i=0; i<8; i++) {
			sensor[i]+=e_get_prox(i);
		}
		wait(10000);
	}

	for (i=0; i<8; i++) {
		sensorzero[i]=(sensor[i]>>5);
		sprintf(buffer, "%d, ", sensorzero[i]);
		e_send_uart1_char(buffer, strlen(buffer));
	}

	sprintf(buffer, " calibration done\r\n");
	e_send_uart1_char(buffer, strlen(buffer));
	wait(100000);
}
