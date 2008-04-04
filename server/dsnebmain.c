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

/** Intercepts service checks and dispatches them to distributed worker nodes.
 *
 * @file dnxNebMain.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include "dsnebmain.h"

#include "dsqueue.h"
#include "dscollector.h"
#include "dsdispatcher.h"
#include "dstimer.h"
#include "dsjoblist.h"
#include "dsconfig.h"

#include "dnxProtocol.h"
#include "dnxTransport.h"
#include "dnxXml.h"
#include "dnxRegistrar.h"
#include "dnxLogging.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#else
# define VERSION "0.20"
#endif

#define DNX_VERSION VERSION
#define DNX_EMBEDDED_SVC_OBJECT 1

// specify event broker API version (required by Nagios)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

static void * myHandle = NULL;      // Private NEB module handle
DnxGlobalData dnxGlobalData;        // Private module data


/** Verify a facility string, and return the corresponding code.
 * 
 * @param[in] szFacility - a string representation of a log facility code.
 * @param[out] nFacility - the address of storage for the returned code.
 * 
 * @return The facility code that matches @p szFacility, or -1 if there are 
 * no matching codes for the specified string.
 */
static int verifyFacility(char * szFacility, int * nFacility)
{
   static struct FacilityCodes { char * str; int val; } * p, facCode[] = 
   {
      { "LOG_LOCAL0",   LOG_LOCAL0 },
      { "LOG_LOCAL1",   LOG_LOCAL1 },
      { "LOG_LOCAL2",   LOG_LOCAL2 },
      { "LOG_LOCAL3",   LOG_LOCAL3 },
      { "LOG_LOCAL4",   LOG_LOCAL4 },
      { "LOG_LOCAL5",   LOG_LOCAL5 },
      { "LOG_LOCAL6",   LOG_LOCAL6 },
      { "LOG_LOCAL7",   LOG_LOCAL7 },
      { NULL, -1 },
   };

   for (p = facCode; p->str && strcmp(szFacility, p->str); p++);

   return *nFacility = p->val;
}

/** Read DNX configuration parameters from a file.
 * 
 * @param[in] ConfigFile - the path and name of the DNX configuration file.
 * @param[in] gData - the DNX global data structure.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxLoadConfig(char * ConfigFile, DnxGlobalData * gData)
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
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for logFacility: %s", gData->logFacility);
   else if (gData->auditWorkerJobs &&  /* If auditWorkerJobs is defined, then */
         verifyFacility(gData->auditWorkerJobs, &(gData->auditLogFacility)) == -1)
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for auditWorkerJobs: %s", gData->auditWorkerJobs);
   else
      ret = DNX_OK;

   return ret;
}


/** Initialize server threads.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initThreads(void)
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

   if ((ret = dnxTimerInit(dnxGlobalData.JobList)) != 0)
   {
      dnxGlobalData.isActive = 0;   // Init failure
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


/** Clean up server threads.
 */
