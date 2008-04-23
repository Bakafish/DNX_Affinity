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

/** Definitions and prototypes for main module functionality.
 *
 * @file dnxNebMain.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXNEBMAIN_H_
#define _DNXNEBMAIN_H_

#include "dnxJobList.h"

#include <time.h>

#define DNX_DISPATH_PORT   12480
#define DNX_COLLECT_PORT   12481
#define DNX_TCP_LISTEN     5

/** Post a completed service request to the Nagios service result buffer.
 * 
 * If @p early_timeout is true (1), then @p res_code will be reset to 
 * STATE_UNKNOWN, so you might as well pass zero for @p res_code.
 * 
 * @param[in] data - An opaque pointer to the nagios service structure and
 *    other relevant results data.
 * @param[in] start_time - the nagios service object start time.
 * @param[in] delta - the running time of the nagios service object in seconds.
 * @param[in] early_timeout - boolean; true means the job DID time out.
 * @param[in] res_code - the result code of this job.
 * @param[in] res_data - the resulting STDOUT output text of this job.
 * 
 * @return Zero on success, or a non-zero error code.
 */
int dnxPostResult(void * data, time_t start_time, unsigned delta, 
      int early_timeout, int res_code, char * res_data);

/** Release all resources associated with a job object.
 * 
 * @param[in] pJob - the job to be freed.
 */
void dnxJobCleanup(DnxNewJob * pJob);

/** Send an audit message to the dnx server audit log.
 * 
 * @param[in] pJob - the job to be audited.
 * @param[in] action - the audit action that we're logging.
 * 
 * @return Zero on success or a non-zero error value.
 */
int dnxAuditJob(DnxNewJob * pJob, char * action);

unsigned int getDnxAffinity(char * name);


#endif   /* _DNXNEBMAIN_H_ */

