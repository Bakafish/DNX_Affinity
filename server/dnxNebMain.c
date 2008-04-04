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

#include "dnxCfgParser.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"
#include "dnxCollector.h"
#include "dnxDispatcher.h"
#include "dnxRegistrar.h"
#include "dnxJobList.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION "<unknown>"
#endif

#ifndef SYSCONFDIR
# define SYSCONFDIR "/etc"
#endif

#ifndef NSCORE
# define NSCORE
#endif
#include "nagios.h"
#include "nebmodules.h"
#include "nebstructs.h"
#include "nebcallbacks.h"
#include "neberrors.h"
#include "broker.h"

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_VERSION                    VERSION

#define DNX_EMBEDDED_SVC_OBJECT        1

// default configuration values
#define DNX_DEFAULT_SERVER_CONFIG_FILE SYSCONFDIR "/dnxServer.cfg"
#define DNX_DEFAULT_MAX_NODE_REQUESTS  0x7FFFFFFF
#define DNX_DEFAULT_MIN_SERVICE_SLOTS  100
#define DNX_DEFAULT_EXPIRE_POLL_INT    5
#define DNX_DEFAULT_LOG_FACILITY       "LOG_LOCAL7"

// specify event broker API version (required)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/** The internal server module configuration data structure. */
typedef struct DnxServerCfg
{
   char * dispatcherUrl;
   char * collectorUrl;
   char * authWorkerNodes;
   unsigned maxNodeRequests;  // Maximum number of node requests we will accept
   unsigned minServiceSlots;  // Minimum number of node requests we will accept
   unsigned expirePollInterval;
   char * localCheckPattern;
   char * syncScript;
   char * logFacility;
   char * auditWorkerJobs;
   unsigned debug;
} DnxServerCfg;

// module static data
static DnxJobList * joblist;        // The master job list
static DnxRegistrar * registrar;    // The client node registrar.
static DnxDispatcher * dispatcher;  // The job list dispatcher.
static DnxCollector * collector;    // The job list results collector.
static time_t start_time;           // The module start time
static void * myHandle;             // Private NEB module handle
static regex_t regEx;               // Compiled regular expression structure
static DnxServerCfg cfg;            // The server configuration parameters
static DnxCfgParser * cfgParser;    // The configuration file parser. 