static void releaseThreads (void)
{
   int ret;

   dnxTimerExit();

   // Cancel all threads
   if (dnxGlobalData.tRegistrar && (ret = pthread_cancel(dnxGlobalData.tRegistrar)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tRegistrar) failed with ret = %d", ret);
   if (dnxGlobalData.tDispatcher && (ret = pthread_cancel(dnxGlobalData.tDispatcher)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tDispatcher) failed with ret = %d", ret);
   if (dnxGlobalData.tCollector && (ret = pthread_cancel(dnxGlobalData.tCollector)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_cancel(tCollector) failed with ret = %d", ret);

   // Wait for all threads to exit
   if (dnxGlobalData.tRegistrar && (ret = pthread_join(dnxGlobalData.tRegistrar, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tRegistrar) failed with ret = %d", ret);
   if (dnxGlobalData.tDispatcher && (ret = pthread_join(dnxGlobalData.tDispatcher, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tDispatcher) failed with ret = %d", ret);
   if (dnxGlobalData.tCollector && (ret = pthread_join(dnxGlobalData.tCollector, NULL)) != 0)
      dnxSyslog(LOG_ERR, "releaseThreads: pthread_join(tCollector) failed with ret = %d", ret);
}


/** Initialize server job queue.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initQueues (void)
{
   extern service * service_list;      // Nagios service list

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


/** Clean up server job queues.
 */
static void releaseQueues(void)
{
   dnxJobListExit(&dnxGlobalData.JobList);
   
   // Remove the Worker Node Request Queue
   dnxQueueDelete(dnxGlobalData.qReq);
   pthread_mutex_destroy(&dnxGlobalData.tmReq);
   pthread_cond_destroy(&dnxGlobalData.tcReq);
}


/** Initialize client/server communications.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initComm(void)
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


/** Clean up client/server communications.
 */
static void releaseComm (void)
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
}


/** Nagios Service Check event handler for the DNX module.
 * 
 * This handler is called for each job that Nagios is about to run.
 * 
 * @param[in] event_type - the type of event being passed to us.
 * @param[in] data - a pointer to a nebstruct_service_check_data structure
 *    containing information about this event.
 * 
 * @return Returns NEBERROR_CALLBACKOVERRIDE if we want to handle the event,
 *    zero (OK) if we want to pass it on to Nagios, or some other non-zero 
 *    value to indicate an error condition.
 */
static int ehSvcCheck(int event_type, void * data)
{
   nebstruct_service_check_data * svcdata = (nebstruct_service_check_data *)data;
   DnxNodeRequest * pNode;
   int ret;

   // validate our event type - in case nagios has a bug.
   assert(event_type == NEBCALLBACK_SERVICE_CHECK_DATA);
   if (event_type != NEBCALLBACK_SERVICE_CHECK_DATA)
      return ERROR;

   // validate our data structure - in case nagios has a bug.
   assert(svcdata != NULL);
   if (svcdata == NULL)
      return ERROR;

   // ignore non-initialization events
   if (svcdata->type != NEBTYPE_SERVICECHECK_INITIATE)
      return OK;

   dnxDebug(5, "ehSvcCheck: Received Service Check Init event");

   // see if this job should be executed locally.
   if (regexec(&(dnxGlobalData.regEx), svcdata->command_line, 0, NULL, 0) == 0)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: Job will execute locally: %s", svcdata->command_line);
      return OK;  // ignore check that should be executed locally
   }

   dnxDebug(1, "dnxServer: ehSvcCheck: Received Job %lu at %lu (%lu)",
      dnxGlobalData.serialNo, (unsigned long)time(0), 
      (unsigned long)(svcdata->start_time.tv_sec));

   // locate the next available worker node from the Request queue
   if ((ret = dnxGetNodeRequest(&dnxGlobalData, &pNode)) != DNX_OK)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: No worker nodes requests available: %d", ret);
      return OK;  // can't handle this request - have Nagios handle it
   }

   // post this service check to the Job queue
   if ((ret = dnxPostNewJob(dnxGlobalData.JobList, 
      dnxGlobalData.serialNo, svcdata, pNode)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServer: ehSvcCheck: Failed to post new job: %d", ret);
      return OK;  // can't handle this request - have Nagios handle it
   }
   
   dnxGlobalData.serialNo++;  // Increment service check serial number

   // tell Nagios that we are overriding the handling of this event
   return NEBERROR_CALLBACKOVERRIDE;
}


/** NEB module shutdown routine.
 * 
 * This routine is called from the main process event handler during the 
 * Nagios shutdown event. It deregisters for all registered events.
 * 
 * @return Zero on success, or a non-zero error value. This routine only
 * returns zero.
 */
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


/** Complete initialization of the DNX NEB module.
 * 
 * This routine finished the initialization process begun by the ehProcessData
 * Nagios event handler. The ehProcessData event handler calls this routine
 * during the EVENTLOOPSTART event. This routine then does all of the complex
 * initialization of data structures and threads for the DNX NEB module.
 * 
 * @return Zero on success, or a non-zero error value.
 */
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


/** Launches a script designed to synchronize DNX plugins on worker nodes.
 * 
 * The script may of course do anything, but its intended purpose is to copy
 * updated or modified "check" modules down to worker nodes before allowing 
 * Nagios initialization to continue. This gives us a way of ensuring that 
 * worker nodes are in a known state before beginning to send them work.
 * 
 * @param[in] script - the OS path and name of the script to execute.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note The use of this routine by the Process Data event handler halts
 * Nagios initialization until after the script completes, allowing DNX
 * the opportunity to perform any sort of tasks that need to be done each
 * time Nagios starts up.
 */
static int launchSyncScript (char * script)
{
   int rv, ret = DNX_OK;    // assume success

   // system() is like fork, execl, waitpid...
   if ((rv = system(script)) == -1)
   {
      dnxSyslog(LOG_ERR, "launchSyncScript: Failed to execute script: %s", 
         strerror(errno));
      ret = DNX_ERR_INVALID;
   }

   dnxDebug(1, "launchSyncScript: Sync script returned %d", WEXITSTATUS(rv));

   return ret;
}


/** Nagios main Process event handler.
 * 
 * This routine is called by Nagios during Nagios NEBCALLBACK_PROCESS_DATA 
 * events. This event handler only handles the EVENTLOOPSTART and EVENTLOOPEND 
 * events. The EVENTLOOPSTART event is sent to all process-data handlers when 
 * Nagios begins its main event loop. The EVENTLOOPEND event is sent by Nagios 
 * at the time it exits its main event loop; this only happens when Nagios is 
 * about to shut down.
 * 
 * @param[in] event_type - the type of event this handler is being sent.
 * @param[in] data - a pointer to a structure whose type is defined by the
 *    value of @p event_type.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int ehProcessData(int event_type, void * data)
{
   nebstruct_process_data * procdata = (nebstruct_process_data *)data;

   // validate our event type - shouldn't happen
   assert(event_type == NEBCALLBACK_PROCESS_DATA);
   if (event_type != NEBCALLBACK_PROCESS_DATA)
      return ERROR;

   // validate our data structure - shouldn't happen
   assert(procdata != NULL);
   if (procdata == NULL)
      return ERROR;

   // look for the process data event loop start sub-event
   if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPSTART)
   {
      dnxDebug(2, "ehProcessData: Received Process Event Loop Start event");
   
      // execute sync script if defined
      if (dnxGlobalData.syncScript)
      {
         dnxDebug(1, "ehProcessData: Executing plugin sync script: %s", 
            dnxGlobalData.syncScript);
         launchSyncScript(dnxGlobalData.syncScript);
      }

      // initialize DNX
      if (dnxServerInit() != OK)
         dnxServerDeInit();   // Encountered init error - shutdown DNX
   }
   return OK;
}


/** Main module INITIALIZATION entry point for a NEB module.
 * 
 * This function gets called when the module is loaded by the Nagios event 
 * broker.
 * 
 * Reads the dnx configuration file for startup parameters, configures DNX 
 * services, and registers event handlers for Nagios events.
 * 
 * @param[in] flags - NEB module flags - see Nagios documentation.
 * @param[in] args - an array of arguments passed from the Nagios module
 *    configuration system. Arguments may be specified on a per-module basis
 *    by system administrators configuring Nagios to load a given module.
 * @param[in] handle - the library handle given to Nagios by the OS when 
 *    Nagios loads the library.
 * 
 * @return Zero on success, or a non-zero error value. If a NEB module returns
 * any value except zero, Nagios will unload the library.
 */
int nebmodule_init(int flags, char * args, nebmodule * handle)
{
   int ret;

   myHandle = handle;   // save a copy of our module handle

   // Announce our presence
   dnxSyslog(LOG_INFO, "dnxNebMain: DNX Server Module Version %s", DNX_VERSION);
   dnxSyslog(LOG_INFO, "dnxNebMain: Copyright (c) 2006-2007 Intellectual Reserve");

   /** @todo Define default values for all initialization parameters, set them
    * up before reading the configuration file. Then if we have a config file
    * read it and override the defaults.
    */

   // The module args string should contain the fully-qualified path to the config file
   if (!args || !*args)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: DNX Configuration File missing from module argument");
      return ERROR;
   }

   if ((ret = dnxLoadConfig(args, &dnxGlobalData)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: Failed to load configuration: %d", ret);
      return ERROR;
   }

   // Subscribe to Process Data call-backs in order to defer initialization
   //    until after Nagios validates its configuration and environment.
   if ((ret = neb_register_callback(NEBCALLBACK_PROCESS_DATA, myHandle, 0, ehProcessData)) != OK)
   {
      dnxSyslog(LOG_ERR, "dnxNebMain: Failed to register Process Data callback: %d", ret);
      return ERROR;
   }

   dnxSyslog(LOG_INFO, "dnxNebMain: Registered Process Data callback.");
   dnxSyslog(LOG_INFO, "dnxNebMain: Module initialization completed.");

   dnxGlobalData.tStart = time(0);     // Set our start time

   return OK;
}

/** Main module TERMINATION entry point for a NEB module.
 * 
 * This function gets called when the module is unloaded by the Nagios event 
 * broker.
 * 
 * Calls dnxServerDeInit to do the bulk of DNX NEB module de-initialization.
 * 
 * @param[in] flags - NEB module flags - see Nagios documentation.
 * @param[in] reason - NEB module reason code - see Nagios documentation.
 * 
 * @return Zero on success, or a non-zero error value. This routine only
 * returns success.
 */
int nebmodule_deinit (int flags, int reason)
{
   dnxSyslog(LOG_INFO, "dnxNebMain: DNX Server shutdown initiated.");

   dnxServerDeInit();

   dnxSyslog(LOG_INFO, "dnxNebMain: Module de-initialization completed.");

   dnxGlobalData.isActive = 0;

   return 0;
}

/*-------------------------------------------------------------------------*/

