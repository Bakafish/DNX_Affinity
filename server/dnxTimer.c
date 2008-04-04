//	dnxTimer.c
//
//	This file implements the DNX Timer thread.
//
//	The purpose of this thread is to monitor the age of service requests
//	which are being actively executed by the worker nodes.
//
//	This requires access to the global Pending queue (which is also
//	manipulated by the Dispatcher and Collector threads.)
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-07-11
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


#include "dnxNebMain.h"
#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxTimer.h"
#include "dnxJobList.h"
#include "dnxLogging.h"

#include <assert.h>


//
//	Constants
//

#define MAX_EXPIRED		10


//
//	Structures
//


//
//	Globals
//


//
//	Prototypes
//

static void dnxTimerCleanup (void *data);
static int dnxExpireJob (DnxNewJob *pExpire);


//----------------------------------------------------------------------------

void *dnxTimer (void *data)
{
	DnxGlobalData *gData = (DnxGlobalData *)data;
	DnxNewJob ExpiredList[MAX_EXPIRED];
	int i, totalExpired;
	int ret = 0;

	assert(data);

	// Set my cancel state to 'enabled', and cancel type to 'deferred'
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	// Set thread cleanup handler
	pthread_cleanup_push(dnxTimerCleanup, data);

	dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Waiting on the Go signal...", pthread_self());

	// Wait for Go signal from dnxNebMain
	if (pthread_mutex_lock(&(gData->tmGo)) != 0)
		pthread_exit(NULL);

	// See if the go signal has already been broadcast
	if (gData->isGo == 0)
	{
		// Nope.  Wait for the synchronization signal
		if (pthread_cond_wait(&(gData->tcGo), &(gData->tmGo)) != 0)
		{
			// pthread_mutex_unlock(&(gData->tmGo));
			pthread_exit(NULL);
		}
	}

	// Release the lock
	pthread_mutex_unlock(&(gData->tmGo));

	dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Watching for expired jobs...", pthread_self());

	// Wait for new service checks or cancellation
	while (1)
	{
		// Yield control for a few seconds
		if ((ret = dnxThreadSleep(DNX_TIMER_SLEEP)) != 0)
			break;

		// Search for expired jobs in the Pending Queue
		totalExpired = MAX_EXPIRED;
		if ((ret = dnxJobListExpire(gData->JobList, ExpiredList, &totalExpired)) == DNX_OK && totalExpired > 0)
		{
			for (i=0; i < totalExpired; i++)
			{
				dnxDebug(1, "dnxTimer[%lx]: Expiring Job: %s", pthread_self(), ExpiredList[i].cmd);

				// Worker Audit Logging
				dnxAuditJob(&ExpiredList[i], "EXPIRE");

				// Report the expired job to Nagios
				ret = dnxExpireJob(&ExpiredList[i]);

				// Release this Job's resources
				dnxJobCleanup(&ExpiredList[i]);
			}
		}

		if (totalExpired > 0 || ret != DNX_OK)
			dnxDebug(1, "dnxTimer[%lx]: Expired job count: %d  Retcode=%d", pthread_self(), totalExpired, ret);

		// Test for thread cancellation
		pthread_testcancel();
	}

	// Note that the Timer thread is exiting
	dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Exiting with ret code = %d", pthread_self(), ret);

	// Remove thread cleanup handler
	pthread_cleanup_pop(1);

	// Terminate this thread
	pthread_exit(NULL);
}

//----------------------------------------------------------------------------
// Dispatch thread clean-up routine

static void dnxTimerCleanup (void *data)
{
	DnxGlobalData *gData = (DnxGlobalData *)data;
	assert(data);

	// Unlock the Go signal mutex
	if (&(gData->tmGo))
		pthread_mutex_unlock(&(gData->tmGo));
}

//----------------------------------------------------------------------------

