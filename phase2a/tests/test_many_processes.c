/*
 * test_basic.c
 *
 * Tests basic functionality of all system calls.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>
#include <assert.h>
#include <libuser.h>

#include "tester.h"
#include "phase2Int.h"

static int passed = TRUE;

static int childPid, p2Pid, p3Pid;

/*
 * CheckName
 *
 * Verifies that the process with the specified pid has the specified name.
 * Tests Sys_GetProcInfo.
 */

static void CheckName(char *name, int pid) 
{
    P1_ProcInfo info;

    int rc = Sys_GetProcInfo(pid, &info);
    TEST(rc, P1_SUCCESS);
    TEST(strcmp(info.name, name), 0);
}

/*
 * P2_Startup
 *
 * Entry point for this test. Creates the user-level process P3_Startup.
 * Tests P2_Spawn and P2_Wait.
 */

int P2_Startup(void *arg)
{
    int rc, waitPid, status;

    P2ProcInit();
    p2Pid = P1_GetPid();
    rc = P2_Spawn("P3_Startup", P3_Startup, NULL, 4*USLOSS_MIN_STACK, 3, &p3Pid);
    TEST(rc, P1_SUCCESS);
    PASSED();
    return 0;
}

int Child(void *arg) {
    return (int) arg;
}

/*
 * P3_Startup
 *
 * Initial user-level process. 
 */

int P3_Startup(void *arg) {
    int rc, waitPid, status;
    int start, finish;
    int numProcesses = P1_MAXPROC - 3;
    char name[P1_MAXNAME];

    int i;
    for (i = 0; i < numProcesses; i++) {
        sprintf(name, "%d", i);
        rc = Sys_Spawn(name, Child, (void *) 42, USLOSS_MIN_STACK, 5, &childPid);
        TEST(rc, P1_SUCCESS);
    }
    rc = Sys_Spawn("!", Child, (void *) 42, USLOSS_MIN_STACK, 5, &childPid);
    TEST(rc, P1_TOO_MANY_PROCESSES);
    int waitNum = 10;
    for (i = 0; i < waitNum; i++) {
        rc = Sys_Wait(&waitPid, &status);
        TEST(rc, P1_SUCCESS);
    }

    for (i = 0; i < waitNum; i++) {
        sprintf(name, "%d!", i);
        rc = Sys_Spawn(name, Child, (void *) 42, USLOSS_MIN_STACK, 1, &childPid);
        TEST(rc, P1_SUCCESS);
    }
    rc = Sys_Wait(&waitPid, &status);
    TEST(rc, P1_SUCCESS);
    rc = Sys_Spawn("!!", Child, (void *) 42, USLOSS_MIN_STACK, 1, &childPid);
    TEST(rc, P1_SUCCESS);
    Sys_Terminate(11);
	assert(0);
    // does not get here
    return 0;
}

void test_setup(int argc, char **argv) {
    // Do nothing.
}

void test_cleanup(int argc, char **argv) {
    if (passed) {
        USLOSS_Console("TEST PASSED.\n");
    }
}
