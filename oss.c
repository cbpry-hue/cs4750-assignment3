#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

// Defined based on project requirements [cite: 16, 20, 21]
typedef struct {
    int occupied;           // 1 if in use, 0 if free [cite: 23, 24, 38]
    pid_t pid;              // Process ID of the child [cite: 25, 29]
    int startSeconds;       // Simulated time when forked [cite: 26, 29]
    int startNano;          // Simulated time when forked [cite: 27]
    int endingTimeSeconds;  // Estimated termination time [cite: 28, 33]
    int endingTimeNano;     // Estimated termination time [cite: 28]
    int messagesSent;       // Total messages sent to this child [cite: 30, 31]
} PCB;

// Global Process Table [cite: 16, 32]
PCB processTable[20]; // [cite: 32]

// Message buffer structure
typedef struct {
    long mtype;     // This will be the child's PID [cite: 14, 128]
    int status;     // 1 to continue, 0 to terminate [cite: 124]
} msgbuffer;

// Global variables for IPC IDs so our signal handler can clean them up
int shmid;
int msqid;
int* sysClock; // sysClock[0] = seconds, sysClock[1] = nanoseconds

// Signal handler for Ctrl-C and the 60-second timeout [cite: 120]
void cleanupAndExit(int signum) {
    if (signum == SIGALRM) {
        printf("\n[OSS] 60-second timeout reached. Terminating...\n"); // [cite: 118, 119]
    } else if (signum == SIGINT) {
        printf("\n[OSS] Ctrl-C caught. Cleaning up...\n"); // [cite: 120]
    }

    // 1. Send kill signals to all active child processes (To be implemented) [cite: 119, 120]
    
    // 2. Detach and remove shared memory [cite: 120, 142]
    shmdt(sysClock);
    shmctl(shmid, IPC_RMID, NULL);
    
    // 3. Remove message queue [cite: 142]
    msgctl(msqid, IPC_RMID, NULL);

    printf("[OSS] IPC cleanup complete. Exiting.\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    // 1. Setup Signal Handlers [cite: 119, 120]
    signal(SIGINT, cleanupAndExit); // [cite: 120]
    signal(SIGALRM, cleanupAndExit); // [cite: 119]
    alarm(60); // Trigger SIGALRM after 60 real-life seconds [cite: 118, 121]

    // 2. Parse Command Line Arguments [cite: 74, 75]
    int opt;
    int totalProcesses = 0;     // -n parameter [cite: 77]
    int simulProcesses = 0;     // -s parameter [cite: 77, 89]
    float timeLimit = 0.0;      // -t parameter [cite: 77, 80]
    float launchInterval = 0.0; // -i parameter [cite: 78, 83]
    char logFileName[256] = "log.txt"; // -f parameter [cite: 78, 84]

    // Parse options using getopt [cite: 146]
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: ./oss [-h] [-n proc] [-s simul] [-t timeLimit] [-i interval] [-f logfile]\n"); // [cite: 77, 78]
                return 0;
            case 'n': totalProcesses = atoi(optarg); break;
            case 's': simulProcesses = atoi(optarg); break;
            case 't': timeLimit = atof(optarg); break;
            case 'i': launchInterval = atof(optarg); break;
            case 'f': strncpy(logFileName, optarg, sizeof(logFileName) - 1); break;
            default:
                fprintf(stderr, "Invalid option provided.\n");
                return 1;
        }
    }

    // 3. Set up Shared Memory for the Simulated Clock [cite: 88, 147]
    // We need space for two integers: seconds and nanoseconds
    key_t shmKey = ftok("oss.c", 1); 
    shmid = shmget(shmKey, 2 * sizeof(int), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("OSS: Failed to create shared memory");
        exit(1);
    }
    sysClock = (int*)shmat(shmid, NULL, 0);
    sysClock[0] = 0; // Seconds
    sysClock[1] = 0; // Nanoseconds

    // 4. Set up the Message Queue [cite: 88, 123]
    key_t msgKey = ftok("oss.c", 2);
    msqid = msgget(msgKey, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("OSS: Failed to create message queue");
        cleanupAndExit(0);
    }

    printf("OSS Initialization complete. Clock and Message Queue ready.\n");

    // Open the log file for writing (append mode or write mode)
    FILE* logFP = fopen(logFileName, "w");
    if (logFP == NULL) {
        perror("OSS: Failed to open log file");
        cleanupAndExit(1);
    }

    int activeChildren = 0;
    int totalLaunched = 0;
    int nextChildIndex = 0;
    int totalMessagesSent = 0;
    int lastTablePrintSec = 0;
    int lastTablePrintNano = 0;

    // Seed the random number generator for time bounds
    srand(getpid());

    // Main coordination loop [cite: 95]
    while (totalLaunched < totalProcesses || activeChildren > 0) {
        
        // 1. Increment Clock [cite: 96, 113]
        // Increment by 250ms divided by the number of current children [cite: 115]
        int incrementNano = 250000000; 
        if (activeChildren > 0) {
            incrementNano /= activeChildren; [cite: 116, 117]
        }
        
        sysClock[1] += incrementNano;
        if (sysClock[1] >= 1000000000) {
            sysClock[0]++;
            sysClock[1] -= 1000000000;
        }

        // 2. Possibly Launch New Child [cite: 106]
        // Obey total process limits and simultaneous limits [cite: 89, 90]
        if (totalLaunched < totalProcesses && activeChildren < simulProcesses) {
            // Find a free slot in the process table [cite: 36, 37]
            int slot = -1;
            for (int i = 0; i < 20; i++) {
                if (processTable[i].occupied == 0) {
                    slot = i;
                    break;
                }
            }

            if (slot != -1) {
                // Generate random run time for child based on -t parameter [cite: 80, 81]
                // Random seconds between 1 and timeLimit
                int maxSec = (int)timeLimit;
                if (maxSec < 1) maxSec = 1;
                int durSec = (rand() % maxSec) + 1; [cite: 81]
                int durNano = rand() % 1000000000; [cite: 81]

                // Prepare string arguments for execlp
                char secStr[20], nanoStr[20];
                sprintf(secStr, "%d", durSec);
                sprintf(nanoStr, "%d", durNano);

                pid_t pid = fork(); [cite: 88]
                if (pid == 0) {
                    execlp("./worker", "./worker", secStr, nanoStr, NULL); [cite: 88]
                    perror("OSS: execlp failed");
                    exit(1);
                } else if (pid > 0) {
                    // Parent updates the PCB [cite: 91]
                    processTable[slot].occupied = 1; [cite: 23]
                    processTable[slot].pid = pid; [cite: 25]
                    processTable[slot].startSeconds = sysClock[0]; [cite: 26, 29]
                    processTable[slot].startNano = sysClock[1]; [cite: 27, 29]
                    processTable[slot].messagesSent = 0; [cite: 30]
                    // Estimate ending time [cite: 33]
                    processTable[slot].endingTimeSeconds = sysClock[0] + durSec; [cite: 28]
                    processTable[slot].endingTimeNano = sysClock[1] + durNano; [cite: 28]
                    if (processTable[slot].endingTimeNano >= 1000000000) {
                        processTable[slot].endingTimeSeconds++;
                        processTable[slot].endingTimeNano -= 1000000000;
                    }

                    activeChildren++;
                    totalLaunched++;

                    // Output process table immediately after launch [cite: 110]
                    printf("\nOSS: Launched child %d in slot %d\n", pid, slot);
                    fprintf(logFP, "\nOSS: Launched child %d in slot %d\n", pid, slot);
                    // TODO: Call your printProcessTable() helper function here
                }
            }
        }

        // 3. Send Message and Wait for Reply if we have active children [cite: 97]
        if (activeChildren > 0) {
            // Calculate next child to send a message to (Round Robin) 
            do {
                nextChildIndex = (nextChildIndex + 1) % 20;
            } while (processTable[nextChildIndex].occupied == 0);

            pid_t targetPid = processTable[nextChildIndex].pid;
            
            // Output sending log to screen and file [cite: 85, 98, 128, 129]
            printf("OSS: Sending message to worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]); [cite: 129]
            fprintf(logFP, "OSS: Sending message to worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]); [cite: 129]

            // Send message to the specific child [cite: 99]
            msgbuffer sendBuf;
            sendBuf.mtype = targetPid; // Worker is waiting for its PID [cite: 1458, 1466]
            sendBuf.status = 1; 

            if (msgsnd(msqid, &sendBuf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
                perror("OSS: msgsnd failed");
                break;
            }
            processTable[nextChildIndex].messagesSent++; [cite: 30, 31]
            totalMessagesSent++;

            // Wait for message back from the child [cite: 100]
            msgbuffer rcvBuf;
            if (msgrcv(msqid, &rcvBuf, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1) { [cite: 1464]
                perror("OSS: msgrcv failed");
                break;
            }

            // Output receiving log [cite: 101, 129]
            printf("OSS: Receiving message from worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]); [cite: 129]
            fprintf(logFP, "OSS: Receiving message from worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]); [cite: 129]

            // Check if child decided to terminate [cite: 102]
            if (rcvBuf.status == 0) { [cite: 124]
                printf("OSS: Worker %d PID %d is planning to terminate.\n", nextChildIndex, targetPid); [cite: 103, 130]
                fprintf(logFP, "OSS: Worker %d PID %d is planning to terminate.\n", nextChildIndex, targetPid); [cite: 130]
                
                // Wait for it to clear out [cite: 104, 105]
                waitpid(targetPid, NULL, 0); 

                // Update PCB [cite: 38, 105]
                processTable[nextChildIndex].occupied = 0; [cite: 38]
                activeChildren--;
            }
        }

        // 4. Print Process Table every half a simulated second [cite: 111]
        long long currentTotalNano = (long long)sysClock[0] * 1000000000LL + sysClock[1];
        long long lastPrintTotalNano = (long long)lastTablePrintSec * 1000000000LL + lastTablePrintNano;
        
        if (currentTotalNano - lastPrintTotalNano >= 500000000LL) {
            // TODO: Call your printProcessTable() helper function here [cite: 111, 112]
            lastTablePrintSec = sysClock[0];
            lastTablePrintNano = sysClock[1];
        }
    }

    // 5. Ending Report [cite: 107, 132]
    printf("\n--- System Simulation Complete ---\n");
    printf("Total processes launched: %d\n", totalLaunched); [cite: 134]
    printf("Total messages sent by OSS: %d\n", totalMessagesSent); [cite: 134]
    
    fprintf(logFP, "\n--- System Simulation Complete ---\n");
    fprintf(logFP, "Total processes launched: %d\n", totalLaunched); [cite: 134]
    fprintf(logFP, "Total messages sent by OSS: %d\n", totalMessagesSent); [cite: 134]

    fclose(logFP);

    // Normal termination cleanup
    cleanupAndExit(0);
    return 0;
}