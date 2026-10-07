/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * file:        supervisor_crown.c
 * author:      
 * description: Collective decision / wall-following with e-Pucks and radios for Webots
 *              Read all receiver channels and print time for convergence if converged
 * $Revision$	October 26, 2015 by Florian Maushart
 * $Date$
 * $Author$
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <webots/emitter.h>
#include <webots/receiver.h>
#include <webots/supervisor.h>
#include <webots/robot.h>

#define ROBOTS 10
#define STEP_SIZE 64

static WbNodeRef rob[ROBOTS];         // References to robots
WbDeviceTag      rec[ROBOTS];         // Supervisor receivers

char status[ROBOTS];                  // Status of each robot
bool printed;                         // Only print once


/* Reset the supervisor */
void reset(void) {
 
  int i;
  printed = false;                    // Not yet printed

  for (i=0;i<ROBOTS;i++)
  {
    char aux[15];
    /* Get and save a reference to the robot/channel (epuck_1_0, epuck_2_0, ... ) */
    strcpy(aux,"epuck_");
    sprintf(aux,"%s%d",aux,i+1);
    strcat(aux,"_0");
    rob[i] = wb_supervisor_node_get_from_def(aux); // get the robot handles

    /* Get supervisor receivers (rec1, rec2, ...) */
    strcpy(aux,"rec");
    sprintf(aux,"%s%d",aux,i+1);     
    rec[i] = wb_robot_get_device(aux);
    if (rec[i]==0) printf("missing receiver %d\n",i);
    wb_receiver_enable(rec[i],32);  // enable the supervisor receivers
  }
}

/* Set direction of robot_1_0 as reference, test if everyone else moves in the same dir */
bool converged(void)
{
  int i;
  char dir = status[0];  // current direction

  for (i=1;i<ROBOTS;i++)
    if (status[i] != dir) return false;

  return true;
}

/* run and count the time */
static int run(int ms) {

  static unsigned long long int clock = 0; // counts the time that has passed

  char *buf;            // Data from robots
  int i;                // FOR-loop counter

  /* Get data */
  for (i=0;i<ROBOTS;i++) {
    /* Check if we're receiving data */
    if(wb_receiver_get_queue_length(rec[i]) > 0) 
    {
      /* iterate through data packets */
      assert(wb_receiver_get_queue_length(rec[i])>0);
      assert(wb_receiver_get_data_size(rec[i])==1);
      buf = (char*)wb_receiver_get_data(rec[i]); 
      status[i] = *buf; 
      wb_receiver_next_packet(rec[i]);
    }
  }

  /* test convergence */
  if (!printed && converged())
  {
      printf("\n\n\n\n\n\n============================================================ CONVERGED IN %.3f SECONDS ===============\n\n\n\n\n\n",clock/1000.0);
      fflush(stdout);
      printed = true;
  }

  clock+=STEP_SIZE; // increment time counter
  return STEP_SIZE;
}


/* main loop */
int main(void) 
{
  // initialization
  wb_robot_init();
  
  reset();  
  wb_robot_step(2*STEP_SIZE);

  // start the controller
  printf("Starting main loop...\n");
  while (wb_robot_step(STEP_SIZE) != -1)
  {
    run(STEP_SIZE);
  }
  
  wb_robot_cleanup();
  return 0;

}

