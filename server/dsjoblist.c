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
 * @file dsjoblist.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include "dsjoblist.h"

#include "dsaudit.h"

#include "dnxLogging.h"
#include "dnxError.h"
#include "dnxDebug.h"


static int auditingEnabled;   /*!< Is job auditing enabled? */

/** Add a new job to a job list.
 *
 * This routine is invoked by the DNX NEB module's Service Check handler
 * to add new service check requests (i.e., a "job") to the Job List.
 * 
 * Jobs are marked as Waiting to be dispatched to worker nodes (via the
 * Dispatcher thread.)
 * 
 * @param[in] pJobList - the job list to which a new job should be added.
 * @param[in] pJob - the new job to be added to @p pJobList.
 *
 * @return Zero on success, or a non-zero error code.
 */
int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob)
{
   unsigned long tail;
   int ret = DNX_OK;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&pJobList->mut);

   tail = pJobList->tail;
   if (pJobList->pList[tail].state && (tail = (tail + 1) % pJobList->size) == pJobList->head)
   {
      dnxSyslog(LOG_ERR, "dnxJobListAdd: Out of job slots (max=%lu): %s", pJobList->size, pJob->cmd);
      ret = DNX_ERR_CAPACITY;
   }
   else
   {
      // Add the slot identifier to the Job's GUID
      pJob->guid.objSlot = tail;
      pJob->state = DNX_JOB_PENDING;

      // Add this job to the job list
      memcpy(&pJobList->pList[tail], pJob, sizeof pJobList->pList[tail]);

      // Update dispatch head index
      if (pJobList->pList[pJobList->tail].state != DNX_JOB_PENDING)
         pJobList->dhead = tail;

      // Update the tail index
      pJobList->tail = tail;

      // dnxDebug(8, "dnxJobListAdd: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pJob->guid.objSerial, pJob->guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

      // Signal the condition variable to indicate that a new job is available
      pthread_cond_signal(&pJobList->cond);
   }

   DNX_PT_MUTEX_UNLOCK(&pJobList->mut);

   return ret;
}


/** Remove jobs older than job expiration time.
 *
 * This routine is invoked by the Timer thread to dequeue all jobs whose
 * timeout has occurred.
 * 
 * Note that this routine walks the *entire* Job List and can remove jobs
 * that are either InProgress (awaiting a result) or Pending (awaiting dispatch.)
 * 
 * Jobs that are deemed to have expired are passed to the Timer thread
 * via a call back mechanism (for efficiency.)
 * 
 * @param[in] pJobList - the list to be scanned for expired jobs.
 * @param[out] pExpiredJobs - the address of storage for expired jobs.
 * @param[in/out] totalJobs - on entry, contains the size of @p pExpiredJobs;
 *    returns the number of slots used in @p pExpiredJobs.
 *
 * @return Zero on success, or a non-zero error code.
 */
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs)
{
   unsigned long current;
   DnxNewJob * pJob;
   int jobCount = 0;
   time_t now;

   // validate parameters
   assert(pJobList && pExpiredJobs && totalJobs && *totalJobs > 0);

   DNX_PT_MUTEX_LOCK(&pJobList->mut);

   // Get the current time (After we acquire the lock! In case we had to wait to acquire it...)
   now = time(0);

   // Walk the entire job list: both InProgress and dispatch-Pending jobs (in that order)
   current = pJobList->head;
   while (jobCount < *totalJobs)
   {
      // Only examine jobs that are either awaiting dispatch or results
      if ((pJob = &pJobList->pList[current])->state == DNX_JOB_INPROGRESS || pJob->state == DNX_JOB_PENDING)
      {
         /** @todo Do we really want to break out here? If we do, we'll never return
          * any expired jobs as long as we find an unexpired one first. 
          */

         if (pJob->expires > now)
            break;   // Bail-out: this job hasn't expired yet

         // Job has expired - add it to the expired job list
         memcpy(&pExpiredJobs[jobCount], pJob, sizeof(DnxNewJob));

         pJob->state = DNX_JOB_NULL; // And dequeue it

         jobCount++;    // Increment expired job list index
      }

      // Bail-out if this was the job list tail
      if (current == pJobList->tail)
         break;

      // Increment the job list index
      current = (current + 1) % pJobList->size;
   }

   // Update the head indices
   pJobList->head = current;
   if (pJobList->pList[current].state != DNX_JOB_INPROGRESS)
      pJobList->dhead = current;

   *totalJobs = jobCount;

   DNX_PT_MUTEX_UNLOCK(&pJobList->mut);

   return DNX_OK;
}


