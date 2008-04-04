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
 * @file dnxJobList.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxJobList.h"

#include "dnxLogging.h"

extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind);

//----------------------------------------------------------------------------
// This routine is invoked by the DNX NEB module's initialization routine
// to create the DNX Job List.
//
// The Job List contains a time-ordered list of service check requests
// (i.e., "jobs") that are either:
//
// 1. Waiting to be dispatched to a worker node for execution (state = Waiting) or,
//
// 2. Are already executing on a worker node and are pending the service
//    check result from the worker node (state = Pending).
//

int dnxJobListInit (DnxJobList **ppJobList, unsigned long size)
{
   // Validate parameters
   if (!ppJobList || size < 1)
      return DNX_ERR_INVALID;

   // Create the job list structure
   if ((*ppJobList = (DnxJobList *)malloc(sizeof(DnxJobList))) == NULL)
      return DNX_ERR_MEMORY;

   //dnxDebug(10, "dnxJobListInit: Malloc(*ppJobList=%p)", *ppJobList);

   // Initialize the job list
   memset(*ppJobList, 0, sizeof(DnxJobList));

   // Create the array of job structures
   if (((*ppJobList)->pList = (DnxNewJob *)malloc(sizeof(DnxNewJob) * size)) == NULL)
   {
      //dnxDebug(10, "dnxJobListInit: Free(*ppJobList=%p)", *ppJobList);
      free(*ppJobList);
      *ppJobList = NULL;
      return DNX_ERR_MEMORY;
   }

   //dnxDebug(10, "dnxJobListInit: Malloc((*ppJobList)->pList=%p)", (*ppJobList)->pList);

   // Set all slots to null
   memset((*ppJobList)->pList, 0, (sizeof(DnxNewJob) * size));

   // Store array size
   (*ppJobList)->size = size;

   // Initialize the mutex and condition variable
   pthread_mutexattr_init(&((*ppJobList)->mut_attr));
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
   pthread_mutexattr_settype(&((*ppJobList)->mut_attr), PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
   pthread_mutex_init(&((*ppJobList)->mut), &((*ppJobList)->mut_attr));
   pthread_cond_init(&((*ppJobList)->cond), NULL);

   return DNX_OK;
}

//----------------------------------------------------------------------------
// This routine is invoked by the DNX NEB module's de-initialization routine
// to release and remove the DNX Job List.
//

int dnxJobListWhack (DnxJobList **ppJobList)
{
   // Validate parameters
   if (!ppJobList)
      return DNX_ERR_INVALID;

   // Destroy the mutex
   if (pthread_mutex_destroy(&((*ppJobList)->mut)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxJobListWhack: Unable to destroy mutex: mutex is in use!");
      return DNX_ERR_BUSY;
   }
   pthread_mutexattr_destroy(&((*ppJobList)->mut_attr));

   // Destroy the condition variable
   if (pthread_cond_destroy(&((*ppJobList)->cond)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxJobListWhack: Unable to destroy condition-variable: condition-variable is in use!");
      return DNX_ERR_BUSY;
   }

   // Free the job list array
   if ((*ppJobList)->pList && (*ppJobList)->size > 0)
   {
      //dnxDebug(10, "dnxJobListWhack: Free((*ppJobList)->pList=%p)", (*ppJobList)->pList);
      free((*ppJobList)->pList);
   }

   // Free the job list
   //dnxDebug(10, "dnxJobListWhack: Free(*ppJobList=%p)", *ppJobList);
   free(*ppJobList);

   *ppJobList = NULL;

   return DNX_OK;
}

//----------------------------------------------------------------------------
// This routine is invoked by the DNX NEB module's Service Check handler
// to add new service check requests (i.e., a "job") to the Job List.
//
// Jobs are marked as Waiting to be dispatched to worker nodes (via the
// Dispatcher thread.)
//

int dnxJobListAdd (DnxJobList *pJobList, DnxNewJob *pJob)
{
   unsigned long tail;
   int ret = DNX_OK;

   // Validate parameters
   if (!pJobList || !pJob)
      return DNX_ERR_INVALID;

   // Acquire the lock on the job list
   if (pthread_mutex_lock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   tail = pJobList->tail;

   // Verify space in the job list
   if (pJobList->pList[tail].state && (tail = ((tail + 1) % pJobList->size)) == pJobList->head)
   {
      dnxSyslog(LOG_ERR, "dnxJobListAdd: Out of job slots (max=%lu): %s", pJobList->size, pJob->cmd);
      ret = DNX_ERR_CAPACITY;
      goto abend;
   }

   // Add the slot identifier to the Job's GUID
   pJob->guid.objSlot = tail;
   pJob->state = DNX_JOB_PENDING;

   // Add this job to the job list
   memcpy(&(pJobList->pList[tail]), pJob, sizeof(DnxNewJob));;

   // Update dispatch head index
   if (pJobList->pList[pJobList->tail].state != DNX_JOB_PENDING)
      pJobList->dhead = tail;

   // Update the tail index
   pJobList->tail = tail;

   //dnxDebug(8, "dnxJobListAdd: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pJob->guid.objSerial, pJob->guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

   // Signal the condition variable to indicate that a new job is available
   pthread_cond_signal(&(pJobList->cond));

abend:
   // Release the lock on the job list
   if (pthread_mutex_unlock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListAdd: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      ret = DNX_ERR_THREAD;
   }

   return ret;
}

//----------------------------------------------------------------------------
// This routine is invoked by the Timer thread to dequeue all jobs whose
// timeout has occurred.
//
// Note that this routine walks the *entire* Job List and can remove jobs
// that are either InProgress (awaiting a result) or Pending (awaiting dispatch.)
//
// Jobs that are deemed to have expired are passed to the Timer thread
// via a call back mechanism (for efficiency.)
//

int dnxJobListExpire (DnxJobList *pJobList, DnxNewJob *pExpiredJobs, int *totalJobs)
{
   unsigned long current;
   DnxNewJob *pJob;
   int jobCount;
   time_t now;

   // Validate parameters
   if (!pJobList || !pExpiredJobs || !totalJobs || *totalJobs < 1)
      return DNX_ERR_INVALID;

   // Acquire the lock on the job list
   if (pthread_mutex_lock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // Get the current time (After we acquire the lock! In case we had to wait to acquire it...)
   now = time(NULL);

   // Walk the entire job list: both InProgress and dispatch-Pending jobs (in that order)
   current = pJobList->head;
   jobCount = 0;
   while (jobCount < *totalJobs)
   {
      // Only examine jobs that are either awaiting dispatch or results
      if ((pJob = &(pJobList->pList[current]))->state == DNX_JOB_INPROGRESS || pJob->state == DNX_JOB_PENDING)
      {
         // Check the job's expiration stamp
         if (pJob->expires > now)
            break;   // Bail-out: this job hasn't expired yet

         // Job has expired - add it to the expired job list
         memcpy(&(pExpiredJobs[jobCount]), pJob, sizeof(DnxNewJob));

         // And dequeue it.
         pJob->state = DNX_JOB_NULL;

         jobCount++;    // Increment expired job list index
      }

      // Bail-out if this was the job list tail
      if (current == pJobList->tail)
         break;

      // Increment the job list index
      current = ((current + 1) % pJobList->size);
   }

   // Update the head index
   pJobList->head = current;

   // If this job is awaiting dispatch, then it is the new dispatch head
   if (pJobList->pList[current].state != DNX_JOB_INPROGRESS)
      pJobList->dhead = current;

   // Update the total jobs in the expired job list
   *totalJobs = jobCount;

   // Release the lock on the job list
   if (pthread_mutex_unlock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListExpire: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------
// This routine is invoked by the Dispatcher thread to select the next
// job waiting to be dispatched to a worker node.
//
// The job is *not* removed from the Job List, but is marked as InProgress;
// that is, it is waiting for the results from the service check.
//

int dnxJobListDispatch (DnxJobList *pJobList, DnxNewJob *pJob)
{
   unsigned long current;

   // Validate parameters
   if (!pJobList || !pJob)
      return DNX_ERR_INVALID;

   // Acquire the lock on the job list
   if (pthread_mutex_lock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // Wait on the condition variable if there are no pending jobs
   //
   // TODO: Need to track total number of Pending jobs in the JobList structure?
   //       OR simply just check to see if the dhead index points to a valid
   //       Pending job?

   // Start at current dispatch head
   current = pJobList->dhead;

   // See if we have a Pending job
   while (pJobList->pList[current].state != DNX_JOB_PENDING)
   {
      //dnxDebug(8, "dnxJobListDispatch: BEFORE: Head=%lu, DHead=%lu, Tail=%lu", pJobList->head, pJobList->dhead, pJobList->tail);
      pthread_cond_wait(&(pJobList->cond), &(pJobList->mut));
      current = pJobList->dhead;
   }

   // Transition this job's state to InProgress
   pJobList->pList[current].state = DNX_JOB_INPROGRESS;

   // Make a copy for the Dispatcher
   memcpy(pJob, &(pJobList->pList[current]), sizeof(DnxNewJob));
   //pJob->cmd = strdup(pJob->cmd); // BUG: This causes a memory leak!

   // Update the dispatch head
   if (pJobList->dhead != pJobList->tail)
      pJobList->dhead = ((current + 1) % pJobList->size);

   //dnxDebug(8, "dnxJobListDispatch: AFTER: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pJob->guid.objSerial, pJob->guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

   // Release the lock on the job list
   if (pthread_mutex_unlock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListDispatch: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------
// This routine is invoked by the Collector thread to dequeue a job from
// the Job List when its service check result has been posted by a worker
// node.
//
// The job *is* removed from the the Job List.
//

int dnxJobListCollect (DnxJobList *pJobList, DnxGuid *pGuid, DnxNewJob *pJob)
{
   unsigned long current;
   int ret = DNX_OK;

   // Validate parameters
   if (!pJobList || !pGuid || !pJob)
      return DNX_ERR_INVALID;

   // Validate the slot identifier in the GUID
   if ((current = pGuid->objSlot) >= pJobList->size)
      return DNX_ERR_INVALID;

   // Acquire the lock on the job list
   if (pthread_mutex_lock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   //dnxDebug(8, "dnxJobListCollect: Compare [%lu,%lu] to [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pGuid->objSerial, pGuid->objSlot,
   // pJobList->pList[current].guid.objSerial, pJobList->pList[current].guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

   // Verify that the GUID of this result matches the GUID of the service check
   if (pJobList->pList[current].state == DNX_JOB_NULL || memcmp(pGuid, &(pJobList->pList[current].guid), sizeof(DnxGuid)) != 0)
   {
      // Most likely, this job expired and was removed from the Job List by the Timer thread
      ret = DNX_ERR_NOTFOUND;
      goto abend;
   }

   // Make a copy for the Collector
   memcpy(pJob, &(pJobList->pList[current]), sizeof(DnxNewJob));
   pJob->state = DNX_JOB_COMPLETE;

   // Dequeue this job
   pJobList->pList[current].state = DNX_JOB_NULL;

   // Update the job list head
   if (current == pJobList->head && current != pJobList->tail)
      pJobList->head = ((current + 1) % pJobList->size);

abend:
   // Release the lock on the job list
   if (pthread_mutex_unlock(&(pJobList->mut)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxJobListCollect: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      ret = DNX_ERR_THREAD;
   }

   return ret;
}

/*--------------------------------------------------------------------------*/

