/*
 *
 *  Created on: Mar 8, 2015
 *      Author: jeremy
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <usloss.h>
#include <phase1.h>
#include <assert.h>
#include <libuser.h>
#include <libdisk.h>

#include "tester.h"
#include "phase2Int.h"

static int passed = FALSE;
static int numSectors = 20;
int P3_Startup(void *arg) {
    char buffer[numSectors*USLOSS_DISK_SECTOR_SIZE];
	int i;
	for (i = 0; i < numSectors*USLOSS_DISK_SECTOR_SIZE; i++) buffer[i] = ' ';
	
	for (i = 0; i < numSectors; i++) {
		char temp[USLOSS_DISK_SECTOR_SIZE];
		sprintf(temp, "%d", i);
		strncpy(buffer + USLOSS_DISK_SECTOR_SIZE*i, temp, strlen(temp));
	}
	buffer[numSectors*USLOSS_DISK_SECTOR_SIZE - 1] = 0;
	
    USLOSS_Console("Write to the disk.\n");
    int rc = Sys_DiskWrite(buffer, 0, 0, numSectors, 0);
    USLOSS_Console("Verify that the disk write was successful.\n");
    USLOSS_Console("%d\n", rc);
	assert(rc == P1_SUCCESS);
    USLOSS_Console("Wrote \"%s\".\n", buffer);

	char buffercheck[numSectors*USLOSS_DISK_SECTOR_SIZE];
	strcpy(buffercheck, buffer);
    bzero(buffer, sizeof(buffer));
	USLOSS_Console("buffer [%s]\n", buffer);
    USLOSS_Console("Read from the disk.\n");
    rc = Sys_DiskRead(buffer, 0, 0, numSectors, 0);
    USLOSS_Console("Verify that the disk read was successful.\n");
    assert(rc == P1_SUCCESS);
	USLOSS_Console(buffer);
    TEST(strcmp(buffercheck, buffer), 0);
    USLOSS_Console("Read \"%s\".\n", buffer);
    return 11;
}
int P2_Startup(void *arg)
{
    int rc, waitPid, status, p3Pid;

    P2ClockInit();
    P2DiskInit();
    rc = P2_Spawn("P3_Startup", P3_Startup, NULL, 4*USLOSS_MIN_STACK, 3, &p3Pid);
    TEST(rc, P1_SUCCESS);
    rc = P2_Wait(&waitPid, &status);
    TEST(rc, P1_SUCCESS);
    TEST(waitPid, p3Pid);
    TEST(status, 11);
    P2DiskShutdown();
    P2ClockShutdown();
    USLOSS_Console("You passed all the tests! Treat yourself to a cookie!\n");
    PASSED();
    return 0;
}


void test_setup(int argc, char **argv) {
    int rc;

    rc = Disk_Create(NULL, 0, 10);
    assert(rc == 0);
}

void test_cleanup(int argc, char **argv) {
    DeleteAllDisks();
    if (passed) {
        USLOSS_Console("TEST PASSED.\n");
    }
}
