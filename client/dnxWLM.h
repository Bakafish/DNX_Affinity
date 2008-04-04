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

typedef struct { int unused; } DnxWlm;

int dnxWlmGetActiveThreads(DnxWlm * wlm);
int dnxWlmGetActiveJobs(DnxWlm * wlm);

int dnxWlmReconfigure(DnxWlm * wlm, DnxWlmCfgData * pcfg);

int dnxWlmCreate(DnxWlmCfgData * cfg, DnxWlm ** pwlm);
void dnxWlmDestroy(DnxWlm * wlm);

#endif   /* _DNXWLM_H_ */

