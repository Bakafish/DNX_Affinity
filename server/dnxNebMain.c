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
#include "dnxDebug.h"
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
# define VERSION "<unknown>"
#endif

#define DNX_VERSION              VERSION
#define DNX_EMBEDDED_SVC_OBJECT  1
#define DNX_MAX_NODE_REQUESTS    1024

// specify event broker API version (required)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

// module static data
static DnxJobList * joblist;        // The master job list
static DnxRegistrar * registrar;    // The client node registrar.
static DnxDispatcher * dispatcher;  // The job list dispatcher.
static DnxCollector * collector;    // The job list results collector.
static time_t start_time;           // The module start time
static void * myHandle;             // Private NEB module handle
static regex_t regEx;               // Compiled regular expression structure

/** @todo These should be combined into config data. */
static int dnxLogFacility;          // DNX syslog facility
static int auditLogFacility;        // Worker audit syslog facility

// module GLOBAL data
DnxServerCfg cfg;                   // The GLOBAL server config parameters

//----------------------------------------------------------------------------

/** Returns the syslog facility code matching a specified facility string.
 * 
 * @param[in] szFacility - the facility string to be verfied.
 * @param[out] nFacility - the address of storage in which the matching 
 *    facility code should be returned.
 *
 * @return The facility code matching @p szFacility, or -1 on no match.
 */
static int verifyFacility(char * szFacility, int * nFacility)
{
   static struct FacCode { char * str; int val; } facodes[] = 
   {
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

   struct FacCode * p;
   
   for (p = facodes; p->str && strcmp(szFacility, p->str); p++);

   return *nFacility = p->val;
}

//----------------------------------------------------------------------------

/** Read and parse the dnxServer configuration file.
 * 
 * @param[in] ConfigFile - the configuration file to be read.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxLoadConfig(char * ConfigFile)
{
   int ret, err_no;

   // clear GLOBAL server configuration data structure
   memset(&cfg, 0, sizeof cfg);

   // Set default max concurrent number for node requests we will accept
   cfg.maxNodeRequests = DNX_MAX_NODE_REQUESTS;
   cfg.minServiceSlots = 1024;
   cfg.expirePollInterval = 5;

   // Initialize configuration sub-system global data
   initGlobals();

   // Parse config file
   if ((ret = parseFile(ConfigFile)) != 0)
   {
      dnxSyslog(LOG_ERR, "getConfig: Failed to parse config file with %d: %s", 
            ret, dnxErrorString(ret));
      return ret;
   }

   // Validate configuration items
   ret = DNX_ERR_INVALID;
   if (!cfg.channelDispatcher)
      dnxSyslog(LOG_ERR, "getConfig: Missing channelDispatcher parameter");
   else if (!cfg.channelCollector)
      dnxSyslog(LOG_ERR, "getConfig: Missing channelCollector parameter");
   else if (cfg.maxNodeRequests < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid maxNodeRequests parameter");
   else if (cfg.minServiceSlots < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid minServiceSlots parameter");
   else if (cfg.expirePollInterval < 1)
      dnxSyslog(LOG_ERR, "getConfig: Missing or invalid expirePollInterval parameter");
   else if (cfg.localCheckPattern   /* If the localCheckPattern is defined, then */
         && (err_no = regcomp(&regEx, cfg.localCheckPattern, 
            (REG_EXTENDED | REG_NOSUB))) != 0) /* Compile the regex */
   {
      char buffer[128];
      regerror(err_no, &regEx, buffer, sizeof(buffer));
      dnxSyslog(LOG_ERR, 
            "getConfig: Failed to compile localCheckPattern (\"%s\"): %s", 
            cfg.localCheckPattern, buffer);
      regfree(&regEx);
   }
   else if (cfg.logFacility &&   /* If logFacility is defined, then */
         verifyFacility(cfg.logFacility, &dnxLogFacility) == -1)
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for logFacility: %s", 
            cfg.logFacility);
   else if (cfg.auditWorkerJobs &&  /* If auditWorkerJobs is defined, then */
         verifyFacility(cfg.auditWorkerJobs, &auditLogFacility) == -1)
      dnxSyslog(LOG_ERR, 
            "getConfig: Invalid syslog facility for auditWorkerJobs: %s", 
            cfg.auditWorkerJobs);
   else
      ret = DNX_OK;

   return ret;
}

