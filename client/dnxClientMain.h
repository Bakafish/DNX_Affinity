/*--------------------------------------------------------------------------
 
   Copyright (c) 2006-2007, Intellectual Reserve, Inc. All rights reserved.
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as 
   published by the Free Software Foundation.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
  --------------------------------------------------------------------------*/

// dnxClientMain.h
//
//	Distributed Nagios Client
//
//	This program implements the worker node functionality.
//
//	Implements a distributed, dynamic thread pool model.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-06-19
//	Last Modified:   2007-08-22
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//


#ifndef _DNXMAIN_H_
#define _DNXMAIN_H_

#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>

#include "dnxError.h"
#include "dnxChannel.h"


//
//	Constants
//

#define DNX_NODE_CONFIG	"dnxNode.cfg"

typedef enum _DnxThreadState_ {
	DNX_THREAD_DEAD = 0,
	DNX_THREAD_RUNNING,
	DNX_THREAD_ZOMBIE
} DnxThreadState;


//
//	Structures
//

typedef struct _DnxWorkerStatus_ {
	DnxThreadState state;	// Thread state
	pthread_t tid;			// Thread ID
	dnxChannel *pDispatch;	// Thread job request channel
	dnxChannel *pCollect;	// Thread job reply channel
	time_t tThreadStart;	// Thread start time
	time_t tJobStart;		// Current job start time
	time_t tJobTime;		// Total amount of time spent in job processing
	unsigned jobsOk;		// Total jobs completed
	unsigned jobsFail;		// Total jobs not completed
	unsigned retries;		// Total communications retries
	void *data;				// Global data (Points to parent DnxGlobalData structure)
	unsigned long requestSerial;	// Request tracking serial number
} DnxWorkerStatus;

typedef struct _DnxGlobalData_ {
	// Configuration File properties
	char *channelAgent;
	char *channelDispatcher;
	char *channelCollector;
	long  poolInitial;
	long  poolMin;
	long  poolMax;
	long  poolGrow;
	long  wlmPollInterval;
	long  wlmShutdownGracePeriod;
	long  threadRequestTimeout;
	long  threadMaxTimeouts;
	long  threadTtlBackoff;
	char *logFacility;
	char *pluginPath;
	long  maxResultBuffer;
	long debug;

	dnxChannel *pAgent;	// Agent communications channel

	pthread_t tWLM;		// Work Load Manager thread handle
	pthread_cond_t wlmCond;
	pthread_mutex_t wlmMutex;
	pthread_mutexattr_t wlmMutexAttr;
	int terminate;		// Thread pool termination flag
	time_t noLaterThan;	// Wait no later than this epoch time to terminate all threads

	// Job Capacity management
	DnxWorkerStatus *tPool;
	int threadsActive;
	int threadsCreated;
	int threadsDestroyed;
	int jobsActive;
	int jobsProcessed;

	pthread_mutex_t threadMutex;
	pthread_mutexattr_t threadMutexAttr;

	pthread_mutex_t jobMutex;
	pthread_mutexattr_t jobMutexAttr;

	int  dnxLogFacility;	// DNX syslog facility

} DnxGlobalData;


#ifdef _SEM_SEMUN_UNDEFINED
union semun
{
	int val;                   // value for SETVAL
	struct semid_ds *buf;      // buffer for IPC_STAT & IPC_SET
	unsigned short int *array; // array for GETALL & SETALL
	struct seminfo *__buf;     // buffer for IPC_INFO
};
#endif


//
//	Globals
//


//
//	Prototypes
//

int dnxGetThreadsActive (void);
int dnxSetThreadsActive (int value);
int dnxGetJobsActive (void);
int dnxSetJobsActive (int value);

#endif   /* _DNXMAIN_H_ */

