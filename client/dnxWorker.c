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

// dnxWorker.c
//
// Distributed Nagios Client
//
// This program implements the worker thread functionality.
//
// Functions:
//
//    1. Requests a Job from the DNX Registrar
//    2. Retrieves Job and executes it
//    3. Posts Results to DNX Collector
//    4. Wash, rinse, repeat
//
// Author: Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written:   2006-06-19
// Last Modified:   2007-09-26


#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>

#include "dnxClientMain.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxXml.h"
#include "dnxWorker.h"
#include "dnxPlugin.h"


//
// Constants
//

// TODO: Dynamically allocate based upon config file maxResultBuffer setting
#define MAX_RESULT_DATA 1024


//
// Structures
//


//
// Globals
//


//
// Prototypes
//

static void dnxWorkerCleanup (void *data);
static int initWorkerComm (DnxWorkerStatus *tData);
static int releaseWorkerComm (DnxWorkerStatus *tData);
static int dnxExecuteJob (DnxWorkerStatus *tData, DnxJob *pJob, DnxResult *pResult);


//----------------------------------------------------------------------------

void *dnxWorker (void *data)
{
   DnxWorkerStatus *tData = (DnxWorkerStatus *)data;
   DnxGlobalData *gData;
   DnxNodeRequest Msg;
   DnxJob Job;
   DnxResult Result;
   int ret = DNX_OK;

   assert(data);

   // Set my cancel state to 'enabled', and cancel type to 'deferred'
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

   // Set thread cleanup handler
   pthread_cleanup_push(dnxWorkerCleanup, data);

   // Set our thread start time and state
   time(&(tData->tThreadStart));
   tData->state = DNX_THREAD_RUNNING;

   // Get reference to global data
   gData = (DnxGlobalData *)(tData->data);

   // Increment threads-created and threads-active counters
   dnxSetThreadsActive(1);

   // Initialize our communications channels
   if ((ret = initWorkerComm(tData)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxWorker[%lx]: Failed to initialize thread comm channels: %d", pthread_self(), ret);
      goto abend;
   }

   // Initialize per-thread statistics
   tData->tJobStart = 0;
   tData->tJobTime = 0;
   tData->jobsOk = 0;
   tData->jobsFail = 0;
   tData->retries = 0;
   tData->requestSerial = 0;

   // Job processing loop
   while (1)
   {
      // Test for thread cancellation
      pthread_testcancel();

      // Test for termination state
      if (gData->terminate)
         break;

      // Increment request serial number
      tData->requestSerial++;

      // Setup job request message
      dnxMakeGuid(&(Msg.guid), DNX_OBJ_WORKER, tData->requestSerial, (unsigned long)pthread_self());
      Msg.reqType = DNX_REQ_REGISTER;
      Msg.jobCap = 1;
      Msg.ttl = gData->threadRequestTimeout - gData->threadTtlBackoff;

      // Request a job
      if ((ret = dnxWantJob(tData->pDispatch, &Msg, NULL)) == DNX_OK)
      {
         // Wait for a reply
         if ((ret = dnxGetJob(tData->pDispatch, &Job, Job.address, (int)(gData->threadRequestTimeout))) == DNX_OK)
         {
            // Execute the job
            if ((ret = dnxExecuteJob(tData, &Job, &Result)) == DNX_OK)
            {
               // Post the results to the DNX Collector
               if ((ret = dnxPutResult(tData->pCollect, &Result, NULL)) != DNX_OK)
               {
                  syslog(LOG_ERR, "dnxWorker[%lx]: Result Posting failure: %d", pthread_self(), ret);
               }

               // Free result data string
               if (Result.resData)
               {
                  free(Result.resData);
                  Result.resData = NULL;
               }
            }
            else  // dnxExecuteJob failure
            {
               syslog(LOG_ERR, "dnxWorker[%lx]: Job Execution failure: %d", pthread_self(), ret);
            }
         }
         else  // dnxGetJob failure
         {
            switch (ret)
            {
            case DNX_ERR_TIMEOUT:   // Timeout is OK
               // Ignore this event
               break;
            case DNX_ERR_RECEIVE:   // Server is unavailable
               syslog(LOG_ERR, "dnxWorker[%lx]: dnxGetJob: Unable to contact server: %d", pthread_self(), ret);
               break;
            default: // Some other failure
               syslog(LOG_ERR, "dnxWorker[%lx]: dnxGetJob failure: %d", pthread_self(), ret);
            }
         }
      }  // dnxWantJob failure
      else
      {
         switch (ret)
         {
         case DNX_ERR_SEND:      // Server is unavailable
         case DNX_ERR_TIMEOUT:   // Server is unavailable
            syslog(LOG_ERR, "dnxWorker[%lx]: dnxWantJob: Unable to contact server: %d", pthread_self(), ret);
            break;
         default: // Some other failure
            syslog(LOG_ERR, "dnxWorker[%lx]: dnxWantJob failure: %d", pthread_self(), ret);
         }
      }

      // Test for termination state
      if (gData->terminate)
         break;

      // Check for error condition
      if (ret != DNX_OK)
      {
         // See if this thread has exceeded its max retries
         if (tData->retries++ >= gData->threadMaxTimeouts)
         {
            // Make sure we don't drop below the min thread count
            if (dnxGetThreadsActive() > gData->poolMin)
            {
               // Self-terminate this thread
               syslog(LOG_INFO, "dnxWorker[%lx]: Thread exiting due to max retries exceeded", pthread_self());
               break;
            }
         }

         // Sleep a little on non-timeout errors
         if (ret != DNX_ERR_TIMEOUT)
            dnxThreadSleep((int)(gData->threadRequestTimeout));
      }
      else
      {
         tData->retries = 0;
      }
   }


abend:;

   // Remove thread cleanup handler
   pthread_cleanup_pop(1);

   // Terminate this thread
   pthread_exit(NULL);

   return 0;
}

//----------------------------------------------------------------------------
// Dispatch thread clean-up routine

static void dnxWorkerCleanup (void *data)
{
   DnxWorkerStatus *tData = (DnxWorkerStatus *)data;

   assert(data);

   // Close our communications channels
   releaseWorkerComm(tData);

   // Let the WLM know this thread needs to be joined
   tData->state = DNX_THREAD_ZOMBIE;

   // Decrement active thread counter and Increment threads destroyed counter
   dnxSetThreadsActive(-1);

   syslog(LOG_INFO, "dnxWorker[%lx]: Thread Termination", pthread_self());
}


//----------------------------------------------------------------------------

static int initWorkerComm (DnxWorkerStatus *tData)
{
   DnxGlobalData *gData;
   char szChan[64];
   int ret = DNX_OK;

   // Get reference to global data
   gData = (DnxGlobalData *)(tData->data);

   tData->pDispatch = tData->pCollect = NULL;

   // Create unique name for worker dispatch channel
   sprintf(szChan, "Dispatch:%lx", pthread_self());
   
   // Create a channel for sending DNX Job Requests
   if ((ret = dnxChanMapAdd(szChan, gData->channelDispatcher)) != DNX_OK)
   {
      syslog(LOG_ERR, "initWorkerComm: dnxChanMapAdd(Dispatch) failed for thread %lx: %d", pthread_self(), ret);
      return ret;
   }

   // Attempt to open the dispatch channel
   if ((ret = dnxConnect(szChan, &(tData->pDispatch), DNX_CHAN_ACTIVE)) != DNX_OK)
   {
      syslog(LOG_ERR, "initWorkerComm: dnxConnect(Dispatch) failed for thread %lx: %d", pthread_self(), ret);
      return ret;
   }


   // Create unique name for worker collector channel
   sprintf(szChan, "Collect:%lx", pthread_self());
   
   // Create a channel for sending DNX Job Results
   if ((ret = dnxChanMapAdd(szChan, gData->channelCollector)) != DNX_OK)
   {
      syslog(LOG_ERR, "initWorkerComm: dnxChanMapAdd(Collect) failed for thread %lx: %d", pthread_self(), ret);
      return ret;
   }

   // Attempt to open the dispatch channel
   if ((ret = dnxConnect(szChan, &(tData->pCollect), DNX_CHAN_ACTIVE)) != DNX_OK)
   {
      syslog(LOG_ERR, "initWorkerComm: dnxConnect(Collect) failed for thread %lx: %d", pthread_self(), ret);
      return ret;
   }
   
   return ret;
}

//----------------------------------------------------------------------------

static int releaseWorkerComm (DnxWorkerStatus *tData)
{
   char szChan[64];
   int ret;

   // Close the dispatch channel
   if ((ret = dnxDisconnect(tData->pDispatch)) != DNX_OK)
      syslog(LOG_ERR, "releaseWorkerComm: dnxDisconnect(Dispatch) failed for thread %lx: %d", pthread_self(), ret);
   tData->pDispatch = NULL;

   // Delete this worker's dispatch channel
   sprintf(szChan, "Dispatch:%lx", pthread_self());
   if ((ret = dnxChanMapDelete(szChan)) != DNX_OK)
      syslog(LOG_ERR, "releaseWorkerComm: dnxChanMapDelete(Dispatch) failed for thread %lx: %d", pthread_self(), ret);

   // Close the collector channel
   if ((ret = dnxDisconnect(tData->pCollect)) != DNX_OK)
      syslog(LOG_ERR, "releaseWorkerComm: dnxDisconnect(Collect) failed for thread %lx: %d", pthread_self(), ret);
   tData->pCollect = NULL;

   // Delete this worker's collector channel
   sprintf(szChan, "Collect:%lx", pthread_self());
   if ((ret = dnxChanMapDelete(szChan)) != DNX_OK)
      syslog(LOG_ERR, "releaseWorkerComm: dnxChanMapDelete(Collect) failed for thread %lx: %d", pthread_self(), ret);

   return ret;
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
      syslog(LOG_ERR, "dnxThreadSleep: Failed to lock timerMutex: %d", ret);
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
      syslog(LOG_ERR, "dnxThreadSleep: Failed to wait on timerCondition: %d", ret);
      return ret;
   }

   if ((ret = pthread_mutex_unlock(&timerMutex)) != 0)
   {
      syslog(LOG_ERR, "dnxThreadSleep: Failed to unlock timerMutex: %d", ret);
   }

   return ret;
}

