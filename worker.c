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
    int status;     // 1 to continue, 0 to terminate [cite: 124]
} msgbuffer;

int main(int argc, char* argv[]) {
    // 1. Verify command line arguments
    // Worker expects two arguments: seconds and nanoseconds [cite: 42]
    if (argc != 3) {
        fprintf(stderr, "Usage: ./worker <seconds> <nanoseconds>\n");
        return 1;
    }

    int durationSec = atoi(argv[1]);
    int durationNano = atoi(argv[2]);

    // 2. Attach to Shared Memory (Clock)
    // Keys must match exactly what was used in oss.c
    key_t shmKey = ftok("oss.c", 1);
    int shmid = shmget(shmKey, 2 * sizeof(int), 0666);
    if (shmid == -1) {
        perror("Worker: Failed to access shared memory");
        return 1;
    }
    int* sysClock = (int*)shmat(shmid, NULL, 0); [cite: 45]

    // 3. Attach to Message Queue
    key_t msgKey = ftok("oss.c", 2);
    int msqid = msgget(msgKey, 0666);
    if (msqid == -1) {
        perror("Worker: Failed to access message queue");
        return 1;
    }

    // 4. Calculate Termination Time
    // Add the system clock time to the time passed via command line [cite: 52]
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

    // Output starting information [cite: 57, 58, 59]
    printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
           myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano);
    printf("--Just Starting\n");

    msgbuffer buf;
    int done = 0;

    // 5. Main Execution Loop
    do { [cite: 46]
        // Wait for a message from oss specifically meant for this process [cite: 47, 60]
        if (msgrcv(msqid, &buf, sizeof(msgbuffer) - sizeof(long), myPid, 0) == -1) {
            perror("Worker: Failed to receive message");
            break;
        }

        messagesReceived++;

        // Output loop iteration information [cite: 61, 62]
        printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
               myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano); [cite: 63]
        printf("--%d messages received from oss\n", messagesReceived); [cite: 64, 67]

        // Check the clock to determine if it is time to terminate [cite: 48, 49, 53]
        if ((sysClock[0] > termSec) || (sysClock[0] == termSec && sysClock[1] >= termNano)) { [cite: 56]
            done = 1;
        }

        // Prepare response to oss
        buf.mtype = myPpid;     // Send back to parent [cite: 1470]
        buf.status = done ? 0 : 1; // 0 if done, 1 if still running [cite: 124]

        // Send message to oss [cite: 50, 60]
        if (msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
            perror("Worker: Failed to send message");
            break;
        }

    } while (!done); [cite: 51]

    // 6. Termination sequence
    // Output final termination information [cite: 56]
    printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", 
           myPid, myPpid, sysClock[0], sysClock[1], termSec, termNano); [cite: 69]
    printf("--Terminating after sending message back to oss after %d received messages.\n", messagesReceived); [cite: 70]

    // Detach from shared memory before exiting
    shmdt(sysClock);

    return 0;
}