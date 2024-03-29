#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <stdio.h>
#include <assert.h>
#include <libuser.h>
#include <usyscall.h>

#include "phase2Int.h"

#define TAG_KERNEL 0
#define TAG_USER 1

#define UNINITIALIZED 0
#define INITIALIZED 1
#define TERMINATED 2

void (*handlers[USLOSS_MAX_SYSCALLS])(USLOSS_Sysargs *args);

typedef struct up {
    int kernelPid, state;
    int (*startFunc)(void *);
    void *startArg;
    int isOrphan;
	int status;
} UserProcess;

static UserProcess processes[P1_MAXPROC];
// semaphores
static int mutex;

void checkIfIsKernel();
int isValidSys(unsigned int number);

static void SpawnStub(USLOSS_Sysargs *sysargs);
void waitStub(USLOSS_Sysargs *sysargs);
void terminateStub(USLOSS_Sysargs *sysargs);
void getProcInfoStub(USLOSS_Sysargs*);
void getPidStub(USLOSS_Sysargs*);
void getTimeOfDayStub(USLOSS_Sysargs*);

int setOsMode(int mode) {
    assert(mode == 0 || mode == 1);
    return USLOSS_PsrSet((USLOSS_PsrGet() & (~1)) | mode);
}
/*
    Returns the pid of the user process given by the kernel pid, -1 if not found.
*/
static int getUserProcess(int kernelPid) {
    int i;
	int retval = -1;
    for (i = 0; i < P1_MAXPROC; i++) {
        if (processes[i].state != UNINITIALIZED && processes[i].kernelPid == kernelPid) {
			retval = i;
			break;
		}
    }
    return retval;
}

/*
 * Helper function to call func passed to P1_Fork with its arg.
 */
static int launch(void *arg)
{
	P1_P(mutex);
    int currentUserProcess = getUserProcess(P1_GetPid());
	assert(currentUserProcess != -1);
	int (*startFunc)(void *) = processes[currentUserProcess].startFunc;
    void *startArg = processes[currentUserProcess].startArg;
	P1_V(mutex);
	assert(setOsMode(0) == USLOSS_DEV_OK);

    int status = startFunc(startArg);
	Sys_Terminate(status);
    return status;
}

/*
 * IllegalHandler
 *
 * Handler for illegal instruction interrupts.
 *
 */

static void 
IllegalHandler(int type, void *arg) 
{
    P1_ProcInfo info;
    assert(type == USLOSS_ILLEGAL_INT);

    int pid = P1_GetPid();
    int rc = P1_GetProcInfo(pid, &info);
    assert(rc == P1_SUCCESS);
    if (info.tag == TAG_KERNEL) {
        P1_Quit(1024);
    } else {
        P2_Terminate(2048);
    }
}

/*
 * SyscallHandler
 *
 * Handler for system call interrupts.
 *
 */

static void 
SyscallHandler(int type, void *arg) 
{
    USLOSS_Sysargs *args = (USLOSS_Sysargs *) arg;
    // not sure if right way to handle errors
    if (!isValidSys(args -> number)) {
		USLOSS_Console("probably shouldn't be here\n");
		USLOSS_IllegalInstruction();
	}
    handlers[args -> number](args);
}


/*
 * P2ProcInit
 *
 * Initialize everything.
 *
 */

void
P2ProcInit(void) 
{
    checkIfIsKernel();
	P1_SemCreate("mutex", 1, &mutex);
    int rc, i;

    for (i = 0; i < P1_MAXPROC; i++) {
        processes[i].state = FALSE;
    }
	
    USLOSS_IntVec[USLOSS_ILLEGAL_INT] = IllegalHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = SyscallHandler;

    // call P2_SetSyscallHandler to set handlers for all system calls
    rc = P2_SetSyscallHandler(SYS_SPAWN, SpawnStub);
    assert(rc == P1_SUCCESS);
    rc = P2_SetSyscallHandler(SYS_WAIT, waitStub);
    assert(rc == P1_SUCCESS);
    rc = P2_SetSyscallHandler(SYS_TERMINATE, terminateStub);
    assert(rc == P1_SUCCESS);
    rc = P2_SetSyscallHandler(SYS_GETPROCINFO, getProcInfoStub);
    assert(rc == P1_SUCCESS);
    rc = P2_SetSyscallHandler(SYS_GETPID, getPidStub);
    assert(rc == P1_SUCCESS);
    rc = P2_SetSyscallHandler(SYS_GETTIMEOFDAY, getTimeOfDayStub);
    assert(rc == P1_SUCCESS);
}

/*
 * P2_SetSyscallHandler
 *
 * Set the system call handler for the specified system call.
 *
 */

int
P2_SetSyscallHandler(unsigned int number, void (*handler)(USLOSS_Sysargs *args))
{
    checkIfIsKernel();
    if (!isValidSys(number)) return P2_INVALID_SYSCALL;
    
    handlers[number] = handler;
    return P1_SUCCESS;
}

/*
 * P2_Spawn
 *
 * Spawn a user-level process.
 *
 */