int dnxThreadSleep (int seconds)
{
	pthread_mutex_t timerMutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t  timerCond  = PTHREAD_COND_INITIALIZER;
	struct timeval  now;            // Time when we started waiting
	struct timespec timeout;        // Timeout value for the wait function
	int ret;

	// Create temporary mutex for time waits
	if ((ret = pthread_mutex_lock(&timerMutex)) != 0)
	{
		dnxSyslog(LOG_ERR, "dnxThreadSleep: Failed to lock timerMutex: %d", ret);
		return ret;
	}

	gettimeofday(&now, NULL);

	// timeval uses micro-seconds.
	// timespec uses nano-seconds.
	// 1 micro-second = 1000 nano-seconds.
	timeout.tv_sec = now.tv_sec + seconds;
	timeout.tv_nsec = now.tv_usec * 1000;

	// Sleep for the specified time
	if ((ret = pthread_cond_timedwait(&timerCond, &timerMutex, &timeout)) != ETIMEDOUT)
	{
		pthread_mutex_unlock(&timerMutex);
		dnxSyslog(LOG_ERR, "dnxThreadSleep: Failed to wait on timerCondition: %d", ret);
		return ret;
	}

	if ((ret = pthread_mutex_unlock(&timerMutex)) != 0)
	{
		dnxSyslog(LOG_ERR, "dnxThreadSleep: Failed to unlock timerMutex: %d", ret);
	}

	return ret;
}

//----------------------------------------------------------------------------
// Posts an expired service request to the Nagios service result buffer

static int dnxExpireJob (DnxNewJob *pExpire)
{
	extern circular_buffer service_result_buffer;
	extern int check_result_buffer_slots;
	service_message *new_message;

	// Allocate memory for the message
	if ((new_message = (service_message *)malloc(sizeof(service_message))) == NULL)
	{
		dnxSyslog(LOG_ERR, "dnxExpireJob: Memory allocation failure");
		return DNX_ERR_MEMORY;
	}

	// Copy the expired job's data to the message buffer
	gettimeofday(&(new_message->finish_time), NULL);
	strncpy(new_message->host_name, pExpire->svc->host_name, sizeof(new_message->host_name)-1);
	new_message->host_name[sizeof(new_message->host_name)-1] = '\0';
	strncpy(new_message->description, pExpire->svc->description, sizeof(new_message->description)-1);
	new_message->description[sizeof(new_message->description)-1] = '\0';
#ifdef SERVICE_CHECK_TIMEOUTS_RETURN_UNKNOWN
	new_message->return_code = STATE_UNKNOWN;
#else
	new_message->return_code = STATE_CRITICAL;
#endif
	new_message->exited_ok = TRUE;
	new_message->check_type = SERVICE_CHECK_ACTIVE;
	new_message-> parallelized = pExpire->svc->parallelize;
	new_message->start_time.tv_sec = pExpire->start_time;
	new_message->start_time.tv_usec = 0L;
	new_message->early_timeout = TRUE;
	strcpy(new_message->output, "(DNX Service Check Timed Out)");

	// Obtain a lock for writing to the buffer
	pthread_mutex_lock(&service_result_buffer.buffer_lock);

	// Handle overflow conditions
	if (service_result_buffer.items == check_result_buffer_slots)
	{
		// Record overflow
		service_result_buffer.overflow++;

		// Update tail pointer
		service_result_buffer.tail = (service_result_buffer.tail + 1) % check_result_buffer_slots;

		dnxSyslog(LOG_ERR, "dnxExpireJob: Service result buffer overflow = %lu", service_result_buffer.overflow);
	}

	// Save the data to the buffer
	((service_message **)service_result_buffer.buffer)[service_result_buffer.head] = new_message;

	// Increment the head counter and items
	service_result_buffer.head = (service_result_buffer.head + 1) % check_result_buffer_slots;
	if (service_result_buffer.items < check_result_buffer_slots)
		service_result_buffer.items++;
	if(service_result_buffer.items>service_result_buffer.high)
		service_result_buffer.high=service_result_buffer.items;

	// Release lock on buffer
	pthread_mutex_unlock(&service_result_buffer.buffer_lock);

	return DNX_OK;
}

//----------------------------------------------------------------------------
