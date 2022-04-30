#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
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

// Statistics
int totalRequestsGranted = 0;
int totalDeadlockTerminations = 0;
int totalNormalTerminations = 0;
int totalDeadlockAlgorithmsRan = 0;
float sumOfAverageTerminations = 0;

// Other globals
int resourcesShared[4];
int blockedQueue[20];
int resourceIndexQueue[20];
int pidNumber = 0;
int timeForNextDeadlockCheck = 1; // In seconds
int blockPtr = 0;
int verbose = 0;
FILE* fp;
char *programName;

int main(int argc, char* argv[]) {
	// Declare/initialize variables
	int i;
	int j;
	int activeChildren[20];
	int writtenLines = 0;
	int currentRequestsGranted = 0;
	programName = argv[0];
	
	verbose = parseArguments(argc, argv);
	fp = fopen("oss.log", "w");
	
	int maxChildren = 40;
	srand(time(NULL));
	int numProcs = 0;	
	
	// Semaphore
	sem = sem_open("thomas_sem", O_CREAT, 0777, 1);
	
	// Shared memory
	if ((shmid = shmget(9784, sizeof(sm), IPC_CREAT | 0600)) < 0) {
		char *output = getOutputPerror();
		perror(output);
		exit(1);
	}
	ptr = shmat(shmid, NULL, 0);

	pid_t childPid;
	int pid = 0;
 	time_t t;
	srand((unsigned) time(&t)); // Seed random number generator
	int totalNumProcs = 0;
	alarm(5); // Start alarm for 5 real-time seconds
	
	for (i = 0; i < 20; i++) {
		// Setup initial number of resources (between 1 and 20 inclusive)
		ptr->resourceStruct.max[i] = getRandomInteger(1, 20);
		ptr->resourceStruct.available[i] = ptr->resourceStruct.max[i];
	}
	for (i = 0; i < 4; i++) resourcesShared[i] = getRandomInteger(0, 19); // Make 4 resources shared
	for (i = 0; i < 18; i++) {
		for (j = 0; j < 20; j++) ptr->descriptor[i].allocated[j] = 0; // Initialize allocations to all zeros
	}
	for (i = 0; i < 20; i++) blockedQueue[i] = -1; // Initialize blocked queue so no processes are blocked
	for (i = 0; i < 18; i++) activeChildren[i] = i; // All processes start as active
	
	// Catch 5 second timer and Ctrl+C interupts
	if (signal(SIGINT, signalHandlers) == SIG_ERR) exit(0);
	if (signal(SIGALRM, signalHandlers) == SIG_ERR) exit(0);
	
	struct time randomFork;
	/* ----- START MAIN LOOP ----- */
	// This loop runs until 40 total children are forked, more than 5 real-time secs pass, or no child is remaining
	while(totalNumProcs < maxChildren || numProcs > 0) {
		if (waitpid(childPid,NULL, WNOHANG)> 0) {
			numProcs--;
		}
		
		// Increment the clock by the fork + 50,000 nanoseconds
		incrementClock(&ptr->time,0,50000);
		int nextFork = getRandomInteger(ONE_MILLION, FIVE_HUNDRED_MILLION); // Random time between 1 & 500 millisecs for the next fork
		incrementClock(&randomFork,0,nextFork);
		
		// Check to make sure there's less than 18 children currently alive
		if (numProcs < 18 && ptr->time.nanoseconds < nextFork) {
			sem_wait(sem);
			randomFork.seconds = ptr->time.seconds;
			randomFork.nanoseconds = ptr->time.nanoseconds;
			sem_post(sem);
			nextFork = getRandomInteger(ONE_MILLION, FIVE_HUNDRED_MILLION); // Random time between 1 & 500 millisecs for the next fork
			incrementClock(&randomFork,0,nextFork);
			
			// Check if all children terminated
			int numTerminated = 0;
			for (i = 0; i < 18; i++) {
				if (activeChildren[i] == -1) numTerminated++;
			}
			
			if (numTerminated == 18) {
				// All children terminated, so end program
				cleanup();
				printStatistics();
				return 0;
			} else numTerminated = 0;
			
			if (activeChildren[pidNumber] != -1) {
				// Blocked
				pid = activeChildren[pidNumber];
			} else {
				// Not blocked
				for (i = pidNumber; i < 18; i++) {
					if (activeChildren[i] != -1) break;
				}
				pidNumber = i;
				pid = activeChildren[pidNumber];
			}
			
			// Fork child
			totalNumProcs++;
			numProcs++;
			childPid = fork();
			if (childPid == 0) {
				char passPid[10];
				sprintf(passPid, "%d", pid);		
				execl("./user","user", NULL);
				exit(0);
			}
			
			// Check to see if child is requesting a resource, then grant or block the process based on the resource's availability
			if (ptr->resourceStruct.requestF == 1) {
				ptr->resourceStruct.requestF = 0;
				int resourceIndex = ptr->resourceStruct.index;
				ptr->descriptor[pid].request[resourceIndex] = getRandomInteger(1, 20);
				
				if (verbose && writtenLines < 10000) {
					fprintf(fp,"OSS: Detected Process P%d requesting R%d at time %d:%d\n",pid, resourceIndex, ptr->time.seconds,ptr->time.nanoseconds);
					writtenLines++;
				}
				int resultBlocked = checkBlocked(pid, resourceIndex);

				if (resultBlocked == 0) {
					// Block process if not already blocked
					if (verbose && writtenLines < 10000) {
						fprintf(fp,"OSS: Placing Process P%d in wait for R%d at time %d:%d\n",pid, resourceIndex, ptr->time.seconds,ptr->time.nanoseconds);
						writtenLines++;
					}
					int duplicate = 0;
					for (i = 0; i < 18; i++) {
						if (blockedQueue[i] == pid) duplicate++;
					}
					if (duplicate == 0) {
						blockedQueue[blockPtr] = pid;
						resourceIndexQueue[blockPtr] = resourceIndex;
					}
					blockPtr++;
				} else {
					// Grant the user's request
					allocated(pid, resourceIndex);
					if (verbose && writtenLines < 10000) {
						fprintf(fp,"OSS: Granting Process P%d request R%d at time %d:%d\n",pid, resourceIndex, ptr->time.seconds,ptr->time.nanoseconds);
						writtenLines++;
					}
					totalRequestsGranted++;
					currentRequestsGranted++;
					// Print the resource table every 20 requests granted
					if (currentRequestsGranted == 20 && verbose && writtenLines < 10000) {
						fprintf(fp,"\n\nResource Table\n");
						currentRequestsGranted = 0;
						int k;
						fprintf(fp,"   ");
						for (i = 0; i < 20; i++) {
							fprintf(fp, "R%d  ", i);
						}
						fprintf(fp, "\n");
						for (i = 0; i < 18; i++) {
							k = i;
							fprintf(fp, "P%d   ", k);
							for (j = 0; j < 20; j++) {
								fprintf(fp, "%d   ", ptr->descriptor[k].allocated[j]);
							}
							fprintf(fp, "\n");
						}
						fprintf(fp, "\n");	
						writtenLines += 18;
					}	
				}
			}
				
			// Check to see if child terminates
			if (ptr->resourceStruct.termF == 1) {
				// Release allocated resources and update activeChildren array
				ptr->resourceStruct.termF = 0;
				if (verbose && writtenLines < 10000) {
					fprintf(fp,"OSS: Terminating Process P%d at time %d:%d\n",pid, ptr->time.seconds,ptr->time.nanoseconds);
					writtenLines++;
				}
				totalNormalTerminations++;
				activeChildren[pid] = -1;
				releaseResources(pid,0);
			}

			// Check to see if child is releasing a resource
			if (ptr->resourceStruct.releaseF == 1) {
				// Release resources allocated to the child
				ptr->resourceStruct.releaseF = 0;
				releaseResources(pid,0);
			}
			if (writtenLines < 10000) {
				runDeadlockDetectionAlgorithm();
				writtenLines += 20;
			}
		}
	}
	cleanup();
	return 0;
}

