#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

// Defined based on project requirements 
typedef struct {
    int occupied;           // 1 if in use, 0 if free 
    pid_t pid;              // Process ID of the child 
    int startSeconds;       // Simulated time when forked 
    int startNano;          // Simulated time when forked 
    int endingTimeSeconds;  // Estimated termination time 
    int endingTimeNano;     // Estimated termination time 
    int messagesSent;       // Total messages sent to this child 
} PCB;

// Global Process Table 
PCB processTable[20];

// Message buffer structure
typedef struct {
    long mtype;     // This will be the child's PID
    int status;     // 1 to continue, 0 to terminate
} msgbuffer;

// Global variables for IPC IDs so our signal handler can clean them up
int shmid;
int msqid;
int* sysClock; // sysClock[0] = seconds, sysClock[1] = nanoseconds

// Signal handler for Ctrl-C and the 60-second timeout 
void cleanupAndExit(int signum) {
    if (signum == SIGALRM) {
        printf("\n[OSS] 60-second timeout reached. Terminating...\n");
    } else if (signum == SIGINT) {
        printf("\n[OSS] Ctrl-C caught. Cleaning up...\n");
    }

    // Send kill signals to all active child processes (To be implemented) 
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 1) {
            // Send SIGTERM to politely ask the child to terminate
            kill(processTable[i].pid, SIGTERM);
        }
    }
    
    // Detach and remove shared memory
    shmdt(sysClock);
    shmctl(shmid, IPC_RMID, NULL);
    
    // 3. Remove message queue 
    msgctl(msqid, IPC_RMID, NULL);

    printf("[OSS] IPC cleanup complete. Exiting.\n");
    exit(0);
}

int printProcessTable(FILE* logFP, int linesWritten) {
    pid_t ossPid = getpid();
    
    // --- Print to Screen ---
    printf("OSS PID:%d SysClockS: %d SysclockNano: %d\n", ossPid, sysClock[0], sysClock[1]);
    printf("Process Table:\n");
    printf("%-5s %-8s %-7s %-6s %-8s %-8s %-8s %-12s\n", 
           "Entry", "Occupied", "PID", "Starts", "StartN", "EndingTS", "EndingTN", "MessagesSent");
    
    for (int i = 0; i < 20; i++) {
        printf("%-5d %-8d %-7d %-6d %-8d %-8d %-8d %-12d\n", 
               i, processTable[i].occupied, processTable[i].pid, 
               processTable[i].startSeconds, processTable[i].startNano, 
               processTable[i].endingTimeSeconds, processTable[i].endingTimeNano, 
               processTable[i].messagesSent);
    }
    printf("\n"); 
    
    // --- Print to Log File ---
    if (linesWritten < 10000) {
        fprintf(logFP, "OSS PID:%d SysClockS: %d SysclockNano: %d\n", ossPid, sysClock[0], sysClock[1]);
        fprintf(logFP, "Process Table:\n");
        fprintf(logFP, "%-5s %-8s %-7s %-6s %-8s %-8s %-8s %-12s\n", 
               "Entry", "Occupied", "PID", "Starts", "StartN", "EndingTS", "EndingTN", "MessagesSent");
        linesWritten += 3;
        
        for (int i = 0; i < 20; i++) {
            fprintf(logFP, "%-5d %-8d %-7d %-6d %-8d %-8d %-8d %-12d\n", 
                   i, processTable[i].occupied, processTable[i].pid, 
                   processTable[i].startSeconds, processTable[i].startNano, 
                   processTable[i].endingTimeSeconds, processTable[i].endingTimeNano, 
                   processTable[i].messagesSent);
            linesWritten++;
        }
        fprintf(logFP, "\n");
        linesWritten++;
    }
    
    return linesWritten;
}

