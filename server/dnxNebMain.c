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

/** Implements the main entry point for the Distributed Nagios eXecutor (DNX).
 *
 * Intercepts all service checks and dispatches them to distributed 
 * worker nodes.
 * 
 * @file dnxNebMain.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

/*!@defgroup DNX_SERVER_IMPL DNX NEB Module Implementation 
 * @defgroup DNX_SERVER_IFC  DNX NEB Module Interface
 */

#include "dnxNebMain.h"
#include "dnxConfig.h"
#include "dnxProtocol.h"
#include "dnxTransport.h"
#include "dnxXml.h"
#include "dnxQueue.h"
#include "dnxCollector.h"
#include "dnxDispatcher.h"
#include "dnxRegistrar.h"
#include "dnxTimer.h"
#include "dnxJobList.h"
#include "dnxLogging.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#else
# define VERSION "0.20"
#endif

#define DNX_VERSION  VERSION

#define DNX_EMBEDDED_SVC_OBJECT  1

// Specify event broker API version (required)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

static void *myHandle = NULL;    // Private NEB module handle
DnxGlobalData dnxGlobalData;     // Private module data

// External global Nagios variables
extern service *service_list;    // Nagios service list
extern int service_check_timeout;   // Nagios global service check timeout

static int dnxLoadConfig (char *ConfigFile, DnxGlobalData *gData);
static int verifyFacility (char *szFacility, int *nFacility);
static int dnxServerInit (void);
static int dnxServerDeInit (void);
static int ehProcessData (int event_type, void *data);
static int ehSvcCheck (int event_type, void *data);
static int dnxPostNewJob (DnxGlobalData *gData, nebstruct_service_check_data *ds, DnxNodeRequest *pNode);
static int initThreads (void);
static int releaseThreads (void);
static int initQueues (void);
static int releaseQueues (void);
static int initComm (void);
static int releaseComm (void);
static int launchScript (char *script);

/*--------------------------------------------------------------------------*/

/* this function gets called when the module is loaded by the event broker */
int nebmodule_init (int flags, char *args, nebmodule *handle)
{
   int ret;

   // Save a copy of our module handle
   myHandle = handle;

   // Announce our presence
   dnxSyslog(LOG_INFO, "dnxNebMain: DNX Server Module Version %s", DNX_VERSION);
   dnxSyslog(LOG_INFO, "dnxNebMain: Copyright (c) 2006-2007 Robert W. Ingraham");
   
   // The module args string should contain the fully-qualified path to the config file
   if (!args || !*args)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: DNX Configuration File missing from module argument");
      return ERROR;
   }

   // Load module configuration
   if ((ret = dnxLoadConfig(args, &dnxGlobalData)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: Failed to load configuration: %d", ret);
      return ERROR;
   }

   // Subscribe to Process Data call-backs in order to defer initialization
   //   until after Nagios validates its configuration and environment.
   if ((ret = neb_register_callback(NEBCALLBACK_PROCESS_DATA, myHandle, 0, ehProcessData)) != OK)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: Failed to register Process Data callback: %d", ret);
      return ERROR;
   }
   dnxSyslog(LOG_INFO, "dnxNebMain: Registered Process Data callback");

   // Write initialization completed message
   dnxSyslog(LOG_INFO, "dnxNebMain: Module initialization completed.");

   // Set our start time
   dnxGlobalData.tStart = time((time_t)0);

   return OK;
}

/*--------------------------------------------------------------------------*/

