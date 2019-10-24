#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <usloss.h>
#include <phase1.h>
#include <time.h>
#include "phase2Int.h"


static int      ClockDriver(void *);
static void     SleepStub(USLOSS_Sysargs *sysargs);
static void 	checkIfIsKernel();
// semaphores
int mutex;

void P(int sid) {
	assert(P1_P(sid) == P1_SUCCESS);
}

void V(int sid) {
	assert(P1_V(sid) == P1_SUCCESS);
}

typedef struct p {
    int pid, startTime, duration, isAsleep, isActive;
} Process; 

Process processes[P1_MAXPROC];

/*
 * P2ClockInit
 *
 * Initialize the clock data structures and fork the clock driver.
 */
void 
P2ClockInit(void) 
{
	checkIfIsKernel();
    int rc;

    P2ProcInit();
	rc = P1_SemCreate("mutex", 1, &mutex);
	assert(rc == P1_SUCCESS);
    // initialize data structures here
	for (int i = 0; i < P1_MAXPROC; i++) processes[i].isActive = FALSE;	

    rc = P2_SetSyscallHandler(SYS_SLEEP, SleepStub);
    assert(rc == P1_SUCCESS);

	int pid;
    // fork the clock driver here
	P1EnableInterrupts();
	rc = P1_Fork("clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2, 0, &pid);
	assert(rc == P1_SUCCESS);
}

/*
 * P2ClockShutdown
 *
 * Clean up the clock data structures and stop the clock driver.
 */

void 
P2ClockShutdown(void) 
{
	checkIfIsKernel();
	// stop clock driver 
	int rc = P1_WakeupDevice(USLOSS_CLOCK_DEV, 0, 0, TRUE);
	assert(rc == P1_SUCCESS);	
}

/*
 * ClockDriver
 *
 * Kernel process that manages the clock device and wakes sleeping processes.
 */
static int 
ClockDriver(void *arg) 
{
	USLOSS_Console("inside clockDriver");
    while(1) {
        int rc;
        int now;

        // wait for the next interrupt
        rc = P1_WaitDevice(USLOSS_CLOCK_DEV, 0, &now);
        if (rc == P1_WAIT_ABORTED) {
            break;
        }
        assert(rc == P1_SUCCESS);
		
        // wakeup any sleeping processes whose wakeup time has arrived
		P(mutex);
		for (int i = 0; i < P1_MAXPROC; i++) {
			USLOSS_Console("here\n");
			if (processes[i].isActive) {
				int rc = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &now);
				assert(rc == USLOSS_DEV_OK);
				if (processes[i].duration*1000000 <= now - processes[i].startTime) {
					processes[i].isAsleep = FALSE;
				}
				USLOSS_Console("%d %d\n", now, processes[i].startTime);
			}
		}
		V(mutex);
    }
    return P1_SUCCESS;
}

/*
 * P2_Sleep
 *
 * Causes the current process to sleep for the specified number of seconds.
 */
int 
P2_Sleep(int seconds) 
{
	checkIfIsKernel();
	if (seconds < 0) return P2_INVALID_SECONDS;
	
    // add current process to data structure of sleepers
	int i;
	P(mutex);
	for (i = 0; i < P1_MAXPROC; i++) {
		if (!processes[i].isActive) break;
	}
	assert(i != P1_MAXPROC);
	processes[i].isActive = TRUE;
	processes[i].isAsleep = TRUE;
	int now;
	int rc = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &now);
	assert(rc == USLOSS_DEV_OK);
	processes[i].startTime = now;
	processes[i].duration = seconds;
	V(mutex);
    // wait until sleep is complete
	while(1) {
		P(mutex);
		if (!processes[i].isAsleep) {
			V(mutex);
			break;
		}
		V(mutex);
	}
	P(mutex);
	processes[i].isActive = FALSE;
    V(mutex);
	return P1_SUCCESS;
}

/*
 * SleepStub
 *
 * Stub for the Sys_Sleep system call.
 */
static void 
SleepStub(USLOSS_Sysargs *sysargs) 
{
    int seconds = (int) sysargs->arg1;
    int rc = P2_Sleep(seconds);
    sysargs->arg4 = (void *) rc;
}

/*
 * Checks psr to make sure OS is in kernel mode, halting USLOSS if not. Mode bit
 * is the LSB.
 */
void checkIfIsKernel(){ 
    if ((USLOSS_PsrGet() & 1) != 1) {
        USLOSS_Console("The OS must be in kernel mode!\n");
        USLOSS_IllegalInstruction();
    }
}
