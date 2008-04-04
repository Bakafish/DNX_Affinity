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

/** Implements the DNX Job List mechanism.
 *
 * The purpose of this thread is to dispatch service check jobs to the
 * registered worker nodes for execution.  It accomplishes this by
 * accepting work node registrations and then dispatching service check
 * jobs to registered worker nodes using a weighted-round-robin algorithm.
 * The algorithm's weighting is based upon the number of jobs-per-second
 * throughput rating of each worker node.
 *
 * @file dnxDispatcher.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <assert.h>

#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxXml.h"
#include "dnxNebMain.h"
#include "dnxRegistrar.h"
#include "dnxJobList.h"
#include "dnxLogging.h"

static void dnxDispatcherCleanup (void *data);
static int dnxDispatchJob (DnxGlobalData *gData, DnxNewJob *pSvcReq);
static int dnxSendJob (DnxGlobalData *gData, DnxNewJob *pSvcReq, DnxNodeRequest *pNode);


//----------------------------------------------------------------------------

void *dnxDispatcher (void *data)
{
	DnxGlobalData *gData = (DnxGlobalData *)data;
	DnxNewJob SvcReq;
	int ret = 0;

	assert(data);

	// Set my cancel state to 'enabled', and cancel type to 'deferred'
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	// Set thread cleanup handler
	pthread_cleanup_push(dnxDispatcherCleanup, data);

	dnxSyslog(LOG_INFO, "dnxDispatcher[%lx]: Waiting on the Go signal...", pthread_self());

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

	dnxSyslog(LOG_INFO, "dnxDispatcher[%lx]: Awaiting new jobs...", pthread_self());

	// Wait for new service checks or cancellation
	while (1)
	{
		// Test for thread cancellation
		pthread_testcancel();

		// Wait for a new entry to be added to the Job Queue
		if ((ret = dnxJobListDispatch(gData->JobList, &SvcReq)) == DNX_OK)
		{
			// Send the Job to the next Worker Node
			if ((ret = dnxDispatchJob(gData, &SvcReq)) == DNX_OK)
			{
				// Worker Audit Logging
				dnxAuditJob(&SvcReq, "DISPATCH");
			}
			else
			{
				dnxSyslog(LOG_ERR, "dnxDispatcher[%lx]: dnxDispatchJob failed: %d", pthread_self(), ret);
				dnxAuditJob(&SvcReq, "DISPATCH-FAIL");
			}
		}
	}

	// Note that the Dispatcher thread is exiting
	dnxSyslog(LOG_INFO, "dnxDispatcher[%lx]: Exiting with ret code = %d", pthread_self(), ret);

	// Remove thread cleanup handler
	pthread_cleanup_pop(1);

	// Terminate this thread
	pthread_exit(NULL);
}

//----------------------------------------------------------------------------
// Dispatch thread clean-up routine

static void dnxDispatcherCleanup (void *data)
{
	DnxGlobalData *gData = (DnxGlobalData *)data;
	assert(data);

	// Unlock the Go signal mutex
	if (&(gData->tmGo))
		pthread_mutex_unlock(&(gData->tmGo));
}

//----------------------------------------------------------------------------
// Sends a service request to the appropriate worker node

static int dnxDispatchJob (DnxGlobalData *gData, DnxNewJob *pSvcReq)
{
	DnxNodeRequest *pNode;
	int ret;

	// Get the worker thread request
	pNode = pSvcReq->pNode;

	// Send this job to the selected worker node
	if ((ret = dnxSendJob(gData, pSvcReq, pNode)) != DNX_OK)
		dnxSyslog(LOG_ERR, "dnxDispatcher[%lx]: dnxDispatchJob: dnxSendJob failed: %d", pthread_self(), ret);

	// TODO: Implement the fork-error re-scheduling logic
	//       as found in run_service_check() in checks.c

	return ret;
}

//----------------------------------------------------------------------------

static int dnxSendJob (DnxGlobalData *gData, DnxNewJob *pSvcReq, DnxNodeRequest *pNode)
{
	DnxJob job;
	int ret;

	// Debug tracking
	dnxDebug(1, "dnxDispatcher[%lx]: dnxSendJob: Dispatching job %lu to worker node: %s",
		pthread_self(), pSvcReq->guid.objSerial, pSvcReq->cmd);

	// Initialize the job structure message
	memset(&job, 0, sizeof(job));
	job.guid     = pSvcReq->guid;
	job.state    = DNX_JOB_PENDING;
	job.priority = 1;
	job.timeout  = pSvcReq->timeout;
	job.cmd      = pSvcReq->cmd;

	// Transmit the job
	if ((ret = dnxPutJob(gData->pDispatch, &job, pNode->address)) != DNX_OK)
		dnxSyslog(LOG_ERR, "dnxDispatcher[%lx]: dnxSendJob: Unable to send job %lu to worker node (%d): %s",
			pthread_self(), pSvcReq->guid.objSerial, ret, pSvcReq->cmd);

	return ret;
}

//----------------------------------------------------------------------------