//----------------------------------------------------------------------------

static int dnxExecuteJob (DnxWorkerStatus *tData, DnxJob *pJob, DnxResult *pResult)
{
   char resData[MAX_RESULT_DATA+1];
   DnxGlobalData *gData;
   int ret;

   // Get reference to global data
   gData = (DnxGlobalData *)(tData->data);

   // Increment the global jobs-active counter
   dnxSetJobsActive(1);

   // Announce job reception
   if (gData->debug)
      syslog(LOG_INFO, "dnxExecuteJob[%lx]: Received job [%lu,%lu] (T/O %d): %s", pthread_self(), pJob->guid.objSerial, pJob->guid.objSlot, pJob->timeout, pJob->cmd);

   // Prepare result structure
   pResult->guid = pJob->guid;         // Copy GUID over intact, as the server uses this for result matchup with the request
   pResult->state = DNX_JOB_COMPLETE;  // Job can be either complete or expired
   pResult->delta = 0;              // Job execution delta
   pResult->resCode = DNX_PLUGIN_RESULT_OK;  // OK
   pResult->resData = NULL;         // Result data

   memset(resData, 0, sizeof(resData));

   // Start job timer
   time(&(tData->tJobStart));
   
   // Execute the job
   ret = dnxPluginExecute(pJob->cmd, &(pResult->resCode), resData, MAX_RESULT_DATA, pJob->timeout);

   // Compute job execution delta
   pResult->delta = time(NULL) - tData->tJobStart;

   // Store dynamic copy of the result string
   if (resData[0] && (pResult->resData = strdup(resData)) == NULL)
   {
      syslog(LOG_ERR, "dnxExecuteJob[%lx]: Out of Memory: null result string for job [%lu,%lu]: %s", pthread_self(), pJob->guid.objSerial, pJob->guid.objSlot, pJob->cmd);
      ret = DNX_ERR_MEMORY;
   }

   // Decrement the global jobs-active counter
   dnxSetJobsActive(-1);

   // Update per-thread statistics
   tData->tJobStart = 0;
   tData->tJobTime += pResult->delta;
   if (pResult->resCode == DNX_PLUGIN_RESULT_OK)
      tData->jobsOk++;
   else
      tData->jobsFail++;

   return ret;
}

/*--------------------------------------------------------------------------*/