/** Selects the next available job to be dispatched.
 *
 * This routine is invoked by the Dispatcher thread to select the next job 
 * waiting to be dispatched to a worker node.
 * 
 * The job is *not* removed from the Job List, but is marked as InProgress;
 * that is, it is waiting for the results from the service check.
 * 
 * @param[in] pJobList - the list to be scanned for jobs to be dispatched.
 * @param[out] pJob - the address of storage for the returned job.
 *
 * @return Zero on success, or a non-zero error code.
 */
int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob)
{
   unsigned long current;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&pJobList->mut);

   // Wait on the condition variable if there are no pending jobs

   /** @todo Do we need to track total number of Pending jobs in the JobList 
    * structure OR simply just check to see if the dhead index points to a
    * valid Pending job?
    */

   // Scan for a Pending job from the current dispatch head
   current = pJobList->dhead;
   while (pJobList->pList[current].state != DNX_JOB_PENDING)
   {
      // dnxDebug(8, "dnxJobListDispatch: BEFORE: Head=%lu, DHead=%lu, Tail=%lu", pJobList->head, pJobList->dhead, pJobList->tail);
      pthread_cond_wait(&pJobList->cond, &pJobList->mut);
      current = pJobList->dhead;
   }

   // Transition this job's state to InProgress
   pJobList->pList[current].state = DNX_JOB_INPROGRESS;

   // Make a copy for the Dispatcher
   memcpy(pJob, &pJobList->pList[current], sizeof pJobList->pList[current]);
   // pJob->cmd = strdup(pJob->cmd); // BUG: This causes a memory leak!

   // Update the dispatch head
   if (pJobList->dhead != pJobList->tail)
      pJobList->dhead = (current + 1) % pJobList->size;

   // dnxDebug(8, "dnxJobListDispatch: AFTER: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pJob->guid.objSerial, pJob->guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

   DNX_PT_MUTEX_UNLOCK(&pJobList->mut);

   return DNX_OK;
}


/** Matches pending jobs to job results from dnxClient.
 *
 * This routine is invoked by the Collector thread to dequeue a job from
 * the Job List when its service check result has been posted by a worker
 * node.
 * 
 * The job *is* removed from the the Job List.
 *
 * @param[in] pJobList - the list to be searched.
 * @param[in] pGuid - the transaction id we're looking for.
 * @param[out] pJob - the address of storage for the matching job.
 *
 * @return Zero on success, or a non-zero error code.
 */
int dnxJobListCollect(DnxJobList * pJobList, DnxGuid * pGuid, DnxNewJob * pJob)
{
   unsigned long current;
   int ret = DNX_OK;

   assert(pJobList && pGuid && pJob);

   current = pGuid->objSlot;

   assert(current < pJobList->size);

   DNX_PT_MUTEX_LOCK(&pJobList->mut);

   // dnxDebug(8, "dnxJobListCollect: Compare [%lu,%lu] to [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", pGuid->objSerial, pGuid->objSlot,
   //       pJobList->pList[current].guid.objSerial, pJobList->pList[current].guid.objSlot, pJobList->head, pJobList->dhead, pJobList->tail);

   // verify the GUID of this result matches that of the service check
   if (memcmp(pGuid, &(pJobList->pList[current].guid), sizeof(DnxGuid)) != 0 || pJobList->pList[current].state == DNX_JOB_NULL)
      ret = DNX_ERR_NOTFOUND; // the job expired and was removed by the Timer thread
   else
   {
      // make a copy for the Collector
      memcpy(pJob, &pJobList->pList[current], sizeof pJobList->pList[current]);
      pJob->state = DNX_JOB_COMPLETE;

      // dequeue the job
      pJobList->pList[current].state = DNX_JOB_NULL;
      if (current == pJobList->head && current != pJobList->tail)
         pJobList->head = (current + 1) % pJobList->size;
   }

   DNX_PT_MUTEX_UNLOCK(&pJobList->mut);

   return ret;
}