int 
P2_Spawn(char *name, int(*func)(void *arg), void *arg, int stackSize, int priority, int *pid) 
{
    checkIfIsKernel();
	assert(P1_P(mutex) == P1_SUCCESS);
    int rc, i;
    for (i = 0; i < P1_MAXPROC; i++) {
        if (processes[i].state == UNINITIALIZED) {
            processes[i].state = INITIALIZED;
            processes[i].startFunc = func;
            processes[i].startArg = arg;
            processes[i].isOrphan = FALSE;
            break;
        }
    }
    if (i == P1_MAXPROC) return P1_TOO_MANY_PROCESSES;
    assert(P1_V(mutex) == P1_SUCCESS);
    rc = P1_Fork(name, launch, arg, stackSize, priority, TAG_USER, &(processes[i].kernelPid));
    P1_P(mutex);
	if (rc != P1_SUCCESS) processes[i].state = UNINITIALIZED;
    *pid = processes[i].kernelPid;
	P1_V(mutex);
    return rc;
}

/*
 * P2_Wait
 *
 * Wait for a user-level process.
 *
 */

int 
P2_Wait(int *pid, int *status) 
{
    checkIfIsKernel();

    int rc = P1_Join(TAG_USER, pid, status);
	
    if (rc != P1_SUCCESS) return rc;

	P1_P(mutex);
    int userPid = getUserProcess(*pid);
    assert(userPid != -1);
    assert(processes[userPid].state == TERMINATED);
	*status = processes[userPid].status;
    processes[userPid].state = UNINITIALIZED;
	P1_V(mutex);
    return P1_SUCCESS;
}

/*
 * P2_Terminate
 *
 * Terminate a user-level process.
 *
 */

void 
P2_Terminate(int status) 
{
    checkIfIsKernel();
	P1_P(mutex);

    int currentUserProcess = getUserProcess(P1_GetPid());
    assert(currentUserProcess != -1);
	processes[currentUserProcess].status = status;
    processes[currentUserProcess].state = processes[currentUserProcess].isOrphan ? UNINITIALIZED : TERMINATED;
    // set all children to orphans
    P1_ProcInfo info;
    int rc = P1_GetProcInfo(P1_GetPid(), &info);

    assert(rc == P1_SUCCESS);
    int i;
    for (i = 0; i < info.numChildren; i++) {
        int userChild = getUserProcess(info.children[i]);
        if (userChild != -1) {
            processes[userChild].isOrphan = TRUE;
            if (processes[userChild].state == TERMINATED) processes[userChild].state = UNINITIALIZED;
		}
    }
	P1_V(mutex);

	P1_Quit(status);
}

/*
 * SpawnStub
 *
 * Stub for Sys_Spawn system call. 
 *
 */

static void 
SpawnStub(USLOSS_Sysargs *sysargs) 
{
    checkIfIsKernel();
    int (*func)(void *) = sysargs->arg1;
    void *arg = sysargs->arg2;
    int stackSize = (int) sysargs->arg3;
    int priority = (int) sysargs->arg4;
    char *name = sysargs->arg5;
    int pid;
    int rc = P2_Spawn(name, func, arg, stackSize, priority, &pid);
    if (rc == P1_SUCCESS) {
        sysargs->arg1 = (void *) pid;
    }
    sysargs->arg4 = (void *) rc;
}

/*
	Stub for Sys_Wait system call.
*/
void waitStub(USLOSS_Sysargs *sysargs) {
    checkIfIsKernel();
    int pid = 0, status = 0, rc;
    rc = P2_Wait(&pid, &status);
    sysargs -> arg1 = (void*) pid;
    sysargs -> arg2 = (void*) status;
    sysargs -> arg4 = (void*) rc;
}

/*
	Stub for Sys_Terminate system call.
*/
void terminateStub(USLOSS_Sysargs *sysargs) {
    checkIfIsKernel();
    P2_Terminate((int) sysargs -> arg1);
}

/*
	Stub for Sys_GetProcInfo system call.
*/
void getProcInfoStub(USLOSS_Sysargs *sysargs) {
	checkIfIsKernel();
	int rc;
	int pid = (int) sysargs->arg1;
	P1_ProcInfo *info = sysargs->arg2;
	rc = P1_GetProcInfo(pid, info);
	sysargs->arg4 = (void*) rc;
}

/*
	Stub for Sys_GetPid system call;
*/
void getPidStub(USLOSS_Sysargs *sysargs) {
	checkIfIsKernel();
	int pid = P1_GetPid();
	sysargs->arg1 = (void*) pid;
}

/*
	Stub for Sys_GetTimeOfDay system call.
*/
void getTimeOfDayStub(USLOSS_Sysargs *sysargs){
	checkIfIsKernel();
	int status;
	int rc = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status);
	sysargs->arg1 = (void*) status;
}

/*
    Checks to see if a sys number is valid.
*/
int isValidSys(unsigned int number) {
    if (number > USLOSS_MAX_SYSCALLS) return FALSE;
    if (!(number >= 3 && number <= 5) && !(number >= 20 && number <= 22)) return FALSE;
    return TRUE;
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




