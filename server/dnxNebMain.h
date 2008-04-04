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

#include "dnxTimer.h"
#include "dnxError.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxRegistrar.h"
#include "dnxDispatcher.h"
#include "dnxCollector.h"

#ifndef NSCORE
# define NSCORE
#endif

/* include (minimum required) event broker header files */
#include "nebmodules.h"
#include "nebcallbacks.h"

/* include other event broker header files that we need for our work */
#include "nebstructs.h"
#include "neberrors.h"
#include "broker.h"

/* include some Nagios stuff as well */
#include "config.h"
#include "common.h"
#include "nagios.h"
#include "objects.h"

#include <time.h>
#include <regex.h>
#include <pthread.h>

#define DNX_DISPATH_PORT   12480
#define DNX_COLLECT_PORT   12481
#define DNX_TCP_LISTEN     5

typedef struct DnxServerCfg_
{
   char * channelDispatcher;
   char * channelCollector;
   char * authWorkerNodes;
   long  maxNodeRequests;  // Maximum number of node requests we will accept
   long  minServiceSlots;
   long  expirePollInterval;
   char * localCheckPattern;
   char * syncScript;
   char * logFacility;
   char * auditWorkerJobs;
   long debug;
} DnxServerCfg;

int nebmodule_init(int flags, char * args, nebmodule * handle);
int nebmodule_deinit(int flags, int reason);
int dnxJobCleanup(DnxNewJob * pJob);
int dnxAuditJob(DnxNewJob * pJob, char * action);

#endif   /* _DNXNEBMAIN_H_ */

