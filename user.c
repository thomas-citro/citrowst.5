#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>	
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <time.h>

#include "structs.h"
#include "config.h"
#include "prototypes.h"

// Semaphore & shared memory
int shmid; 
sm* ptr;
sem_t *sem;

// Globals
char *programName;

int main(int argc, char* argv[]) {
	programName = argv[0];
	
	// Semaphore
	sem = sem_open("thomas_sem", 0); 
	
	// Shared memory
	if ((shmid = shmget(9784, sizeof(sm), 0600)) < 0) {
		char *output = getOutputPerror();
		perror(output);
		exit(1);
	}
	ptr = shmat(shmid, NULL, 0);
	
	time_t t;
	time(&t);	
	srand((int)time(&t) % getpid()); // Seed random number generator
	
	int nextMove = getRandomInteger(0, ONE_MILLION); // Random time between 0 & 1 millisecs for when child should either request or release
	struct time moveTime;
	sem_wait(sem);
	moveTime.seconds = ptr->time.seconds;
	moveTime.nanoseconds = ptr->time.nanoseconds;
	sem_post(sem);

	int termination = getRandomInteger(0, TWO_HUNDRED_FIFTY_MILLION); // Random time between 0 & 250 millisecs for when child should check if it can terminate
	struct time termCheck;
	sem_wait(sem);
	termCheck.seconds = ptr->time.seconds;
	termCheck.nanoseconds = ptr->time.nanoseconds;
	sem_post(sem);
	
	while(1) {
		// Check if it's time to request/release
		if((ptr->time.seconds > moveTime.seconds) || (ptr->time.seconds == moveTime.seconds && ptr->time.nanoseconds >= moveTime.nanoseconds)) {
			sem_wait(sem);
			moveTime.seconds = ptr->time.seconds;
			moveTime.nanoseconds = ptr->time.nanoseconds;
			sem_post(sem);
			incrementClock(&moveTime, 0, nextMove); // Increment time for next request/release

			int chanceToRequest = getRandomInteger(1, 100);
			if(chanceToRequest < 95) { // 95% chance to request
				// Mark the process's request in shared memory
				ptr->resourceStruct.requestF = 1;
				int resourceIndex = getRandomInteger(0, 19);
				ptr->resourceStruct.index = resourceIndex;
			} else ptr->resourceStruct.releaseF = 1; // Mark the process's release in shared memory
		}
		int chanceToTerminate = getRandomInteger(1, 100);
		
		// Check if it's time to check for termination
		if((ptr->time.seconds > termCheck.seconds) || (ptr->time.seconds == termCheck.seconds && ptr->time.nanoseconds >= termCheck.nanoseconds)) {
			// Set the time for the next termination check
			termination = getRandomInteger(0, TWO_HUNDRED_FIFTY_MILLION); // Random time between 0 & 250 millisecs
			termCheck.seconds = ptr->time.seconds;
			termCheck.nanoseconds = ptr->time.nanoseconds;
			incrementClock(&termCheck, 0, termination);
			
			if(chanceToTerminate < 10) ptr->resourceStruct.termF = 1; // 10% chance to terminate
			exit(0);
		}
	}	
	return 0;
}

void incrementClock(struct time* time, int sec, int ns) {
	sem_wait(sem);
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= ONE_BILLION) {
		time->nanoseconds -= ONE_BILLION;
		time->seconds++;
	}
	sem_post(sem);
}

// Return random integer between lower and upper bounds (inclusive)
int getRandomInteger(int lower, int upper) {
	int num = (rand() % (upper - lower + 1)) + lower;
	return num;
}

char *getOutputPerror () {
	char* output = strdup(programName);
	strcat(output, ": Error");
	return output;
}