
/*
 * test_sleep.c
 */
#include <assert.h>
#include <usloss.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libuser.h>

#include "tester.h"
#include "phase2Int.h"

#define NUM_SLEEPERS 10

static int passed = TRUE;

int Slept(int start, int end) {
    return (end - start) / 1000000;
}

/*
 * Sleeper
 *
 * Sleeps for the number of seconds in arg and checks that it slept long enough.
 *
 */

int Sleeper(void *arg) {
    int start, end, rc;
    int seconds = (int) arg;
    Sys_GetTimeOfDay(&start);
    rc = Sys_Sleep(seconds);
    assert(rc == 0);
    Sys_GetTimeOfDay(&end);
    TEST(Slept(start, end) >= seconds, 1);
	USLOSS_Console("%d %d %d\n", seconds, start, end);
    return 0;
}

/*
 * P3_Startup
 *
 * Creates NUM_SLEEPERS children who sleep for random amounts of time.
 *
 */
int
P3_Startup(void *arg)
{
    int status, rc;
    int pid = -1;

    for (int i = 0; i < NUM_SLEEPERS; i++) {
        int duration = random() % 10;
        rc = Sys_Spawn(MakeName("Sleeper", i), Sleeper, (void *) duration, USLOSS_MIN_STACK, 5, &pid);
        TEST(rc, P1_SUCCESS);
    }

    for (int i = 0; i < NUM_SLEEPERS; i++) {
        rc = Sys_Wait(&pid, &status);
        TEST(rc, P1_SUCCESS);
    }
    return 11;
}

int P2_Startup(void *arg)
{
    int rc, waitPid, status, p3Pid;

    P2ClockInit();
    rc = P2_Spawn("P3_Startup", P3_Startup, NULL, 4*USLOSS_MIN_STACK, 3, &p3Pid);
    TEST(rc, P1_SUCCESS);
    rc = P2_Wait(&waitPid, &status);
    TEST(rc, P1_SUCCESS);
    TEST(waitPid, p3Pid);
    TEST(status, 11);
    P2ClockShutdown();
    PASSED();
    return 0;
}

void test_setup(int argc, char **argv) {
    // Do nothing.
}

void test_cleanup(int argc, char **argv) {
    if (passed) {
        USLOSS_Console("TEST PASSED.\n");
    }
    P2ClockShutdown();
}

