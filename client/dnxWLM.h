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

/** A WLM configuration data structure - passed to dnxWlmCreate. */
typedef struct DnxWlmCfgData
{
   char * dispatcher;      /*!< The name of the dispatcher channel. */
   char * collector;       /*!< The name of the collector channel. */
   unsigned reqTimeout;    /*!< The thread request timeout in seconds. */
   unsigned ttlBackoff;    /*!< The time-to-live backoff in seconds. */
   unsigned maxRetries;    /*!< The maximum allowable retries. */
   unsigned poolMin;       /*!< The minimum number of pool threads. */
   unsigned poolInitial;   /*!< The initial number of pool threads. */
   unsigned poolMax;       /*!< The maximum number of pool threads. */
   unsigned poolGrow;      /*!< The pool growth increment value. */
   unsigned pollInterval;  /*!< The poll interval in seconds. */
   unsigned shutdownGrace; /*!< The shutdown grace period in seconds. */
   unsigned maxResults;    /*!< The maximum size of the results buffer. */
} DnxWlmCfgData;

/** An abstract data type - the external representation of a WLM object. */
typedef struct { int unused; } DnxWlm;

/** Return the active thread count on the specified Work Load Manager.
 * 
 * @param[in] wlm - the Work Load Manager whose active thread count should be
 *    returned.
 * 
 * @return The active thread count on @p wlm.
 */
int dnxWlmGetActiveThreads(DnxWlm * wlm);

/** Return the active job count on the specified Work Load Manager.
 * 
 * @param[in] wlm - the Work Load Manager whose active job count should be
 *    returned.
 * 
 * @return The active job count on @p wlm.
 */
int dnxWlmGetActiveJobs(DnxWlm * wlm);

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

