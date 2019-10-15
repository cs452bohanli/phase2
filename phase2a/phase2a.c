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
} UserProcess;

static UserProcess processes[P1_MAXPROC];

void checkIfIsKernel();
int isValidSys(int number);

static void SpawnStub(USLOSS_Sysargs *sysargs);
void waitStub(USLOSS_Sysargs *sysargs);
void terminateStub(USLOSS_Sysargs *sysargs);

/*
    Returns the pid of the user process given by the kernel pid, -1 if not found.
*/
static int getUserProcess(int kernelPid) {
    int i;
    for (i = 0; i < P1_MAXPROC; i++) {
        if (processes[i].state != UNINITIALIZED && processes[i].kernelPid == kernelPid) return i;
    }
    return -1;
}

/*
 * Helper function to call func passed to P1_Fork with its arg.
 */
static int launch(void *arg)
{
    int currentUserProcess = getUserProcess(P1_GetPid());
    assert(currentUserProcess != -1);
    int status = processes[currentUserProcess].startFunc(processes[currentUserProcess].startArg);
    P2_Terminate(status);
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
    if (!isValidSys(args -> number)) USLOSS_IllegalInstruction();
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
    *pid = i;
    rc = P1_Fork(name, launch, arg, stackSize, priority, TAG_USER, &(processes[i].kernelPid));
    if (rc != P1_SUCCESS) processes[i].state = UNINITIALIZED;
    
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
    int currentUserProcess = getUserProcess(P1_GetPid());
    assert(currentUserProcess != -1);
    int rc = P1_Join(TAG_USER, pid, status);
    if (rc != P1_SUCCESS) return rc;

    *pid = getUserProcess(*pid);
    assert(*pid != -1);
    assert(processes[*pid].state == TERMINATED);
    processes[*pid].state = UNINITIALIZED;
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
    int currentUserProcess = getUserProcess(P1_GetPid());
    assert(currentUserProcess != -1);
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

void waitStub(USLOSS_Sysargs *sysargs) {
    checkIfIsKernel();
    int pid = 0, status = 0, rc;
    rc = P2_Wait(&pid, &status);
    sysargs -> arg1 = (void*) pid;
    sysargs -> arg2 = (void*) status;
    sysargs -> arg4 = (void*) rc;
}

void terminateStub(USLOSS_Sysargs *sysargs) {
    checkIfIsKernel();
    P2_Terminate((int) sysargs -> arg1);
}

/*
    Checks to see if a sys number is valid.
*/
void isValidSys(unsigned int number) {
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
        USLOSS_Console("The OS must be in kernel mode!");
        USLOSS_IllegalInstruction();
    }
}