// Return random integer between lower and upper bounds (inclusive)
int getRandomInteger(int lower, int upper) {
	int num = (rand() % (upper - lower + 1)) + lower;
	return num;
}

int parseArguments(int argc, char* argv[]) {
	int option;
	int isVerbose = 0;
	while ((option = getopt (argc, argv, "hv")) != -1)
	switch (option) {
		case 'h':
			printUsageStatement();
			break;
		case 'v':
			isVerbose = 1;
			break;
		default:
			printUsageStatement();
	}
	return isVerbose;
}

void printUsageStatement() {
	fprintf(stderr, "Usage: ./oss [-v for verbose mode]\n");
	exit(0);
}

// Display statistics before ending program
void printStatistics() {
	printf("\n");
	printf("%60s", "- Statistics -\n");
	printf("  %-52s|   %'d\n", "Total Requests Granted", totalRequestsGranted);
	printf("  %-52s|   %'d\n", "Total Processes Terminated via Deadlock Algorithm", totalDeadlockTerminations);
	printf("  %-52s|   %'d\n", "Total Processes Terminated Normally", totalNormalTerminations);
	printf("  %-52s|   %'d\n", "Total Times Deadlock Algorithm Ran", totalDeadlockAlgorithmsRan);
	printf("  %-52s|   %'.2f%%\n\n", "Avg % of Processes Terminated in a Deadlock", (sumOfAverageTerminations / totalDeadlockAlgorithmsRan) * 100);
	
	fprintf(fp, "\n");
	fprintf(fp, "%60s", "- Statistics -\n");
	fprintf(fp, "  %-52s|   %'d\n", "Total Requests Granted", totalRequestsGranted);
	fprintf(fp, "  %-52s|   %'d\n", "Total Processes Terminated via Deadlock Algorithm", totalDeadlockTerminations);	
	fprintf(fp, "  %-52s|   %'d\n", "Total Processes Terminated Normally", totalNormalTerminations);
	fprintf(fp, "  %-52s|   %'d\n", "Total Times Deadlock Algorithm Ran", totalDeadlockAlgorithmsRan);
	fprintf(fp, "  %-52s|   %'.2f%%\n\n", "Avg % of Processes Terminated in a Deadlock", (sumOfAverageTerminations / totalDeadlockAlgorithmsRan) * 100);
}