//----------------------------------------------------------------------------

/** Return the number of services configured in Nagios.
 *
 * @return The number of services configured in Nagios.
 * 
 * @todo This routine should be in nagios code. Add it to the dnx patch files
 * for nagios 2.7 and 2.9, and export it from nagios so we can call it.
 */
static int nagiosGetServiceCount(void)
{
   extern service * service_list;      // the global nagios service list

   service * temp_service;
   int total_services = 0;

   // walk the service list, count the nodes
   for (temp_service = service_list; temp_service; 
         temp_service = temp_service->next)
      total_services++;

   return total_services;
}

//----------------------------------------------------------------------------

/** Post a completed service request to the Nagios service result buffer.
 * 
 * @param[in] svc - the nagios service object from which results are taken.
 * @param[in] start_time - the nagios service object start time.
 * @param[in] early_timeout - boolean; true means the job DID time out.
 * @param[in] res_code - the result code of this job.
 * @param[in] res_data - the resulting STDOUT output text of this job.
 * 
 * @return Zero on success, or a non-zero error code.
 * 
 * @todo This routine should be in nagios code. Add it to the dnx patch files
 * for nagios 2.7 and 2.9, and export it from nagios so we can call it.
 */
int nagiosPostResult(service * svc, time_t start_time, 
      int early_timeout, int res_code, char * res_data)
{
   extern circular_buffer service_result_buffer;
   extern int check_result_buffer_slots;

   service_message * new_message;

   // note that we're using malloc, not xmalloc - nagios takes ownership
   if ((new_message = (service_message *)malloc(sizeof *new_message)) == 0)
      return DNX_ERR_MEMORY;

   gettimeofday(&new_message->finish_time, 0);
   strncpy(new_message->host_name, svc->host_name, 
         sizeof(new_message->host_name) - 1);
   new_message->host_name[sizeof(new_message->host_name) - 1] = 0;
   strncpy(new_message->description, svc->description, 
         sizeof(new_message->description) - 1);
   new_message->description[sizeof(new_message->description) - 1] = 0;
   new_message->return_code = res_code;
   new_message->exited_ok = TRUE;
   new_message->check_type = SERVICE_CHECK_ACTIVE;
   new_message->parallelized = svc->parallelize;
   new_message->start_time.tv_sec = start_time;
   new_message->start_time.tv_usec = 0L;
   new_message->early_timeout = early_timeout;
   strncpy(new_message->output, res_data, sizeof(new_message->output) - 1);
   new_message->output[sizeof(new_message->output) - 1] = 0;

   pthread_mutex_lock(&service_result_buffer.buffer_lock);

   // handle overflow conditions
   if (service_result_buffer.items == check_result_buffer_slots)
   {
      service_result_buffer.overflow++;
      service_result_buffer.tail = (service_result_buffer.tail + 1) 
            % check_result_buffer_slots;
   }

   // save the data to the buffer
   ((service_message **)service_result_buffer.buffer)
         [service_result_buffer.head] = new_message;

   // increment the head counter and items
   service_result_buffer.head = (service_result_buffer.head + 1) 
         % check_result_buffer_slots;
   if (service_result_buffer.items < check_result_buffer_slots)
      service_result_buffer.items++;
   if (service_result_buffer.items > service_result_buffer.high)
      service_result_buffer.high = service_result_buffer.items;

   pthread_mutex_unlock(&service_result_buffer.buffer_lock);

   return 0;
}

//----------------------------------------------------------------------------

/** Calculate the optimal size of the job list.
 *
 * Assumes the caller will actually use the returned value to allocate the 
 * job list. Based on this assumption, this routine logs messages indicating
 * when various configuration overrides have taken effect.
 * 
 * @return The calculated size of the job list.
 */
static int dnxCalculateJobListSize(void)
{
   int size = nagiosGetServiceCount();
 
   // zero doesn't make sense...
   if (size < 1)
   {
      size = 100;
      dnxSyslog(LOG_WARNING, "init: No Nagios services defined! Defaulting "
                             "to %d slots in the DNX job queue", size);
   }

   // check for configuration maxNodeRequests override
   if (size < cfg.maxNodeRequests)
   {
      dnxSyslog(LOG_WARNING, 
         "init: Overriding automatic service check slot count. Changing from %d to %d", 
         size, cfg.maxNodeRequests);
      size = cfg.maxNodeRequests;
   }
   return size;
}

