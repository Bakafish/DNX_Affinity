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

/** Types and definitions for work load manager thread.
 * 
 * @file dnxWLM.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IFC
 */

#ifndef _DNXWLM_H_
#define _DNXWLM_H_

#include "dnxProtocol.h"

/** A WLM configuration data structure - passed to dnxWlmCreate. */
typedef struct DnxWlmCfgData
{
   char * dispatcher;            //!< The name of the dispatcher channel.
   char * collector;             //!< The name of the collector channel.
   unsigned reqTimeout;          //!< The thread request timeout in seconds.
   unsigned ttlBackoff;          //!< The time-to-live backoff in seconds.
   unsigned maxRetries;          //!< The maximum allowable retries.
   unsigned poolMin;             //!< The minimum number of pool threads.
   unsigned poolInitial;         //!< The initial number of pool threads.
   unsigned poolMax;             //!< The maximum number of pool threads.
   unsigned poolGrow;            //!< The pool growth increment value.
   unsigned pollInterval;        //!< The poll interval in seconds.
   unsigned shutdownGrace;       //!< The shutdown grace period in seconds.
   unsigned maxResults;          //!< The maximum size of the results buffer.
   unsigned showNodeAddr;        //!< Boolean: show node in error results.
   char * hostname;              //!< String holding the hostname of the client.
} DnxWlmCfgData;

/** A structure for returning WLM statistics to a caller. */
typedef struct DnxWlmStats
{
   unsigned jobs_succeeded;      //!< The total number of successful jobs.
   unsigned jobs_failed;         //!< The total number of unsuccessful jobs.
   unsigned threads_created;     //!< The total number of threads created.
   unsigned threads_destroyed;   //!< The total number of threads destroyed.
   unsigned total_threads;       //!< The number of threads in existence.
   unsigned active_threads;      //!< The number of threads currently busy.
   unsigned requests_sent;       //!< The number of requests sent.
   unsigned jobs_received;       //!< The number of jobs received.
   unsigned min_exec_time;       //!< The minimum job execution time.
   unsigned avg_exec_time;       //!< The average job execution time.
   unsigned max_exec_time;       //!< The maximum job execution time.
   unsigned avg_total_threads;   //!< The average number of threads existing.
   unsigned avg_active_threads;  //!< The average number of threads active.
   unsigned thread_time;         //!< The total amount of thread running time.
   unsigned job_time;            //!< The total time spent processing jobs.
} DnxWlmStats;

/** An abstract data type - the external representation of a WLM object. */
typedef struct { int unused; } DnxWlm;

/** Reset all WLM statistics counters.
 * 
 * @param[in] wlm - the Work Load Manager whose stats counters are to be reset.
 */
void dnxWlmResetStats(DnxWlm * wlm);

/** Return a snapshot of WLM statistics.
 * 
 * @param[in] wlm - the Work Load Manager whose stats are to be returned.
 * @param[out] wsp - the address of storage for the WLM stats to be returned.
 */
void dnxWlmGetStats(DnxWlm * wlm, DnxWlmStats * wsp);

/** Reconfigure an existing Work Load Manager object.
 * 
 * @param[in] wlm - the work load manager object to be reconfigured.
 * @param[in] cfg - the new configuration parameters.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxWlmReconfigure(DnxWlm * wlm, DnxWlmCfgData * cfg);

/** Creates a new Work Load Manager object.
 * 
 * @param[in] cfg - a reference to the WLM configuration data structure.
 * @param[out] pwlm - the address of storage for the returned WLM object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxWlmCreate(DnxWlmCfgData * cfg, DnxWlm ** pwlm);

/** The main thread routine for the work load manager.
 * 
 * @param[in] wlm - the work load manager object to be destroyed.
 */
void dnxWlmDestroy(DnxWlm * wlm);

#endif   /* _DNXWLM_H_ */

