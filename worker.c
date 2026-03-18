#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>

// Message buffer structure - MUST match the one in oss.c exactly
typedef struct {
    long mtype;     // Message type 
    int status;     // 1 to continue, 0 to terminate
} msgbuffer;

int main(int argc, char* argv[]) {
    // Verify command line arguments
    // Worker takes in two command line arguments: seconds and nanoseconds 
    if (argc != 3) {
        fprintf(stderr, "Usage: ./worker <seconds> <nanoseconds>\n");
        return 1;
    }

    int durationSec = atoi(argv[1]);
    int durationNano = atoi(argv[2]);

    // Attach to Shared Memory (Clock)
    // Keys must match exactly what was used in oss.c 
    key_t shmKey = ftok("oss.c", 1);
    int shmid = shmget(shmKey, 2 * sizeof(int), 0666);
    if (shmid == -1) {
        perror("Worker: Failed to access shared memory");
        return 1;
    }
    
    // Use volatile to prevent the compiler from caching the clock values 
    volatile int* sysClock = (volatile int*)shmat(shmid, NULL, 0);

    // Attach to Message Queue
    key_t msgKey = ftok("oss.c", 2);
    int msqid = msgget(msgKey, 0666);
    if (msqid == -1) {
        perror("Worker: Failed to access message queue");
        return 1;
    }

    // Calculate Termination Time
    // Add the system clock time to the time passed via command line 
    int termSec = sysClock[0] + durationSec;
    int termNano = sysClock[1] + durationNano;

    // Handle nanosecond overflow (if nanoseconds exceed 1 second)
    if (termNano >= 1000000000) {
        termSec += termNano / 1000000000;
        termNano = termNano % 1000000000;
    }

    pid_t myPid = getpid();
    pid_t myPpid = getppid();
    int messagesReceived = 0;

    // Output starting information 
    printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
           myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano);
    printf("--Just Starting\n");

    msgbuffer buf;
    int done = 0;

    // Main Execution Loop
    do {
        // Wait for a message from oss specifically meant for this process 
        if (msgrcv(msqid, &buf, sizeof(msgbuffer) - sizeof(long), myPid, 0) == -1) {
            perror("Worker: Failed to receive message");
            break;
        }

        messagesReceived++;

        // Check the clock to determine if it is time to terminate
        if ((sysClock[0] > termSec) || (sysClock[0] == termSec && sysClock[1] >= termNano)) { 
            done = 1;
        }

        // Output loop iteration information ONLY if we aren't terminating this cycle
        if (!done) {
            printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
                   myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano);
            
            // Format check for singular vs plural grammar based on rubric 
            if (messagesReceived == 1) {
                printf("--1 message received from oss\n");
            } else {
                printf("--%d messages received from oss\n", messagesReceived);
            }
        }

        // Prepare response to oss
        buf.mtype = myPpid;     // Send back to parent 
        buf.status = done ? 0 : 1; // 0 if done, 1 if still running

        // Send message to oss 
        if (msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
            perror("Worker: Failed to send message");
            break;
        }

    } while (!done);

    // Termination sequence
    // Output final termination information 
    printf("WORKER PID:%d PPID:%d SysClocks: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
           myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano);
    printf("--Terminating after sending message back to oss after %d received messages.\n", messagesReceived);

    // Detach from shared memory before exiting (cast to void* to prevent compiler warnings)
    shmdt((void *)sysClock);

    return 0;
}