/** Post a new job to a job list.
 * 
 * @param[in] jobList - the list to which a new job should be posted.
 * @param[in] serial - the serial number of the new job.
 * @param[in] ds - the Nabios object representing the job.
 * @param[in] pNode - the request node to be assigned to the job.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPostNewJob (DnxJobList * jobList, unsigned long serial, nebstruct_service_check_data * ds, DnxNodeRequest * pNode)
{
   service * svc;
   DnxNewJob Job;
   int ret;

   // Obtain a pointer to the Nagios service definition structure
#ifdef DNX_EMBEDDED_SVC_OBJECT
   if ((svc = (service *)(ds->object)) == NULL)
#else
   if ((svc = find_service(ds->host_name, ds->service_description)) == NULL)
#endif
   {
      // ERROR - This should never happen here: The service was not found.
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Could not find service %s for host %s",
         ds->service_description, ds->host_name);
      return DNX_ERR_INVALID;
   }

   // Fill-in the job structure with the necessary information
   dnxMakeGuid(&(Job.guid), DNX_OBJ_JOB, serial, 0);
   Job.svc        = svc;
   Job.cmd        = strdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   Job.expires    = Job.start_time + Job.timeout;
   Job.pNode      = pNode;

   dnxDebug(1, "DnxNebMain: Posting Job %lu: %s", serial, Job.cmd);

   // Post to the Job Queue
   if ((ret = dnxJobListAdd(jobList, &Job)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Failed to post Job \"%s\": %d", Job.cmd, ret);

   // Worker Audit Logging
   dsAuditJob(&Job, "ASSIGN");

   return ret;
}


/** Delete all resources (memory) specific to a given job.
 * 
 * @param[in] pJob - the job to be deleted.
 */
void dnxJobCleanup (DnxNewJob * pJob)
{
   if (pJob)
   {
      // Free the Pending Job command string
      if (pJob->cmd)
      {
         free(pJob->cmd);
         pJob->cmd = NULL;
      }

      // Free the node request message
      dnxDebug(10, "dnxJobCleanup: Free(pNode=%p)", pJob->pNode);
      if (pJob->pNode)
      {
         free(pJob->pNode);
         pJob->pNode = NULL;
      }
   }
}


/** Initialize a new JobList object.
 *
 * This routine is invoked by the DNX NEB module's initialization routine to 
 * create the DNX Job List.
 *
 * The Job List contains a time-ordered list of service check requests
 * (i.e., "jobs") that are either:
 * 
 * 1. Waiting to be dispatched to a worker node for execution (state = Waiting) or,
 *
 * 2. Are already executing on a worker node and are pending the service
 *    check result from the worker node (state = Pending).
 *
 * @param[out] ppJobList - the address of storage for the returned job list.
 * @param[in] size - the size of the job list.
 *
 * @return Zero on success, or a non-zero error code.
 */
int dnxJobListInit(DnxJobList ** ppJobList, unsigned long size)
{
   DnxJobList * joblist;

   assert(ppJobList && size);

   // create the job list structure
   if ((joblist = (DnxJobList *)malloc(sizeof *joblist)) == NULL)
      return DNX_ERR_MEMORY;

   // dnxDebug(10, "dnxJobListInit: Malloc(joblist=%p)", *joblist);

   memset(joblist, 0, sizeof *joblist);

   // create the array of job structures of the specified size
   if ((joblist->pList = (DnxNewJob *)malloc(sizeof *joblist->pList * size)) == NULL)
   {
      // dnxDebug(10, "dnxJobListInit: Free(*ppJobList=%p)", *ppJobList);
      free(joblist);
      return DNX_ERR_MEMORY;
   }

   // dnxDebug(10, "dnxJobListInit: Malloc((*ppJobList)->pList=%p)", (*ppJobList)->pList);

   memset(joblist->pList, 0, sizeof *joblist->pList * size);

   joblist->size = size;

   DNX_PT_MUTEX_INIT(&joblist->mut);
   pthread_cond_init(&joblist->cond, 0);

   *ppJobList = joblist;

   return DNX_OK;
}


/** Destroy a job list object created by dnxJobListInit.
 *
 * This routine is invoked by the DNX NEB module's de-initialization routine
 * to release and remove the DNX Job List.
 *
 * @param[in/out] ppJobList - the address of the job list to be destroyed.
 */
void dnxJobListExit(DnxJobList ** ppJobList)
{
   DnxJobList * joblist;

   assert(ppJobList && *ppJobList);

   joblist = *ppJobList;

   DNX_PT_MUTEX_DESTROY(&joblist->mut);
   DNX_PT_COND_DESTROY(&joblist->cond);

   assert(joblist->pList && joblist->size > 0);

   // dnxDebug(10, "dnxJobListExit: free((*ppJobList)->pList=%p)", joblist->pList);
   free((*ppJobList)->pList);

   // dnxDebug(10, "dnxJobListExit: free(*ppJobList=%p)", joblist);
   free(joblist);

   *ppJobList = NULL;
}

/*-------------------------------------------------------------------------*/

