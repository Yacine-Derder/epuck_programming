/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * file:        epuck_crown.c
 * author:      Christopher M. Cianci
 * description: Collective decision / wall-following with e-Pucks and radios for Webots
 *
 * $Revision$	October 26, 2015 by Florian Maushart
 * $Date$
 * $Author$
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

// #include <sstream>
// using namespace std;

#include <webots/robot.h>
#include <webots/differential_wheels.h>
#include <webots/emitter.h>
#include <webots/receiver.h>
#include <webots/distance_sensor.h>
#include <webots/radio.h>

#define DEBUG 1
#define TIME_STEP 64


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Collective decision parameters */

#define OPCHANGE_PROB       0.5     // Probability of changing opinion
#define RIGHT_WALL_PROB     0.5     // Probability of initially selecting RIGHT
#define DECISION_INTERVAL   1       // Frequency of communication in seconds

enum follow_state {
    RANDOM  = 0,                    // Initial state aliases
    LEFT    = 1,
    RIGHT   = 2,
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* e-Puck parameters */

#define NB_SENSORS           8
#define BIAS_SPEED           400

// Weights for the Braitenberg algorithm
int Interconn[16] = {5,-4,-6,3,5,4,4,-20,-20,-15,-5,5,3,-5,-15,-15};


// The state variables
int robot_id;                       // Unique robot ID
char state;                         // Wall following state

int lmsg, rmsg;                     // Communication variables

static char display[256]="";

// Proximity and radio handles
WbDeviceTag em;
static WbDeviceTag ds[NB_SENSORS];    // Handle for the infrared distance sensors
static WbDeviceTag radio;             // Radio


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* helper functions */

// Generate random number in [0,1]
double rnd(void) {
  return ((double)rand())/((double)RAND_MAX);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* opinion functions */

// We received a packet and increase the message counters accordingly
static void radio_callback(WbRadioEvent event) 
{
    // Read & Print content of package
    // comment to avoid spam in the console
    snprintf(display,256,
        "received \"%i\" (%d bytes) from %s "
        "with RSSI=%g on radio %d",
        wb_radio_event_get_data(event)[0],
        wb_radio_event_get_data_size(event),
        wb_radio_event_get_emitter(event),
        wb_radio_event_get_rssi(event),
        wb_radio_event_get_radio(event)); 
    

    // Increment counters
    if(wb_radio_event_get_data(event)[0]==RIGHT) rmsg++;
    if(wb_radio_event_get_data(event)[0]==LEFT ) lmsg++;
}

/* Check for change of opinion */
// This code is executed every 1/DECISION_INTERVAL s.
int changeOpinion() 
{
    char temp;

    /* Check for change from left to right */
    // With probability OPCHANGE_PROB, take the opinion of the majority
    if (state == LEFT && rmsg > lmsg) {
        if (rnd() < OPCHANGE_PROB) {
            printf("robot %d changes to right\n",robot_id);
            temp = RIGHT;
            wb_emitter_send(em,&temp,sizeof(char));
            return RIGHT;
        } 
    }

    /* Check for change from right to left */
    // With probability OPCHANGE_PROB, take the opinion of the majority
    if (state == RIGHT && lmsg > rmsg) {
        if (rnd() < OPCHANGE_PROB) {
            printf("robot %d changes to left\n",robot_id);
            temp = LEFT;
            wb_emitter_send(em,&temp,sizeof(char));
            return LEFT;
        }
    }

    /* Otherwise don't change */
    return state;
}

// Broadcasts your current opinion
void broadcast_opinion(){

	WbRadioMessage msg;

	msg = wb_radio_message_new(6,&state,"255.255.255.255");
        wb_radio_send(radio,msg,0);

}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* RESET and INIT (combined in function reset()) */

void reset(void) 
{
    int i; // FOR-loop counter
  
    char emi_name[64];

    char s[4]="ps0";
    for(i=0; i<NB_SENSORS;i++) 
    {
        // the device name is specified in the world file
        ds[i]=wb_robot_get_device(s);      
        s[2]++; // increases the device number
        wb_distance_sensor_enable(ds[i],64);
    }

    // read robot id and state from the robot's name
    char* robot_name; robot_name=(char*) wb_robot_get_name(); 
    sscanf(robot_name,"epuck_%d_%d",&robot_id,(int*)&state);

    sprintf(emi_name,"emi%d",robot_id);
    em = wb_robot_get_device(emi_name);
    if (em==0) printf("missing receiver %d\n",i);
    //wb_emitter_enable(em,32);

    radio = wb_robot_get_device("radio");

    wb_radio_enable(radio, TIME_STEP);
    
    wb_radio_set_callback(radio,radio_callback);
    printf("robot %d with tx_power: %f \n",robot_id,wb_radio_get_tx_power(radio));

    srand(getpid()); // Seed random generator

    if (state == RANDOM) // Check initial state 
    {
        /* Generate random initial state */
        if (rnd() < RIGHT_WALL_PROB) state = RIGHT;
        else state = LEFT;
        printf("Robot %d with random selection: %d\n",robot_id,state);
    } 
    else 
    {
        printf("Robot %d with deterministic selection: %d\n",robot_id,state);
    }

    wb_emitter_send(em,&state,sizeof(char));

    lmsg = 0; rmsg = 0; // Clear message variables
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* WALL-FOLLOWING */

void run(int ms)
{
    // Motor speed and sensor variables	
    int msl,msr;                // motor speed left and right
    int d1,d2;                  // motor speed 1 and 2 
    int distances[NB_SENSORS];  // array keeping the distance sensor readings
	
    // Forward speed        
    d1=0; d2=0;

    // Other variables
    static int clock = 0;    // Artificial TIMER1
    int sensor_nb;           // FOR-loop counters    

    // "TIMER" function to determine when to check & broadcast the opinion
    // (This would be done via an interrupt on a real e-puck)
    if ( clock % (int)(1024*DECISION_INTERVAL) == ms*robot_id )
    {
            /* Check for opinion change */
            state = changeOpinion();
            lmsg = 0; rmsg = 0;

            /* Broadcast local opinion */
            broadcast_opinion();
    }

    /* Print message received in the callback function */
    if (DEBUG && display[0]) {
            printf("%s\n",display);
            display[0]=0; /* don't display twice */
    }

    // Add the weighted sensors values -> obstacle avoidance and wall following
    for(sensor_nb=0;sensor_nb<NB_SENSORS;sensor_nb++)
    {  // read sensor values and calculate motor speeds
        /* Check for state */
        if (state == RIGHT)
            distances[sensor_nb] =
                wb_distance_sensor_get_value(ds[sensor_nb]);
        else
            distances[sensor_nb] =
                wb_distance_sensor_get_value(ds[NB_SENSORS-1-sensor_nb]);

        d1 += (distances[sensor_nb]-300) * Interconn[sensor_nb];
        d2 += (distances[sensor_nb]-300) * Interconn[sensor_nb + NB_SENSORS];
    }

    d1 /= 80; d2 /= 80;        // Normalizing speeds

    /* Check for state: Right or left wall follow */
    if (state == RIGHT) { msr = d1+BIAS_SPEED; msl = d2+BIAS_SPEED; } 
    else                { msr = d2+BIAS_SPEED; msl = d1+BIAS_SPEED; }
      
    // Speed bounds, to avoid setting to high speeds to the motor
    if (msl > 1000) {msl = 1000;}
    if (msr > 1000) {msr = 1000;}
    if (msl< -1000) {msl = -1000;}
    if (msr < -1000) {msr = -1000;}
    // Set the speed
    wb_differential_wheels_set_speed(msl,msr);

    clock += ms;
    return;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* MAIN */

int main(int argc, char **argv) {
  
  wb_robot_init();
  
  reset();
  
  // RUN THE MAIN ALGORIHM
  while (wb_robot_step(TIME_STEP) != -1) {
    run(TIME_STEP);
  }
  
  wb_robot_cleanup();
  return 0;
}
