#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <usloss.h>
#include <phase1.h>

#include "phase2Int.h"
#define QUEUE_SIZE (P1_MAXSEM/3)

static int      DiskDriver(void *);
static void     DiskReadStub(USLOSS_Sysargs *sysargs);
static void	    DiskWriteStub(USLOSS_Sysargs *sysargs);
static void     DiskSizeStub(USLOSS_Sysargs *sysargs);
void checkIfIsKernel();
void moveTrack(int track, int unit);
void completeReadWriteAt(int type, int sector, int unit, void *buffer);

int NUM_TRACKS[USLOSS_DISK_UNITS];

typedef struct r {
	int type, track, first, sectors, succeeded;
	void *buffer;
} Request;

Request queue[USLOSS_DISK_UNITS][QUEUE_SIZE];
int queueStarts[USLOSS_DISK_UNITS];
int queueEnds[USLOSS_DISK_UNITS];

// semaphores
int requestFinished[USLOSS_DISK_UNITS][QUEUE_SIZE];
int requestSent[USLOSS_DISK_UNITS];
int mutex[USLOSS_DISK_UNITS];

// helper functions for semaphores, makes code cleaner
void P(int sid) {
	assert(P1_P(sid) == P1_SUCCESS);
}

void V(int sid) {
	assert(P1_V(sid) == P1_SUCCESS);
}

int numDisks = 0;

/*
 * P2DiskInit
 *
 * Initialize the disk data structures and fork the disk drivers.
 */