int main(int argc, char* argv[]) {
    // Setup Signal Handlers 
    signal(SIGINT, cleanupAndExit);
    signal(SIGALRM, cleanupAndExit);
    alarm(60); // Trigger SIGALRM after 60 real-life seconds 

    // Parse Command Line Arguments 
    int opt;
    int linesWritten = 0;
    int activeChildren = 0;
    int totalProcesses = 0;     // -n parameter 
    int simulProcesses = 0;     // -s parameter 
    float timeLimit = 0.0;      // -t parameter 
    float launchInterval = 0.0; // -i parameter 
    char logFileName[256] = "log.txt"; // -f parameter 
    long long nextLaunchTimeNano = 0; // Tracks when we can launch the next process

    // Parse options using getopt 
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: ./oss [-h] [-n proc] [-s simul] [-t timeLimit] [-i interval] [-f logfile]\n");
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

    // Set up Shared Memory for the Simulated Clock 
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

    // Set up the Message Queue 
    key_t msgKey = ftok("oss.c", 2);
    msqid = msgget(msgKey, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("OSS: Failed to create message queue");
        cleanupAndExit(0);
    }

    printf("OSS Initialization complete. Clock and Message Queue ready.\n");

    for (int i = 0; i < 20; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].messagesSent = 0;
    }

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

    // Main coordination loop
    while (totalLaunched < totalProcesses || activeChildren > 0) {
        
        // Increment Clock 
        // Increment by 250ms divided by the number of current children 
        int incrementNano = 250000000; 
        if (activeChildren > 0) {
            incrementNano /= activeChildren;
        }
        
        sysClock[1] += incrementNano;
        if (sysClock[1] >= 1000000000) {
            sysClock[0]++;
            sysClock[1] -= 1000000000;
        }

        // Calculate current total simulated nanoseconds
        long long currentTotalSimNano = (long long)sysClock[0] * 1000000000LL + sysClock[1];

        // Possibly Launch New Child 
        // Obey total process limits, simultaneous limits, AND the launch interval 
        if (totalLaunched < totalProcesses && activeChildren < simulProcesses && currentTotalSimNano >= nextLaunchTimeNano) {
            // Find a free slot in the process table 
            int slot = -1;
            for (int i = 0; i < 20; i++) {
                if (processTable[i].occupied == 0) {
                    slot = i;
                    break;
                }
            }

            if (slot != -1) {
                // Generate random run time for child based on fractional -t parameter
                float random_time = ((float)rand() / (float)RAND_MAX) * timeLimit;
                
                // Extract whole seconds
                int durSec = (int)random_time;
                
                // Extract the fractional part and convert to nanoseconds
                int durNano = (int)((random_time - (float)durSec) * 1000000000.0f); 

                // Edge case: Ensure the worker runs for at least some tiny amount of time
                // to prevent immediate 0-time termination bugs
                if (durSec == 0 && durNano == 0) {
                    durNano = 10000; 
                }

                // Prepare string arguments for execlp
                char secStr[20], nanoStr[20];
                sprintf(secStr, "%d", durSec);
                sprintf(nanoStr, "%d", durNano);

                pid_t pid = fork(); 
                if (pid == 0) {
                    execlp("./worker", "./worker", secStr, nanoStr, NULL); 
                    perror("OSS: execlp failed");
                    exit(1);
                } else if (pid > 0) {
                    // Parent updates the PCB 
                    processTable[slot].occupied = 1; 
                    processTable[slot].pid = pid; 
                    processTable[slot].startSeconds = sysClock[0]; 
                    processTable[slot].startNano = sysClock[1]; 
                    processTable[slot].messagesSent = 0;
                    // Estimate ending time 
                    processTable[slot].endingTimeSeconds = sysClock[0] + durSec;
                    processTable[slot].endingTimeNano = sysClock[1] + durNano;
                    if (processTable[slot].endingTimeNano >= 1000000000) {
                        processTable[slot].endingTimeSeconds++;
                        processTable[slot].endingTimeNano -= 1000000000;
                    }

                    activeChildren++;
                    totalLaunched++;

                    // Calculate when the NEXT process is allowed to launch based on -i
                    long long intervalNano = (long long)(launchInterval * 1000000000.0f);
                    nextLaunchTimeNano = currentTotalSimNano + intervalNano;

                    // Output process table immediately after launch
                    printf("\nOSS: Launched child %d in slot %d\n", pid, slot);
                    if (linesWritten < 10000) {
                        fprintf(logFP, "\nOSS: Launched child %d in slot %d\n", pid, slot);
                        linesWritten++;
                    }
                    linesWritten = printProcessTable(logFP, linesWritten);
                }
            }
        }

        // Send Message and Wait for Reply if we have active children
        if (activeChildren > 0) {
            // Calculate next child to send a message to (Round Robin) 
            do {
                nextChildIndex = (nextChildIndex + 1) % 20;
            } while (processTable[nextChildIndex].occupied == 0);

            pid_t targetPid = processTable[nextChildIndex].pid;
            
            // Output sending log to screen and file
            printf("OSS: Sending message to worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]);
            fprintf(logFP, "OSS: Sending message to worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]);

            // Send message to the specific child 
            msgbuffer sendBuf;
            sendBuf.mtype = targetPid; // Worker is waiting for its PID 
            sendBuf.status = 1; 

            if (msgsnd(msqid, &sendBuf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
                perror("OSS: msgsnd failed");
                break;
            }
            processTable[nextChildIndex].messagesSent++;
            totalMessagesSent++;

            // Wait for message back from the child
            msgbuffer rcvBuf;
            if (msgrcv(msqid, &rcvBuf, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1) {
                perror("OSS: msgrcv failed");
                break;
            }

            // Output receiving log
            printf("OSS: Receiving message from worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]);
            fprintf(logFP, "OSS: Receiving message from worker %d PID %d at time %d:%d\n", 
                   nextChildIndex, targetPid, sysClock[0], sysClock[1]);

            // Check if child decided to terminate
            if (rcvBuf.status == 0) {
                printf("OSS: Worker %d PID %d is planning to terminate.\n", nextChildIndex, targetPid);
                fprintf(logFP, "OSS: Worker %d PID %d is planning to terminate.\n", nextChildIndex, targetPid); 
                
                // Wait for it to clear out
                waitpid(targetPid, NULL, 0); 

                // Update PCB
                processTable[nextChildIndex].occupied = 0; 
                activeChildren--;
            }
        }

        // Print Process Table every half a simulated second
        long long currentTotalNano = (long long)sysClock[0] * 1000000000LL + sysClock[1];
        long long lastPrintTotalNano = (long long)lastTablePrintSec * 1000000000LL + lastTablePrintNano;
        
        if (currentTotalNano - lastPrintTotalNano >= 500000000LL) {
            linesWritten = printProcessTable(logFP, linesWritten);
            lastTablePrintSec = sysClock[0];
            lastTablePrintNano = sysClock[1];
        }
    }

    // Ending Report 
    printf("\n--- System Simulation Complete ---\n");
    printf("Total processes launched: %d\n", totalLaunched);
    printf("Total messages sent by OSS: %d\n", totalMessagesSent);
    
    fprintf(logFP, "\n--- System Simulation Complete ---\n");
    fprintf(logFP, "Total processes launched: %d\n", totalLaunched);
    fprintf(logFP, "Total messages sent by OSS: %d\n", totalMessagesSent);

    fclose(logFP);

    // Normal termination cleanup
    cleanupAndExit(0);
    return 0;
}