static int dnxLoadConfig (char *ConfigFile, DnxGlobalData *gData)
{
   int ret, err_no;

   // Initialize our module data
   memset(gData, 0, sizeof(DnxGlobalData));

   // Set default max concurrent number for node requests we will accept
   gData->maxNodeRequests = DNX_MAX_NODE_REQUESTS;
   gData->minServiceSlots = 1024;
   gData->expirePollInterval = 5;
   gData->dnxLogFacility = LOG_LOCAL7;

   // Initialize global data
   initGlobals();

   // Parse config file
   if ((ret = parseFile(ConfigFile)) != 0)
   {
      dnxSyslog(LOG_ERR, "getConfig: Failed to parse config file: %d", ret);
      return ret;
   }

   // Validate configuration items
   ret = DNX_ERR_INVALID;
   if (!gData->channelDispatcher)
      dnxSyslog(LOG_ERR, "getConfig: Missing channelDispatcher parameter");
   else if (!gData->channelCollector)
      dnxSyslog(LOG_ERR, "getConfig: Missing channelCollector parameter");
   else if (gData->maxNodeRequests < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid maxNodeRequests parameter");
   else if (gData->minServiceSlots < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid minServiceSlots parameter");
   else if (gData->expirePollInterval < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid expirePollInterval parameter");
   else if (gData->localCheckPattern &&   /* If the localCheckPattern is defined, then */
         (err_no = regcomp(&(gData->regEx), gData->localCheckPattern, (REG_EXTENDED | REG_NOSUB))) != 0) /* Compile the regex */
   {
      char buffer[128];
      regerror(err_no, &(gData->regEx), buffer, sizeof(buffer));
      dnxSyslog(LOG_ERR, "getConfig: Failed to compile localCheckPattern (\"%s\"): %s", gData->localCheckPattern, buffer);
      regfree(&(gData->regEx));
   }
   else if (gData->logFacility &&   /* If logFacility is defined, then */
         verifyFacility(gData->logFacility, &(gData->dnxLogFacility)) == -1)
   {
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for logFacility: %s", gData->logFacility);
   }
   else if (gData->auditWorkerJobs &&  /* If auditWorkerJobs is defined, then */
         verifyFacility(gData->auditWorkerJobs, &(gData->auditLogFacility)) == -1)
   {
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for auditWorkerJobs: %s", gData->auditWorkerJobs);
   }
   else
      ret = DNX_OK;

   return ret;
}

/*--------------------------------------------------------------------------*/

typedef struct _FacilityCodes_ {
   char *str;
   int val;
} FacilityCodes;

static FacilityCodes facCode[] = {
   { "LOG_LOCAL0",   LOG_LOCAL0 },
   { "LOG_LOCAL1",   LOG_LOCAL1 },
   { "LOG_LOCAL2",   LOG_LOCAL2 },
   { "LOG_LOCAL3",   LOG_LOCAL3 },
   { "LOG_LOCAL4",   LOG_LOCAL4 },
   { "LOG_LOCAL5",   LOG_LOCAL5 },
   { "LOG_LOCAL6",   LOG_LOCAL6 },
   { "LOG_LOCAL7",   LOG_LOCAL7 },
   { NULL, -1 }
};

static int verifyFacility (char *szFacility, int *nFacility)
{
   FacilityCodes *p;

   for (p = facCode; p->str && strcmp(szFacility, p->str); p++);

   return (*nFacility = p->val);
}

/*--------------------------------------------------------------------------*/

/* this function gets called when the module is unloaded by the event broker */
int nebmodule_deinit (int flags, int reason)
{
   dnxSyslog(LOG_INFO, "dnxNebMain: DNX Server shutdown initiated.");

   // Begin shutdown process
   dnxServerDeInit();

   // Write de-initialization completed message
   dnxSyslog(LOG_INFO, "dnxNebMain: Module de-initialization completed.");

   dnxGlobalData.isActive = 0;   // De-Init success

   return 0;
}

/*--------------------------------------------------------------------------*/

static int dnxServerInit (void)
{
   int ret;

   // Initialize the Job, Request and Pending Queues
   if ((ret = initQueues()) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServerInit: Failed to initialize queues: %d", ret);
      return ERROR;
   }

   // Initialize the communications stack
   if ((ret = initComm()) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServerInit: Failed to initialize communications: %d", ret);
      releaseQueues();
      return ERROR;
   }

   // Start all of the threads: Dispatcher, Collector, Registrar and Timer
   if ((ret = initThreads()) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServerInit: Failed to initialize threads: %d", ret);
      releaseComm();
      releaseQueues();
      return ERROR;
   }

   // Subscribe to the Service Check call-back type
   neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, myHandle, 0, ehSvcCheck);
   dnxSyslog(LOG_INFO, "dnxNebMain: Registered Service Check callback");

   dnxGlobalData.isActive = 1;   // Init success

   dnxSyslog(LOG_INFO, "dnxServerInit: Server initialization completed.");

   return OK;
}

/*--------------------------------------------------------------------------*/

static int dnxServerDeInit (void)
{
   /* deregister for all events we previously registered for... */
   neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, ehProcessData);
   neb_deregister_callback(NEBCALLBACK_SERVICE_CHECK_DATA, ehSvcCheck);

   // Remove all of our objects: Threads, sockets and Queues
   releaseThreads();
   releaseComm();
   releaseQueues();

   // If the localCheckPattern is defined, then release regex structure
   if (dnxGlobalData.localCheckPattern)
      regfree(&(dnxGlobalData.regEx));

   return OK;
}

/*--------------------------------------------------------------------------*/

// Process Data Event Handler
static int ehProcessData (int event_type, void *data)
{
   nebstruct_process_data *procdata = (nebstruct_process_data *)data;

   // Validate our event type
   if (event_type != NEBCALLBACK_PROCESS_DATA)
      return OK;  // Ignore all non-process-data events

   // Sanity-check our data structure
   if (procdata == NULL)
   {
      dnxSyslog(LOG_ERR, "ehProcessData: Received NULL process data structure");
      return ERROR;  // Should not happen - internal Nagios error
   }

   // Determine our sub-event type
   switch (procdata->type)
   {
   case NEBTYPE_PROCESS_EVENTLOOPSTART:   // Perform DNX init
      dnxDebug(2, "ehProcessData: Received Process Event Loop Start event");

      // Execute sync script, if defined
      if (dnxGlobalData.syncScript)
      {
         dnxDebug(1, "ehProcessData: Executing plugin sync script: %s", dnxGlobalData.syncScript);

         // NB: This halts Nagios execution until the script exits...
         launchScript(dnxGlobalData.syncScript);
      }

      // Initialize DNX Server resources and threads
      if (dnxServerInit() != OK)
         dnxServerDeInit();   // Encountered init error - shutdown DNX
      break;

   case NEBTYPE_PROCESS_EVENTLOOPEND:     // Perform DNX de-init
      dnxDebug(2, "ehProcessData: Received Process Event Loop End event");
   }

   return OK;
}

/*--------------------------------------------------------------------------*/

// Service Check Event Handler
static int ehSvcCheck (int event_type, void *data)
{
   nebstruct_service_check_data *svcdata = (nebstruct_service_check_data *)data;
   DnxNodeRequest *pNode;
   int ret;

   // Validate our event type
   if (event_type != NEBCALLBACK_SERVICE_CHECK_DATA)
      return OK;  // Ignore all non-service-check events

   // Sanity-check our data structure
   if (svcdata == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxServer: ehSvcCheck: Received NULL service data structure");
      return ERROR;  // Should not happen - internal Nagios error
   }

   // Only need to look at pre-run service checks
   if (svcdata->type != NEBTYPE_SERVICECHECK_INITIATE)
      return OK;  // Ignore non-initialization events

#if 0
   dnxDebug(5, "ehSvcCheck: Received Service Check Init event");
#endif

   // See if this job should be executed locally.
   //
   // We do this by seeing if the check-command string (svcdata->command_line)
   // matches the regular-expression specified in the localCheckPattern
   // directive in the Server configuration file.
   //
   if (regexec(&(dnxGlobalData.regEx), svcdata->command_line, 0, NULL, 0) == 0)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: Job will execute locally: %s", svcdata->command_line);
      return OK;  // Ignore check that should be executed locally
   }

   // Make sure we have at least one valid worker node request.
   // If not, execute check locally.

   dnxDebug(1, "dnxServer: ehSvcCheck: Received Job %lu at %lu (%lu)",
         dnxGlobalData.serialNo, (unsigned long)time(NULL), (unsigned long)(svcdata->start_time.tv_sec));

   // Locate the next available worker node from the Request Queue
   if ((ret = dnxGetNodeRequest(&dnxGlobalData, &pNode)) != DNX_OK)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: No worker nodes requests available: %d", ret);
      return OK;  // Unable to handle this request - Have Nagios handle it
   }

   // Post this service check to the Job Queue
   if ((ret = dnxPostNewJob(&dnxGlobalData, svcdata, pNode)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServer: ehSvcCheck: Failed to post new job: %d", ret);
      return OK;  // Unable to handle this request - Have Nagios handle it
   }

   // Increment service check serial number
   dnxGlobalData.serialNo++;

   // Tell Nagios that we are overriding the handling of this event
   return NEBERROR_CALLBACKOVERRIDE;
}