void 
P2DiskInit(void) 
{
    checkIfIsKernel(); //added
	int rc;
    // initialize data structures here
	int i, j, status;
	USLOSS_DeviceRequest request;
	char name[10];
	for (i = 0; i < USLOSS_DISK_UNITS; i++) {
		request.opr = USLOSS_DISK_TRACKS;
		request.reg1 = (void*) &(NUM_TRACKS[i]);
		rc = USLOSS_DeviceOutput(USLOSS_DISK_DEV, i, &request);		
		if (rc == USLOSS_DEV_OK) numDisks++;
		else break;

		rc = P1_WaitDevice(USLOSS_DISK_DEV, i, &status);
		assert(rc == P1_SUCCESS);

		queueStarts[i] = 0;
		queueEnds[i] = 0;
		sprintf(name, "%d.", i);
		rc = P1_SemCreate(name, 0, &(requestSent[i]));
		assert(rc == P1_SUCCESS);
		sprintf(name, ".%d", i);
		rc = P1_SemCreate(name, 1, &(mutex[i]));
		assert(rc == P1_SUCCESS);
		for (j = 0; j < QUEUE_SIZE; j++) {
			sprintf(name, "%d", j);
			rc = P1_SemCreate(name, 0, &(requestFinished[i][j]));
			assert(rc == P1_SUCCESS);
		}
	}

    // install system call stubs here

    rc = P2_SetSyscallHandler(SYS_DISKREAD, DiskReadStub);
    assert(rc == P1_SUCCESS);
	rc = P2_SetSyscallHandler(SYS_DISKWRITE, DiskWriteStub);
	assert(rc == P1_SUCCESS);
	rc = P2_SetSyscallHandler(SYS_DISKSIZE, DiskSizeStub); //added
    assert(rc == P1_SUCCESS);

    // fork the disk drivers here
	int pid;
	for (i = 0; i < numDisks; i++ ) {
		rc = USLOSS_PsrSet(USLOSS_PsrGet() | (1 << 1)); // set 2nd but of the psr to 1
		assert(rc == USLOSS_DEV_OK); //added interrupt enable
		rc = P1_Fork("disk driver", DiskDriver, (void*) i, USLOSS_MIN_STACK, 2, 0, &pid);
		assert(rc == P1_SUCCESS);
	}
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
	checkIfIsKernel(); //added
	shutdown = TRUE;
	int i;
	for (i = 0; i < numDisks; i++) {
		V(requestSent[i]);
	}
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
	int driverNum = (int) arg;
    while (!shutdown) {// repeat
    	//   wait for next request
		P(requestSent[driverNum]);
		if (shutdown) break;	
		Request request = queue[driverNum][queueStarts[driverNum]];
	 	
		int currentTrack = request.track;
		moveTrack(request.track, driverNum);
		int i, index = request.first;
		queue[driverNum][queueStarts[driverNum]].succeeded = TRUE;
		for (i = 0; i < request.sectors; i++) {	
			if (index == USLOSS_DISK_TRACK_SIZE) {
				if ((++currentTrack) >= NUM_TRACKS[driverNum]) {
					queue[driverNum][queueStarts[driverNum]].succeeded = FALSE;	
					break;
				}
				moveTrack(currentTrack, driverNum);
				index = 0;
			}
			completeReadWriteAt(request.type, index, driverNum, request.buffer + i*USLOSS_DISK_SECTOR_SIZE);
			index++;
		}
		
		V(requestFinished[driverNum][queueStarts[driverNum]]);
		queueStarts[driverNum] = (queueStarts[driverNum] + 1) % QUEUE_SIZE;

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
 * P2_DiskWrite
 *
 * Writes the specified number of sectors from the disk starting at the specified track and sector.
 */
int 
P2_DiskWrite(int unit, int track, int first, int sectors, void *buffer) 
{
	checkIfIsKernel(); //added
	if (unit < 0 || unit >= numDisks) return P1_INVALID_UNIT;
	if (track < 0 || track >= NUM_TRACKS[unit]) return P2_INVALID_TRACK;
	if (first < 0 || first >= USLOSS_DISK_TRACK_SIZE) return P2_INVALID_FIRST;
	if (buffer == NULL) return P2_NULL_ADDRESS;
	
    // give request to the proper device driver
	P(mutex[unit]);
	int requestIndex = queueEnds[unit];
	queue[unit][requestIndex].type = USLOSS_DISK_WRITE;
	queue[unit][requestIndex].track = track;
	queue[unit][requestIndex].first = first;
	queue[unit][requestIndex].sectors = sectors;
	queue[unit][requestIndex].buffer = buffer;
	queueEnds[unit] = (queueEnds[unit] + 1) % QUEUE_SIZE;
	V(mutex[unit]);
	V(requestSent[unit]);
	// wait until device driver completes the request
	P(requestFinished[unit][requestIndex]);
    return queue[unit][requestIndex].succeeded ? P1_SUCCESS : P2_INVALID_SECTORS;
}

/*
 * DiskWriteStub
 *
 * Stub for the Sys_DiskRead system call.
 */
static void 
DiskWriteStub(USLOSS_Sysargs *sysargs) 
{
	checkIfIsKernel(); //added
	void *buffer = sysargs->arg1;
	int sectors = (int) sysargs->arg2;
	int track = (int) sysargs->arg3;
	int first = (int) sysargs->arg4;
	int unit = (int) sysargs->arg5;
	
    // call P2_DiskWrite
    // put result in sysargs
	sysargs->arg4 = (void*) P2_DiskWrite(unit, track, first, sectors, buffer);
}


/*
 * P2_DiskRead
 *
 * Reads the specified number of sectors from the disk starting at the specified track and sector.
 */
int 
P2_DiskRead(int unit, int track, int first, int sectors, void *buffer) 
{
	checkIfIsKernel(); //added
	if (unit < 0 || unit >= numDisks) return P1_INVALID_UNIT;
	if (track < 0 || track >= NUM_TRACKS[unit]) return P2_INVALID_TRACK;
	if (first < 0 || first >= USLOSS_DISK_TRACK_SIZE) return P2_INVALID_FIRST;
	if (buffer == NULL) return P2_NULL_ADDRESS;
	
    // give request to the proper device driver
	P(mutex[unit]);
	int requestIndex = queueEnds[unit];
	queue[unit][requestIndex].type = USLOSS_DISK_READ;
	queue[unit][requestIndex].track = track;
	queue[unit][requestIndex].first = first;
	queue[unit][requestIndex].sectors = sectors;
	queue[unit][requestIndex].buffer = buffer;
	queueEnds[unit] = (queueEnds[unit] + 1) % QUEUE_SIZE;
	V(mutex[unit]);
	V(requestSent[unit]);
	// wait until device driver completes the request
	P(requestFinished[unit][requestIndex]);
    return queue[unit][requestIndex].succeeded ? P1_SUCCESS : P2_INVALID_SECTORS;
}

/*
 * DiskReadStub
 *
 * Stub for the Sys_DiskRead system call.
 */
static void 
DiskReadStub(USLOSS_Sysargs *sysargs) 
{
	checkIfIsKernel(); //added
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
	int status;
	rc = P1_WaitDevice(USLOSS_DISK_DEV, unit, &status);
	assert(rc == P1_SUCCESS);
}

void completeReadWriteAt(int type, int sector, int unit, void *buffer) {
	int rc;
	USLOSS_DeviceRequest request;
	request.opr = type;
	request.reg1 = (void*) sector;
	request.reg2 = buffer;
	rc = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
	assert(rc == USLOSS_DEV_OK);
	int status;
	rc = P1_WaitDevice(USLOSS_DISK_DEV, unit, &status);
	assert(rc == P1_SUCCESS);
}

int P2_DiskSize(int unit, int *sector, int *track, int *disk){
	checkIfIsKernel(); //added
	int rc = P1_SUCCESS;
	if (unit < 0 || unit >= numDisks) return P1_INVALID_UNIT;
	//remove the P2_NULL_ADDRESS
	
	
	
	return rc;
}

static void 
DiskSizeStub(USLOSS_Sysargs *sysargs) 
{
	checkIfIsKernel(); //added
	int rc;
	int sector = 0, track = 0, disk = 0;
	int unit = (int) sysargs->arg1;
	// call P2_DiskSize
	rc = P2_DiskSize(unit, &sector, &track, &disk);
    // put result in sysargs
	sysargs->arg1 = (void*) sector;
	sysargs->arg2 = (void*) track;
	sysargs->arg3 = (void*) disk;
	sysargs->arg4 = (void*) rc;
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