//----------------------------------------------------------------------------

/** Post a new job from Nagios to the dnxServer job queue.
 * 
 * @param[in] joblist - the job list to which the new job should be posted.
 * @param[in] serial - the serial number of the new job.
 * @param[in] ds - a pointer to the nagios job that's being posted.
 * @param[in] pNode - a dnxClient node request structure that is being 
 *    posted with this job. The dispatcher thread will send the job to the
 *    associated node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxPostNewJob(DnxJobList * joblist, unsigned long serial, 
      nebstruct_service_check_data * ds, DnxNodeRequest * pNode)
{
   service * svc;
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
   dnxMakeGuid(&Job.guid, DNX_OBJ_JOB, serial, 0);
   Job.payload    = svc;
   Job.cmd        = xstrdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   Job.expires    = Job.start_time + Job.timeout + 5; /* temporary till we have a config variable for it ... */
   Job.pNode      = pNode;

   dnxDebug(1, "DnxNebMain: Posting Job %lu: %s", serial, Job.cmd);

   // Post to the Job Queue
   if ((ret = dnxJobListAdd(joblist, &Job)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Failed to post Job \"%s\": %d", 
            Job.cmd, ret);

   // Worker Audit Logging
   dnxAuditJob(&Job, "ASSIGN");

   return ret;
}

//----------------------------------------------------------------------------

/** Release all resources associated with a job object.
 * 
 * @param[in] pJob - the job to be freed.
 * 
 * @return Always returns zero.
 */