/*--------------------------------------------------------------------------*/

static int dnxPostNewJob (DnxGlobalData *gData, nebstruct_service_check_data *ds, DnxNodeRequest *pNode)
{
   service *svc;
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
   dnxMakeGuid(&(Job.guid), DNX_OBJ_JOB, gData->serialNo, 0);
   Job.svc        = svc;
   Job.cmd        = strdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   Job.expires    = Job.start_time + Job.timeout + 5; /* temporary till we have a config variable for it ... */
   Job.pNode      = pNode;

   dnxDebug(1, "DnxNebMain: Posting Job %lu: %s", gData->serialNo, Job.cmd);

   // Post to the Job Queue
   if ((ret = dnxJobListAdd(gData->JobList, &Job)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Failed to post Job \"%s\": %d", Job.cmd, ret);

   // Worker Audit Logging
   dnxAuditJob(&Job, "ASSIGN");

   return ret;
}

/*--------------------------------------------------------------------------*/

int dnxJobCleanup (DnxNewJob *pJob)
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
      //dnxDebug(10, "dnxJobCleanup: Free(pNode=%p)", pJob->pNode);
      if (pJob->pNode)
      {
         free(pJob->pNode);
         pJob->pNode = NULL;
      }
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int initThreads (void)
{
   int ret;

   // Create the starting condition synchronization variable
   pthread_mutex_init(&dnxGlobalData.tmGo, NULL);
   pthread_cond_init(&dnxGlobalData.tcGo, NULL);

   // Clear the ShowStart flag
   dnxGlobalData.isGo = 0;

   dnxDebug(1, "DnxNebMain: Starting threads...");

   // Create the Result Collector thread
   if ((ret = pthread_create(&dnxGlobalData.tCollector, NULL, dnxCollector, (void *)&dnxGlobalData)) != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
      dnxSyslog(LOG_ERR, "initThreads: Failed to create Collector thread: %d", ret);
      return DNX_ERR_THREAD;
      
   }

   // Create the Dispatcher thread
   if ((ret = pthread_create(&dnxGlobalData.tDispatcher, NULL, dnxDispatcher, (void *)&dnxGlobalData)) != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
      dnxSyslog(LOG_ERR, "initThreads: Failed to create Dispatcher thread: %d", ret);
      releaseThreads();    // Cancel prior threads
      return DNX_ERR_THREAD;
      
   }

   // Create the Registrar thread
   if ((ret = pthread_create(&dnxGlobalData.tRegistrar, NULL, dnxRegistrar, (void *)&dnxGlobalData)) != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
      dnxSyslog(LOG_ERR, "initThreads: Failed to create Registrar thread: %d", ret);
      releaseThreads();    // Cancel prior threads
      return DNX_ERR_THREAD;
      
   }

   // Create the Service Check Timer thread
   if ((ret = pthread_create(&dnxGlobalData.tTimer, NULL, dnxTimer, (void *)&dnxGlobalData)) != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
      dnxSyslog(LOG_ERR, "initThreads: Failed to create Timer thread: %d", ret);
      releaseThreads();    // Cancel prior threads
      return DNX_ERR_THREAD;
      
   }

   // Set the ShowStart flag
   dnxGlobalData.isGo = 1;

   // Signal all threads that it's show-time!
   pthread_mutex_lock(&dnxGlobalData.tmGo);
   ret = pthread_cond_broadcast(&dnxGlobalData.tcGo);
   pthread_mutex_unlock(&dnxGlobalData.tmGo);

   if (ret != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
      dnxSyslog(LOG_ERR, "initThreads: Failed to broadcast GO signal: %d", ret);
      releaseThreads();       // Cancel prior threads
      return DNX_ERR_THREAD;
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int releaseThreads (void)
{
   int ret;

   // Cancel all threads
   if (dnxGlobalData.tRegistrar && (ret = pthread_cancel(dnxGlobalData.tRegistrar)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tRegistrar) failed with ret = %d", ret);
   if (dnxGlobalData.tDispatcher && (ret = pthread_cancel(dnxGlobalData.tDispatcher)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tDispatcher) failed with ret = %d", ret);
   if (dnxGlobalData.tTimer && (ret = pthread_cancel(dnxGlobalData.tTimer)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tTimer) failed with ret = %d", ret);
   if (dnxGlobalData.tCollector && (ret = pthread_cancel(dnxGlobalData.tCollector)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tCollector) failed with ret = %d", ret);

   // Wait for all threads to exit
   if (dnxGlobalData.tRegistrar && (ret = pthread_join(dnxGlobalData.tRegistrar, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tRegistrar) failed with ret = %d", ret);
   if (dnxGlobalData.tDispatcher && (ret = pthread_join(dnxGlobalData.tDispatcher, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tDispatcher) failed with ret = %d", ret);
   if (dnxGlobalData.tTimer && (ret = pthread_join(dnxGlobalData.tTimer, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tTimer) failed with ret = %d", ret);
   if (dnxGlobalData.tCollector && (ret = pthread_join(dnxGlobalData.tCollector, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tCollector) failed with ret = %d", ret);

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int initQueues (void)
{
   service *temp_service;
   int total_services = 0;
   int ret;

   // Create the (merged) Job List: Contains both Pending and InProgress Jobs
   // Find the total number of defined services
   for (temp_service=service_list; temp_service; temp_service=temp_service->next)
      total_services++;

   if (total_services < 1)
   {
      total_services = 100;
      dnxSyslog(LOG_WARNING, "initQueues: No services defined!  Defaulting to 100 slots in the DNX Job Queue");
   }

   // Check for configuration maxNodeRequests override
   if (total_services < dnxGlobalData.maxNodeRequests)
   {
      dnxSyslog(LOG_WARNING, "initQueues: Overriding automatic service check slot count. Was %d, now is %d", total_services, dnxGlobalData.maxNodeRequests);
      total_services = dnxGlobalData.maxNodeRequests;
   }

   dnxSyslog(LOG_INFO, "initQueues: Allocating %d service request slots in the DNX Job Queue", total_services);

   dnxDebug(2, "DnxNebMain: Initializing Job List and Node Request Queue");

   // Create the DNX Job List (Contains Pending and InProgress jobs)
   if ((ret = dnxJobListInit(&(dnxGlobalData.JobList), total_services)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initQueues: Failed to initialize DNX Job List with %d slots", total_services);
      return DNX_ERR_MEMORY;
   }

   // Create the Worker Node Requests Queue (Worker Nodes wanting work)
   pthread_mutex_init(&(dnxGlobalData.tmReq), NULL);
   pthread_cond_init(&(dnxGlobalData.tcReq), NULL);
   if ((ret = dnxQueueInit(&(dnxGlobalData.qReq), &(dnxGlobalData.tmReq), &(dnxGlobalData.tcReq), total_services)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initQueues: Failed to init Request Queue: %d", ret);
      return ret;
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int releaseQueues (void)
{
   // Remove the Job List
   dnxJobListWhack(&(dnxGlobalData.JobList));
   
   // Remove the Worker Node Request Queue
   dnxQueueDelete(dnxGlobalData.qReq);
   pthread_mutex_destroy(&(dnxGlobalData.tmReq));
   pthread_cond_destroy(&(dnxGlobalData.tcReq));
   
   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int initComm (void)
{
   int ret;

   dnxDebug(2, "DnxNebMain: Creating Dispatch and Collector channels");

   dnxGlobalData.pDispatch = dnxGlobalData.pCollect = NULL;

   // Initialize the DNX comm stack
   if ((ret = dnxChanMapInit(NULL)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initComm: dnxChanMapInit failed: %d", ret);
      return ret;
   }

   // Create Dispatcher channel
   if ((ret = dnxChanMapAdd("Dispatch", dnxGlobalData.channelDispatcher)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initComm: dnxChanMapInit(Dispatch) failed: %d", ret);
      return ret;
   }

   // Create Collector channel
   if ((ret = dnxChanMapAdd("Collect", dnxGlobalData.channelCollector)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initComm: dnxChanMapInit(Collect) failed: %d", ret);
      return ret;
   }
   
   // Attempt to open the Dispatcher channel
   if ((ret = dnxConnect("Dispatch", &(dnxGlobalData.pDispatch), DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initComm: dnxConnect(Dispatch) failed: %d", ret);
      return ret;
   }
   
   // Attempt to open the Collector channel
   if ((ret = dnxConnect("Collect", &(dnxGlobalData.pCollect), DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "initComm: dnxConnect(Collect) failed: %d", ret);
      return ret;
   }

   if (dnxGlobalData.debug)
   {
      dnxChannelDebug(dnxGlobalData.pDispatch, dnxGlobalData.debug);
      dnxChannelDebug(dnxGlobalData.pCollect, dnxGlobalData.debug);
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int releaseComm (void)
{
   int ret;

   // Close the Collector channel
   if ((ret = dnxDisconnect(dnxGlobalData.pCollect)) != DNX_OK)
      dnxSyslog(LOG_ERR, "releaseComm: Failed to disconnect Collector channel: %d", ret);
   dnxGlobalData.pCollect = NULL;

   // Delete the Collector channel
   if ((ret = dnxChanMapDelete("Collect")) != DNX_OK)
      dnxSyslog(LOG_ERR, "releaseComm: Failed to delete Collector channel: %d", ret);

   // Close the Dispatcher channel
   if ((ret = dnxDisconnect(dnxGlobalData.pDispatch)) != DNX_OK)
      dnxSyslog(LOG_ERR, "releaseComm: Failed to disconnect Dispatcher channel: %d", ret);
   dnxGlobalData.pDispatch = NULL;

   // Delete the Dispatcher channel
   if ((ret = dnxChanMapDelete("Dispatch")) != DNX_OK)
      dnxSyslog(LOG_ERR, "releaseComm: Failed to delete Dispatcher channel: %d", ret);

   // Release the DNX comm stack
   if ((ret = dnxChanMapRelease()) != DNX_OK)
      dnxSyslog(LOG_ERR, "releaseComm: Failed to release DNX comm stack: %d", ret);

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int launchScript (char *script)
{
   int ret;

   /* Validate parameters */
   if (!script)
   {
      dnxSyslog(LOG_ERR, "launchScript: Invalid parameters");
      return DNX_ERR_INVALID;
   }

   // Exec the script
   if ((ret = system(script)) == -1)
   {
      dnxSyslog(LOG_ERR, "launchScript: Failed to exec script: %s", strerror(errno));
      ret = DNX_ERR_INVALID;
   }
   else
      ret = DNX_OK;

   // Display script return code
   dnxDebug(1, "launchScript: Sync script returned %d", WEXITSTATUS(ret));

   return ret; // This statement should not be reached...
}

/*--------------------------------------------------------------------------*/

int dnxAuditJob (DnxNewJob *pJob, char *action)
{
   struct sockaddr_in src_addr;
   in_addr_t addr;

   if (dnxGlobalData.auditWorkerJobs)
   {
      // Convert opaque Worker Node address to IPv4 address
      //
      // TODO: This conversion should take place in the dnxUdpRead function
      //       and the resultant address string stored in the DnxNewJob
      //       structure.  This would have two benefits:
      //
      //    1. Encapsulates conversion at the protocol level.
      //    2. Saves some time during logging.
      //
      memcpy(&src_addr, pJob->pNode->address, sizeof(src_addr));
      addr = ntohl(src_addr.sin_addr.s_addr);

      syslog((dnxGlobalData.auditLogFacility | LOG_INFO),
         "%s: Job %lu: Worker %u.%u.%u.%u-%lx: %s",
         action,
         pJob->guid.objSerial,
         (unsigned)((addr >> 24) & 0xff),
         (unsigned)((addr >> 16) & 0xff),
         (unsigned)((addr >>  8) & 0xff),
         (unsigned)( addr        & 0xff),
         pJob->pNode->guid.objSlot,
         pJob->cmd
         );
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

