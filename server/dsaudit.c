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

/** Implements the DNX job auditing functionality.
 * 
 * The DNX auditing subsytem accepts audit messages from the DNX NEB module
 * and inserts them into the audit log via syslog. 
 *
 * @file dsaudit.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */
#include "dsaudit.h"

#include "dsjoblist.h"

#include "dnxError.h"

#include <syslog.h>
#include <netinet/in.h>


static int auditingEnabled;   /*!< The current state of auditing.       */
static int auditingFacility;  /*!< The auditing syslog facility code.   */


/** Enable or disable job auditing.
 *
 * @param[in] enable - boolean flag indicating the desired state
 *    of job auditing.
 */
void dsEnableJobAuditing(int enable)
{
   auditingEnabled = enable? 1: 0;
}


/** Write an audit message to the audit log for a posted job.
 *
 * @param[in] pJob - the job to be logged.
 * @param[in] action - the action to be performed on the job.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dsAuditJob(DnxNewJob * pJob, char * action)
{
   if (auditingEnabled)
   {
      struct sockaddr_in src_addr;
      in_addr_t addr;

      /** @todo This conversion should take place in the dnxUdpRead function
       * and the resultant address string stored in the DnxNewJob structure.
       * This would have two benefits:
       *
       *    1. Encapsulates conversion at the protocol level.
       *    2. Saves some time during logging.
       */

      // Convert opaque Worker Node address to IPv4 address
      memcpy(&src_addr, pJob->pNode->address, sizeof(src_addr));
      addr = ntohl(src_addr.sin_addr.s_addr);

      syslog(auditingFacility | LOG_INFO,
         "%s: Job %lu: Worker %u.%u.%u.%u-%lx: %s",
         action, pJob->guid.objSerial,
         (unsigned)((addr >> 24) & 0xff),
         (unsigned)((addr >> 16) & 0xff),
         (unsigned)((addr >>  8) & 0xff),
         (unsigned)( addr        & 0xff),
         pJob->pNode->guid.objSlot, pJob->cmd);
   }
   return DNX_OK;
}


/** Initialize the auditing subsystem.
 *
 * @param[in] facility - the syslog facility value to use for the audit log.
 * @param[in] enabled - a boolean value representing the desired initial 
 *    state of job auditing (on or off).
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dsAuditInit(int facility, int enabled)
{
   auditingFacility = facility;
   auditingEnabled = enabled;
}


/** Clean up the auditing subsystem.
 */
void dsAuditExit(void)
{
}

/*-------------------------------------------------------------------------*/