/** @todo These should be combined into config data. */
static int dnxLogFacility;          // DNX syslog facility
static int auditLogFacility;        // Worker audit syslog facility

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

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
      { 0, -1 }
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
static int initConfig(char * ConfigFile)
{
   static DnxCfgDictionary dict[] = 
   {
      {"channelDispatcher",   DNX_CFG_URL,      &cfg.dispatcherUrl      },
      {"channelCollector",    DNX_CFG_URL,      &cfg.collectorUrl       },
      {"authWorkerNodes",     DNX_CFG_STRING,   &cfg.authWorkerNodes    },
      {"maxNodeRequests",     DNX_CFG_UNSIGNED, &cfg.maxNodeRequests    },
      {"minServiceSlots",     DNX_CFG_UNSIGNED, &cfg.minServiceSlots    },
      {"expirePollInterval",  DNX_CFG_UNSIGNED, &cfg.expirePollInterval },
      {"localCheckPattern",   DNX_CFG_STRING,   &cfg.localCheckPattern  },
      {"syncScript",          DNX_CFG_FSPATH,   &cfg.syncScript         },
      {"logFacility",         DNX_CFG_STRING,   &cfg.logFacility        },
      {"auditWorkerJobs",     DNX_CFG_STRING,   &cfg.auditWorkerJobs    },
      {"debug",               DNX_CFG_UNSIGNED, &cfg.debug              },
   };
   int ret;

   // set configuration defaults - don't forget to allocate strings
   memset(&cfg, 0, sizeof cfg);
   cfg.logFacility         = xstrdup(DNX_DEFAULT_LOG_FACILITY);
   cfg.maxNodeRequests     = DNX_DEFAULT_MAX_NODE_REQUESTS;
   cfg.minServiceSlots     = DNX_DEFAULT_MIN_SERVICE_SLOTS;
   cfg.expirePollInterval  = DNX_DEFAULT_EXPIRE_POLL_INT;

   if ((ret = dnxCfgParserCreate(ConfigFile, 
         dict, elemcount(dict), 0, 0, &cfgParser)) != 0)
      return ret;

   if ((ret = dnxCfgParserParse(cfgParser)) == 0)
   {
      int err;

      // validate configuration items in context
      ret = DNX_ERR_INVALID;
      if (!cfg.dispatcherUrl)
         dnxSyslog(LOG_ERR, "config: Missing channelDispatcher parameter");
      else if (!cfg.collectorUrl)
         dnxSyslog(LOG_ERR, "config: Missing channelCollector parameter");
      else if (cfg.maxNodeRequests < 1)
         dnxSyslog(LOG_ERR, "config: Invalid maxNodeRequests parameter");
      else if (cfg.minServiceSlots < 1)
         dnxSyslog(LOG_ERR, "config: Invalid minServiceSlots parameter");
      else if (cfg.expirePollInterval < 1)
         dnxSyslog(LOG_ERR, "config: Invalid expirePollInterval parameter");
      else if (cfg.localCheckPattern
            && (err = regcomp(&regEx, cfg.localCheckPattern, 
                  REG_EXTENDED | REG_NOSUB)) != 0)
      {
         char buffer[128];
         regerror(err, &regEx, buffer, sizeof(buffer));
         dnxSyslog(LOG_ERR, "config: Failed to compile localCheckPattern (\"%s\"): %s", 
               cfg.localCheckPattern, buffer);
         regfree(&regEx);
      }
      else if (cfg.logFacility 
               && verifyFacility(cfg.logFacility, &dnxLogFacility) == -1)
         dnxSyslog(LOG_ERR, "config: Invalid syslog facility: %s", 
               cfg.logFacility);
      else if (cfg.auditWorkerJobs 
            && verifyFacility(cfg.auditWorkerJobs, &auditLogFacility) == -1)
         dnxSyslog(LOG_ERR, "config: Invalid audit facility: %s", 
               cfg.auditWorkerJobs);
      else
         ret = DNX_OK;
   }

   if (ret != DNX_OK)
      dnxCfgParserDestroy(cfgParser);
   
   return ret;
}

//----------------------------------------------------------------------------

