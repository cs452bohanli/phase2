#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <usloss.h>
#include <phase1.h>

#include "phase2Int.h"


static int      DiskDriver(void *);
static void     DiskReadStub(USLOSS_Sysargs *sysargs);
void moveTrack(int track, int unit);
void readValueAt(int sector, int unit, void *buffer);

int NUM_TRACKS[USLOSS_DISK_UNITS];

/*
 * P2DiskInit
 *
 * Initialize the disk data structures and fork the disk drivers.
 */
void 
P2DiskInit(void) 
{
    int rc;

    // initialize data structures here
	int i;
	USLOSS_DeviceRequest request;
	for (i = 0; i < USLOSS_DISK_UNITS; i++) {
		request.opr = USLOSS_DISK_TRACKS;
		request.reg1 = (void*) &(NUM_TRACKS[i]);
		rc = USLOSS_DeviceOutput(USLOSS_DISK_DEV, i, &request);
	}
    // install system call stubs here

    rc = P2_SetSyscallHandler(SYS_DISKREAD, DiskReadStub);
    assert(rc == P1_SUCCESS);

    // fork the disk drivers here
	int pid;
	rc = P1_Fork("disk driver", DiskDriver, NULL, USLOSS_MIN_STACK, 2, 0, &pid);
	assert(rc == P1_SUCCESS);
}

int shutdown = FALSE;
/*
 * P2DiskShutdown
 *
 * Clean up the disk data structures and stop the disk drivers.
 */

void 
P2DiskShutdown(void) 
{
	shutdown = TRUE;
}

/*
 * DiskDriver
 *
 * Kernel process that manages a disk device and services disk I/O requests from other processes.
 * Note that it may require several disk operations to service a single I/O request.
 */
static int 
DiskDriver(void *arg) 
{
	//int rc;
    while (!shutdown) {// repeat
    	//   wait for next request
		
    	//   while request isn't complete
    	//          send appropriate operation to disk (USLOSS_DeviceOutput)
    	//          wait for operation to finish (P1_WaitDevice)
    	//          handle errors
    	//   update the request status and wake the waiting process
    	// until P2DiskShutdown has been called
	}
    return P1_SUCCESS;
}

/*
 * P2_DiskRead
 *
 * Reads the specified number of sectors from the disk starting at the specified track and sector.
 */
int 
P2_DiskRead(int unit, int track, int first, int sectors, void *buffer) 
{
	if (unit < 0 || unit >= USLOSS_DISK_UNITS) return P1_INVALID_UNIT;
	if (track < 0 || track >= NUM_TRACKS[unit]) return P2_INVALID_TRACK;
	if (first < 0 || first >= USLOSS_DISK_TRACK_SIZE) return P2_INVALID_FIRST;
	if (buffer == NULL) return P2_NULL_ADDRESS;
	
	int currentTrack = track;
	moveTrack(track, unit);
	int i, index = first;
	for (i = 0; i < sectors; i++) {
		if (index == USLOSS_DISK_TRACK_SIZE) {
			if ((++currentTrack) >= NUM_TRACKS[unit]) return P2_INVALID_SECTORS;
			moveTrack(currentTrack, unit);
			index = 0;
		}
		readValueAt(index, unit, buffer + i*USLOSS_DISK_SECTOR_SIZE);
		index++;
	}
    // give request to the proper device driver
    // wait until device driver completes the request
    return P1_SUCCESS;
}

/*
 * DiskReadStub
 *
 * Stub for the Sys_DiskRead system call.
 */
static void 
DiskReadStub(USLOSS_Sysargs *sysargs) 
{
	void *buffer = sysargs->arg1;
	int sectors = (int) sysargs->arg2;
	int track = (int) sysargs->arg3;
	int first = (int) sysargs->arg4;
	int unit = (int) sysargs->arg5;
	
    // call P2_DiskRead
    // put result in sysargs
	sysargs->arg4 = (void*) P2_DiskRead(unit, track, first, sectors, buffer);
}

void moveTrack(int track, int unit) {
	int rc;
	USLOSS_DeviceRequest request;
	request.opr = USLOSS_DISK_SEEK;
	request.reg1 = (void*) track;
	rc = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
	assert(rc == USLOSS_DEV_OK);
}

void readValueAt(int sector, int unit, void *buffer) {
	int rc;
	USLOSS_DeviceRequest request;
	request.opr = USLOSS_DISK_READ;
	request.reg1 = (void*) sector;
	request.reg2 = buffer;
	rc = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
	assert(rc == USLOSS_DEV_OK);
}

