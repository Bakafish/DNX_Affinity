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

/** Definitions and prototypes for the DNX Job List mechanism.
 *
 * @file dnxJobList.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXJOBLIST_H_
#define _DNXJOBLIST_H_

#include "dnxProtocol.h"

#include <time.h>

typedef struct _DnxNewJob_ 
{
   DnxJobState state;      // Job state
   DnxXID xid;             // Service request transaction id.
   char * cmd;             // Processed check command
   time_t start_time;      // Service check start time
   int timeout;            // Service check timeout in seconds
   time_t expires;         // Expiration time
   void * payload;         // job payload (service check structure)
   DnxNodeRequest * pNode; // Worker Request that will handle this Job
} DnxNewJob;

/** An abstract data type for a DNX Job List object. */
typedef struct { int unused; } DnxJobList;

/** Add a job to a job list.
 * 
 * This routine is invoked by the DNX NEB module's Service Check handler
 * to add new service check requests (i.e., a "job") to the Job List.
 * 
 * Jobs are marked as Waiting to be dispatched to worker nodes (via the
 * Dispatcher thread.)
 *
 * @param[in] pJobList - the job list to which @p pJob should be added.
 * @param[in] pJob - the job to be added to @p pJobList.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob);

/** Expire a set of old jobs from a job list.
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
 * @param[in] pJobList - the job list from which to expire old jobs.
 * @param[out] pExpiredJobs - the address of storage in which to return 
 *    a list of up to @p totalJobs jobs.
 * @param[in,out] totalJobs - on entry, contains the maximum size of the 
 *    array pointed to by pExpiredJobs; on exit, contains the number of jobs
 *    stored in the pExpiredJobs array.
 *
 * @return Zero on success, or a non-zero error value. (Currently always
 *    returns zero.)
 * 
 * @note This routine is called from dnxTimer, which is a deferred cancellation
 * thread start procedure. If new code is added in the future, which calls any
 * pthread or system cancellation points then appropriate cleanup routines
 * should be pushed as well. As it currently stands, the only place we could
 * be cancelled is while waiting for the list mutex. If we are cancelled 
 * during this time, no resources will be lost.
 */
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs);

/** Select a dispatchable job from a job list.
 * 
 * This routine is invoked by the Dispatcher thread to select the next
 * job waiting to be dispatched to a worker node.
 * 
 * The job is *not* removed from the Job List, but is marked as InProgress;
 * that is, it is waiting for the results from the service check.
 *
 * @param[in] pJobList - the job list from which to select a dispatchable job.
 * @param[out] pJob - the address of storage in which to return data about the
 *    job to be dispatched. Makes a copy of the job in the job list and stores
 *    the copy in the @p pJob parameter.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob);

/** Locate a pending job to which collected results should apply.
 * 
 * This routine is invoked by the Collector thread to dequeue a job from
 * the Job List when its service check result has been posted by a worker
 * node.
 * 
 * The job *is* removed from the the Job List.
 * 
 * @param[in] pJobList - the job list from which to obtain the pending job.
 * @param[in] pxid - the unique identifier for this pending job.
 * @param[out] pJob - the address of storage in which to return collected 
 *    result information about the job belonging to @p pxid.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListCollect(DnxJobList * pJobList, DnxXID * pxid, DnxNewJob * pJob);

/** Create a new job list.
 * 
 * This routine is invoked by the DNX NEB module's initialization routine to 
 * create the DNX Job List.
 * 
 * The Job List contains a time-ordered list of service check requests 
 * (i.e., "jobs") that are either waiting to be dispatched to a worker node 
 * for execution (state = Waiting) or are already executing on a worker node 
 * and are pending the service check result from the worker node (state = 
 * Pending).
 * 
 * @param[in] size - the initial size of the job list to be created.
 * @param[out] ppJobList - the address of storage for returning a new job
 *    list object pointer.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListCreate(unsigned size, DnxJobList ** ppJobList);

/** Destroy a job list.
 * 
 * This routine is invoked by the DNX NEB module's de-initialization routine
 * to release and remove the DNX Job List.
 * 
 * @param[in] pJobList - refers to the job list to be destroyed.
 */
void dnxJobListDestroy(DnxJobList * pJobList);

#endif   /* _DNXJOBLIST_H_ */