/** Release the configuration parser object. */
void releaseConfig(void) { dnxCfgParserDestroy(cfgParser); }

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
      dnxSyslog(LOG_WARNING, 
            "init: No Nagios services defined! "
            "Defaulting to %d slots in the DNX job queue", size);
   }

   // check for configuration minServiceSlots override
   if (size < cfg.minServiceSlots)
   {
      dnxSyslog(LOG_WARNING, 
         "init: Overriding calculated service check slot count. "
         "Increasing from %d to configured minimum: %d", 
         size, cfg.minServiceSlots);
      size = cfg.minServiceSlots;
   }

   // check for configuration maxNodeRequests override
   if (size > cfg.maxNodeRequests)
   {
      dnxSyslog(LOG_WARNING, 
         "init: Overriding calculated service check slot count. "
         "Decreasing from %d to configured maximum: %d", size, 
         cfg.maxNodeRequests);
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
   if ((svc = (service *)(ds->object)) == 0)
#else
   if ((svc = find_service(ds->host_name, ds->service_description)) == 0)
#endif
   {
      // ERROR - This should never happen here: The service was not found.
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Could not find service %s for host %s",
         ds->service_description, ds->host_name);
      return DNX_ERR_INVALID;
   }

   // Fill-in the job structure with the necessary information
   dnxMakeXID(&Job.xid, DNX_OBJ_JOB, serial, 0);
   Job.payload    = svc;
   Job.cmd        = xstrdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   Job.expires    = Job.start_time + Job.timeout + 5; /* temporary till we have a config variable for it ... */
   Job.pNode      = pNode;

   dnxDebug(2, "DnxNebMain: Posting Job [%lu]: %s", serial, Job.cmd);

   // Post to the Job Queue
   if ((ret = dnxJobListAdd(joblist, &Job)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Failed to post Job [%lu]; \"%s\": %d", 
            Job.xid.objSerial, Job.cmd, ret);

   // Worker Audit Logging
   dnxAuditJob(&Job, "ASSIGN");

   return ret;
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
   DnxNodeRequest * pNode;
   int ret;

   if (event_type != NEBCALLBACK_SERVICE_CHECK_DATA)
      return OK;

   if (svcdata == 0)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Received NULL service data structure");
      return ERROR;  // shouldn't happen - internal Nagios error
   }

   if (svcdata->type != NEBTYPE_SERVICECHECK_INITIATE)
      return OK;  // ignore non-pre-run service checks

   // check for local execution pattern on command line
   if (cfg.localCheckPattern && regexec(&regEx, svcdata->command_line, 0, 0, 0) == 0)
   {
      dnxDebug(1, "ehSvcCheck: Job will execute locally: %s", svcdata->command_line);
      return OK;     // tell nagios execute locally
   }

   dnxDebug(2, "ehSvcCheck: Received Job [%lu] at %lu (%lu)",
         serial, (unsigned long)time(0), 
         (unsigned long)svcdata->start_time.tv_sec);

   if ((ret = dnxGetNodeRequest(registrar, &pNode)) != DNX_OK)
   {
      dnxDebug(1, "ehSvcCheck: No worker nodes requests available: %s", 
            dnxErrorString(ret));
      return OK;     // tell nagios execute locally
   }

   if ((ret = dnxPostNewJob(joblist, serial, svcdata, pNode)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Unable to post job [%lu]: %s", 
            serial, dnxErrorString(ret));
      return OK;     // tell nagios execute locally
   }

   serial++;                           // bump serial number

   return NEBERROR_CALLBACKOVERRIDE;   // tell nagios we want it
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

   releaseConfig();

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

   // create and configure collector
   if ((ret = dnxCollectorCreate("Collect", cfg.collectorUrl, 
         joblist, &collector)) != 0)
      return ret;

   // create and configure dispatcher
   if ((ret = dnxDispatcherCreate("Dispatch", cfg.dispatcherUrl, 
         joblist, &dispatcher)) != 0)
      return ret;

   // create worker node registrar
   if ((ret = dnxRegistrarCreate(joblistsz * 2, 
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

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

void dnxJobCleanup(DnxNewJob * pJob)
{
   if (pJob)
   {
      // Free the Pending Job command string
      if (pJob->cmd)
      {
         xfree(pJob->cmd);
         pJob->cmd = 0;
      }

      // Free the node request message
      if (pJob->pNode)
      {
         xfree(pJob->pNode);
         pJob->pNode = 0;
      }
   }
}

//----------------------------------------------------------------------------

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
         pJob->xid.objSerial,
         (unsigned)((addr >> 24) & 0xff),
         (unsigned)((addr >> 16) & 0xff),
         (unsigned)((addr >>  8) & 0xff),
         (unsigned)( addr        & 0xff),
         pJob->pNode->xid.objSlot,
         pJob->cmd
         );
   }

   return DNX_OK;
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
      args = DNX_DEFAULT_SERVER_CONFIG_FILE;
      dnxSyslog(LOG_ERR, "dnxServer: DNX config file not specified. "
                         "Defaulting to %s", args);
   }

   verifyFacility(DNX_DEFAULT_LOG_FACILITY, &dnxLogFacility);
   auditLogFacility = 0;

   memset(&regEx, 0, sizeof regEx);

   if ((ret = initConfig(args)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Failed to load configuration: %d", ret);
      return ERROR;
   }

   // set configured debug level and syslog log facility code
   initLogging(&cfg.debug, &dnxLogFacility);

   // subscribe to PROCESS_DATA call-backs in order to defer initialization
   //    until after Nagios validates its configuration and environment.
   if ((ret = neb_register_callback(NEBCALLBACK_PROCESS_DATA, 
         myHandle, 0, ehProcessData)) != OK)
   {
      dnxSyslog(LOG_ERR, 
            "dnxServer: PROCESS_DATA event registration failed with %d: %s", 
            ret, dnxErrorString(ret));
      releaseConfig();
      return ERROR;
   }

   dnxSyslog(LOG_INFO, "dnxServer: Registered for PROCESS_DATA event");
   dnxSyslog(LOG_INFO, "dnxServer: Module initialization completed");

   start_time = time(0);

   return OK;
}

/*--------------------------------------------------------------------------*/