int dnxJobCleanup(DnxNewJob * pJob)
{
   if (pJob)
   {
      // Free the Pending Job command string
      if (pJob->cmd)
      {
         xfree(pJob->cmd);
         pJob->cmd = NULL;
      }

      // Free the node request message
      if (pJob->pNode)
      {
         xfree(pJob->pNode);
         pJob->pNode = NULL;
      }
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Send an audit message to the dnx server audit log.
 * 
 * @param[in] pJob - the job to be audited.
 * @param[in] action - the audit action that we're logging.
 * 
 * @return Always returns zero.
 */
int dnxAuditJob(DnxNewJob * pJob, char * action)
{
   struct sockaddr_in src_addr;
   in_addr_t addr;

   if (cfg.auditWorkerJobs)
   {
      // Convert opaque Worker Node address to IPv4 address

      /** @todo This conversion should take place in the dnxUdpRead function
       * and the resultant address string stored in the DnxNewJob
       * structure. This would have two benefits:
       * 
       *    1. Encapsulates conversion at the protocol level.
       *    2. Saves some time during logging.
       */
      memcpy(&src_addr, pJob->pNode->address, sizeof(src_addr));
      addr = ntohl(src_addr.sin_addr.s_addr);

      syslog(auditLogFacility | LOG_INFO,
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

//----------------------------------------------------------------------------

/** Service Check Event Handler.
 * 
 * @param[in] event_type - the event type for which we're being called.
 * @param[in] data - an opaque pointer to nagios event-specific data.
 * 
 * @return Zero if we want Nagios to handle the event; 
 *    NEBERROR_CALLBACKOVERRIDE indicates that we want to handle the event
 *    ourselves; any other non-zero value represents an error.
 */
static int ehSvcCheck(int event_type, void * data)
{
   static unsigned long serial = 0; // the number of service checks processed

   nebstruct_service_check_data * svcdata = (nebstruct_service_check_data *)data;
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

   dnxDebug(5, "ehSvcCheck: Received Service Check Init event");

   // See if this job should be executed locally.
   //
   // We do this by seeing if the check-command string (svcdata->command_line)
   // matches the regular-expression specified in the localCheckPattern
   // directive in the Server configuration file.
   //
   if (regexec(&regEx, svcdata->command_line, 0, NULL, 0) == 0)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: Job will execute locally: %s", 
            svcdata->command_line);
      return OK;  // Ignore check that should be executed locally
   }

   // Make sure we have at least one valid worker node request.
   // If not, execute check locally.

   dnxDebug(1, "dnxServer: ehSvcCheck: Received Job %lu at %lu (%lu)",
         serial, (unsigned long)time(NULL), (unsigned long)(svcdata->start_time.tv_sec));

   // Locate the next available worker node from the Request Queue
   if ((ret = dnxGetNodeRequest(registrar, &pNode)) != DNX_OK)
   {
      dnxDebug(1, "dnxServer: ehSvcCheck: No worker nodes requests available: %d", ret);
      return OK;  // Unable to handle this request - Have Nagios handle it
   }

   // Post this service check to the Job Queue
   if ((ret = dnxPostNewJob(joblist, serial, svcdata, pNode)) != 0)
   {
      dnxSyslog(LOG_ERR, 
            "dnxServer: ehSvcCheck: Failed to post new job with %d: %s", 
            ret, dnxErrorString(ret));
      return OK;  // Unable to handle this request - Have Nagios handle it
   }

   serial++;

   // tell Nagios that we are overriding the handling of this event
   return NEBERROR_CALLBACKOVERRIDE;
}

//----------------------------------------------------------------------------

// forward declaration due to circular reference
static int ehProcessData(int event_type, void * data);

/** Deinitialize the dnx server.
 * 
 * @return Always returns zero.
 */
static int dnxServerDeInit(void)
{
   // deregister for all nagios events we previously registered for...
   neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, ehProcessData);
   neb_deregister_callback(NEBCALLBACK_SERVICE_CHECK_DATA, ehSvcCheck);

   // ensure we don't destroy non-existent objects from here on out...
   if (registrar)
      dnxRegistrarDestroy(registrar);

   if (dispatcher)
      dnxDispatcherDestroy(dispatcher);

   if (collector)
      dnxCollectorDestroy(collector);

   if (joblist)
      dnxJobListDestroy(joblist);

   if (cfg.localCheckPattern)
      regfree(&regEx);

   // it doesn't matter if we haven't initialized the
   // channel map - it can figure that out for itself
   dnxChanMapRelease();

   return OK;
}

//----------------------------------------------------------------------------

/** Initialize the dnxServer.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxServerInit(void)
{
   int ret, joblistsz;

   // clear globals so we know what to "undo" as we back out
   joblist = 0;
   registrar = 0;
   dispatcher = 0;
   collector = 0;

   if ((ret = dnxChanMapInit(0)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxServerInit: dnxChanMapInit failed with %d: %s", 
            ret, dnxErrorString(ret));
      return ret;
   }

   joblistsz = dnxCalculateJobListSize();

   dnxSyslog(LOG_INFO, 
         "dnxServerInit: Allocating %d service request slots in the DNX job list", 
         joblistsz);

   if ((ret = dnxJobListCreate(joblistsz, &joblist)) != 0)
   {
      dnxSyslog(LOG_ERR, 
            "dnxServerInit: Failed to initialize DNX job list with %d slots", 
            joblistsz);
      return ret;
   }

   if ((ret = dnxCollectorCreate(&cfg.debug, "Collect", 
         cfg.channelCollector, joblist, &collector)) != 0)
      return ret;

   if ((ret = dnxDispatcherCreate(&cfg.debug, "Dispatch", 
         cfg.channelDispatcher, joblist, &dispatcher)) != 0)
      return ret;

   if ((ret = dnxRegistrarCreate(&cfg.debug, joblistsz,
         dnxDispatcherGetChannel(dispatcher), &registrar)) != 0)
      return ret;

   // registration for this event starts everything rolling
   neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, myHandle, 0, ehSvcCheck);

   dnxSyslog(LOG_INFO, "dnxServerInit: Registered for SERVICE_CHECK_DATA event");
   dnxSyslog(LOG_INFO, "dnxServerInit: Server initialization completed");

   return 0;
}

//----------------------------------------------------------------------------

/** Launches an external command and waits for it to return a status code.
 * 
 * @param[in] script - the command line to be launched.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int launchScript(char * script)
{
   int ret;

   assert(script);

   // exec the script - system waits till child completes
   if ((ret = system(script)) == -1)
   {
      dnxSyslog(LOG_ERR, "launchScript: Failed to exec script with %d: %s", 
            errno, strerror(errno));
      ret = DNX_ERR_INVALID;
   }
   else
      ret = DNX_OK;

   dnxSyslog(LOG_INFO, "launchScript: Sync script returned %d", 
         WEXITSTATUS(ret));

   return ret;
}

//----------------------------------------------------------------------------

/** Process Data Event Handler.
 * 
 * @param[in] event_type - the event regarding which we were called by Nagios.
 * @param[in] data - an opaque pointer to an event-specific data structure.
 * 
 * @return Zero if all is okay, but we want nagios to handle this event;
 *    non-zero if there's a problem of some sort.
 */
static int ehProcessData(int event_type, void * data)
{
   nebstruct_process_data *procdata = (nebstruct_process_data *)data;

   // validate our event type - ignore wrong event type
   assert(event_type == NEBCALLBACK_PROCESS_DATA);
   if (event_type != NEBCALLBACK_PROCESS_DATA)
      return OK;

   // sanity-check our data structure - should never happen
   assert(procdata);
   if (!procdata)
   {
      dnxSyslog(LOG_ERR, "ehProcessData: Received NULL process data structure");
      return ERROR;
   }

   // look for process event loop start event
   if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPSTART)
   {
      dnxDebug(2, "ehProcessData: Received PROCESS_EVENTLOOPSTART event");

      // execute sync script, if defined
      if (cfg.syncScript)
      {
         dnxSyslog(LOG_INFO, "ehProcessData: Executing plugin sync script: %s", 
               cfg.syncScript);

         // NB: This halts Nagios execution until the script exits...
         launchScript(cfg.syncScript);
      }

      // if server init fails, do server shutdown
      if (dnxServerInit() != 0)
         dnxServerDeInit();
   }
   return OK;
}

//----------------------------------------------------------------------------

/** The main NEB module deinitialization routine.
 * 
 * This function gets called when the module is unloaded by the event broker.
 * 
 * @param[in] flags - nagios NEB module flags - not used.
 * @param[in] reason - nagios reason code - not used.
 *
 * @return Always returns zero.
 */
int nebmodule_deinit(int flags, int reason)
{
   dnxSyslog(LOG_INFO, "dnxServer: DNX Server shutdown initiated.");
   dnxServerDeInit();
   dnxSyslog(LOG_INFO, "dnxServer: DNX Server shutdown completed.");

   xheapchk();

   return 0;
}

//----------------------------------------------------------------------------

/** The main NEB module initialization routine.
 * 
 * This function gets called when the module is loaded by the event broker.
 * 
 * @param[in] flags - module flags - not used
 * @param[in] args - module arguments. These come from the nagios 
 *    configuration file, and are passed through to the module as it loads.
 * @param[in] handle - our module handle - passed from the OS to nagios as
 *    nagios loaded us.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int nebmodule_init(int flags, char * args, nebmodule * handle)
{
   int ret;

   myHandle = handle;

   dnxSyslog(LOG_INFO, "dnxServer: DNX Server module version %s", DNX_VERSION);
   dnxSyslog(LOG_INFO, "dnxServer: Copyright (c) 2006-2007 "
                       "Intellectual Reserve. All rights reserved.");
   
   // module args string should contain a fully-qualified config file path
   if (!args || !*args)
   {
      dnxSyslog(LOG_ERR, "dnxServer: DNX config file not specified");
      return ERROR;
   }

   dnxLogFacility = LOG_LOCAL7;
   auditLogFacility = 0;

   memset(&regEx, 0, sizeof regEx);

   if ((ret = dnxLoadConfig(args)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Failed to load configuration: %d", ret);
      return ERROR;
   }

   cfgServerLogging(&cfg.debug, &dnxLogFacility);

   // subscribe to PROCESS_DATA call-backs in order to defer initialization
   //    until after Nagios validates its configuration and environment.
   if ((ret = neb_register_callback(NEBCALLBACK_PROCESS_DATA, 
         myHandle, 0, ehProcessData)) != OK)
   {
      dnxSyslog(LOG_ERR, 
            "dnxServer: PROCESS_DATA event registration failed with %d: %s", 
            ret, dnxErrorString(ret));
      return ERROR;
   }

   dnxSyslog(LOG_INFO, "dnxServer: Registered for PROCESS_DATA event");
   dnxSyslog(LOG_INFO, "dnxServer: Module initialization completed");

   start_time = time(0);

   return OK;
}

/*--------------------------------------------------------------------------*/

