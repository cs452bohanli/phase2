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
    rc = P2_Wait(&waitPid, &status);
    TEST(rc, P1_SUCCESS);
    TEST(status, 11);
    TEST(waitPid, p3Pid);
    PASSED();
    return 0;
}

/*
 * Child
 *
 * Checks its pid and those of its ancestors.
 * Tests Sys_GetPID.
 */

int Child(void *arg) {
    int pid;

    Sys_GetPID(&pid);
    TEST(pid, childPid);
    CheckName("Child", childPid);
    CheckName("P2_Startup", p2Pid);
    CheckName("P3_Startup", p3Pid);
    return (int) arg;
}

/*
 * P3_Startup
 *
 * Initial user-level process. 
 * Tests Sys_GetTimeOfDay, Sys_Spawn, Sys_Wait, and Sys_Terminate.
 */

int P3_Startup(void *arg) {
    int rc, waitPid, status;
    int start, finish;

    Sys_GetTimeOfDay(&start);

    rc = Sys_Spawn("Child", Child, (void *) 42, USLOSS_MIN_STACK, 3, &childPid);
    TEST(rc, P1_SUCCESS);

    rc = Sys_Wait(&waitPid, &status);
    TEST(rc, P1_SUCCESS);
    TEST(status, 42);
    TEST(waitPid, childPid);
    Sys_GetTimeOfDay(&finish);
    TEST(finish > start, 1);
    Sys_Terminate(11);
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