void runDeadlockDetectionAlgorithm() {
	if (pidNumber < 17) pidNumber++;
	else {				
		int i, numBlocks = 0;
		for (i = 0; i < 20; i++) {
			if (blockedQueue[i] != -1) numBlocks++;
		}
		
		// Run deadlock recovery every second if there's any blocks
		if (numBlocks > 0 && ptr->time.seconds == timeForNextDeadlockCheck) {
			timeForNextDeadlockCheck++;
			runDeadlockRecoveryAlgorithm();
			totalDeadlockAlgorithmsRan++;
		}
		pidNumber = 0;		
		
		for (i = 0; i < 20; i++) blockedQueue[i] = -1; // Reset blocked queue so no children are blocked	
		blockPtr = 0;
	}	
}

void runDeadlockRecoveryAlgorithm() {
	fprintf(fp,"OSS: Running deadlock detection at time %d:%d\n", ptr->time.seconds,ptr->time.nanoseconds);
	int i;
	int j;
	float blockCount = 0;	
	float terminated = 0;
	float averageDL = 0;
	
	for (i = 0; i < 20; i++) {
		if (blockedQueue[i] != -1) {
			blockCount++;
		}
	}
	fprintf(fp,"	Process ");
	for (i = 0; i < blockCount; i++) {
		fprintf(fp, "P%d, ", blockedQueue[i]);
	}
	fprintf(fp, "deadlocked\n");
	fprintf(fp,"	Attempting to resolve deadlock...\n");
	
	// Attempt to resolve deadlock by terminating children 1-by-1 until resources become available
	for (i = 0; i < blockCount; i++) {	
		if (ptr->resourceStruct.available[resourceIndexQueue[i]] <= ptr->descriptor[blockedQueue[i]].request[resourceIndexQueue[i]]) {
			fprintf(fp,"	Killing Process P%d\n", blockedQueue[i]);
			fprintf(fp,"		");
			totalDeadlockTerminations++;
			releaseDeadlockResources(blockedQueue[i], 1);
			
			if (i + 1 < blockCount) {
				fprintf(fp,"	OSS: Running deadlock detection after P%d killed\n",blockedQueue[i]);
				fprintf(fp,"	Processes ");
				terminated++;
				for (j = i + 1; j < blockCount; j++) {
					fprintf(fp, "P%d, ",blockedQueue[j]);	
				}
				fprintf(fp, "deadlocked\n");
			}
		} else {
			allocated(blockedQueue[i], resourceIndexQueue[i]);
			fprintf(fp,"	OSS: Granting Process P%d request R%d at time %d:%d\n", blockedQueue[i], resourceIndexQueue[i], ptr->time.seconds, ptr->time.nanoseconds);
		}
	}
	fprintf(fp,"System is no longer in deadlock\n");
	fprintf(fp,"\n");
	
	// Log average for statistics
	averageDL = terminated / blockCount;
	sumOfAverageTerminations += averageDL; 
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

// Release resources for a child that chose to release
void releaseResources(int pid, int dl) {
	int i, j, numEmptyResources = 0;
	
	if (verbose) {
		fprintf(fp,"OSS: Acknowledging Process P%d releasing: ",pid);
	}

	// Display resources being released
	for (i = 0; i < 20; i++) {
		if (ptr->descriptor[pid].allocated[i] > 0) {
			if (verbose) {
				fprintf(fp,"R%d:%d ",i, ptr->descriptor[pid].allocated[i]);	
			}
			j++;
		} else if (ptr->descriptor[pid].allocated[i] == 0) {
			numEmptyResources++;
		}
	}
	
	if (numEmptyResources == 20 && verbose) {
		fprintf(fp," N/A ");
		fprintf(fp,"at time %d:%d \n", ptr->time.seconds,ptr->time.nanoseconds);
	} else {
		// Updated available resources with the newly released resources
		if (verbose) {
			fprintf(fp,"at time %d:%d", ptr->time.seconds,ptr->time.nanoseconds);
			fprintf(fp,"\n");
		}

		for (i = 0; i < 20; i++) {
			if (i == resourcesShared[0] || i == resourcesShared[1] || i == resourcesShared[2] || i == resourcesShared[3]) {
				ptr->descriptor[pid].allocated[i] = 0;
			} else {
				ptr->resourceStruct.available[i] += ptr->descriptor[pid].allocated[i];
				ptr->descriptor[pid].allocated[i] = 0;			
			}
		}
	}
}

// Release resources for a child that was terminated during deadlock recovery algorithm
void releaseDeadlockResources(int pid, int dl) {
	int i, j, numEmptyResources = 0;

	if (verbose == 0 || verbose) {
		fprintf(fp,"Resources released are as follows: ");
	}
	
	// Display resources being released
	for (i = 0; i < 20; i++) {
		if (ptr->descriptor[pid].allocated[i] > 0) {
			if (verbose == 0 || verbose) {
				fprintf(fp,"R%d:%d ",i, ptr->descriptor[pid].allocated[i]);
			}
			j++;
		} else if (ptr->descriptor[pid].allocated[i] == 0) numEmptyResources++;
	}
	if (numEmptyResources == 20) fprintf(fp," N/A\n");
	else {
		if (verbose == 0 || verbose) {
				fprintf(fp,"\n");
		}
        
		// Updated available resources with the newly released resources
		for (i = 0; i < 20; i++) {
			if (i == resourcesShared[0] || i == resourcesShared[1] || i == resourcesShared[2] || i == resourcesShared[3]) {
				ptr->descriptor[pid].allocated[i] = 0;
			} else {
				ptr->resourceStruct.available[i] += ptr->descriptor[pid].allocated[i];
				ptr->descriptor[pid].allocated[i] = 0;
			}
		}
	}
}

// Check if requested resources are available
int checkBlocked(int pid, int resourceIndex) {
	if (ptr->resourceStruct.available[resourceIndex] > 0) return 1;
	else return 0;
}

// Allocate resources
void allocated(int pid, int resourceIndex) {
	if (resourceIndex == resourcesShared[0] || resourceIndex == resourcesShared[1] || resourceIndex == resourcesShared[2] || resourceIndex == resourcesShared[3]) {
		ptr->resourceStruct.available[resourceIndex] = ptr->resourceStruct.max[resourceIndex];
	} else {
		ptr->resourceStruct.available[resourceIndex] = ptr->resourceStruct.available[resourceIndex] - ptr->descriptor[pid].request[resourceIndex];
	}
	ptr->descriptor[pid].allocated[resourceIndex] = ptr->descriptor[pid].request[resourceIndex];
}

void cleanup() {
	// Deallocate semaphore & shared memory
	shmctl(shmid, IPC_RMID, NULL);	
	sem_unlink("thomas_sem");
	sem_close(sem);
}

void signalHandlers(int signum) {
	if (signum == SIGINT) {
		printf("\nInterupted by Ctrl+C\n");
	} else {
		printf("\nInterupted by 5 second alarm\n");
	}
	cleanup();
	printStatistics();
    exit(0);
}

char *getOutputPerror () {
	char* output = strdup(programName);
	strcat(output, ": Error");
	return output;
}
