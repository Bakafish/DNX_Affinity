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

/** Header file for global structures associated with the DNX Server.
 *
 * @file dnxNebMain.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXNEBMAIN_H_
#define _DNXNEBMAIN_H_

#ifndef NSCORE
#define NSCORE
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

#include <regex.h>

#include "dnxError.h"
#include "dnxJobList.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxQueue.h"	// For dnxQueue definition

#define DNX_DISPATH_PORT	12480
#define DNX_COLLECT_PORT	12481
#define DNX_TCP_LISTEN		5

#define DNX_MAX_NODE_REQUESTS	1024

typedef struct _DnxGlobalData_ {
	unsigned long serialNo;	// Number of service checks processed
	time_t tStart;			// Module start time

	pthread_cond_t tcGo;	// ShowStart condition variable
	pthread_mutex_t tmGo;	// ShowStart mutex
	int isGo;               // ShowStart flag: 0=No, 1=Yes

	DnxJobList *JobList;	// Master Job List

	pthread_cond_t tcReq;	// Request Queue condition variable
	pthread_mutex_t tmReq;	// Request Queue mutex
	DnxQueue *qReq;			// Registered Worker Node Requests

	pthread_t tDispatcher;	// Dispatcher thread ID
	pthread_t tRegistrar;	// Registrar thread ID
	pthread_t tCollector;	// Collector thread ID

	dnxChannel *pDispatch;	// Dispatch communications channel
	dnxChannel *pCollect;	// Collector communications channel

	regex_t regEx;			// Compiled Regular Expression structure

	int  dnxLogFacility;	// DNX syslog facility
	int  auditLogFacility;	// Worker Audit syslog facility

	// Configuration File properties
	char *channelDispatcher;
	char *channelCollector;
	char *authWorkerNodes;
	long  maxNodeRequests;	// Max number of node requests we will accept
	long  minServiceSlots;
	long  expirePollInterval;
	char *localCheckPattern;
	char *syncScript;
	char *logFacility;
	char *auditWorkerJobs;
	long  debug;

	int isActive;			// Boolean: Is this module active?
} DnxGlobalData;

int nebmodule_init(int flags, char *args, nebmodule *handle);
int nebmodule_deinit (int flags, int reason);
int dnxJobCleanup (DnxNewJob *pJob);
int dnxAuditJob (DnxNewJob *pJob, char *action);

#endif   /* _DNXNEBMAIN_H_ */
