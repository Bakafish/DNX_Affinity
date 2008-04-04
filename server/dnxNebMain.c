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
#include "objects.h"    // for nagios service data type
#include "nebmodules.h"
#include "nebstructs.h"
#include "nebcallbacks.h"
#include "neberrors.h"
#include "broker.h"

#if CURRENT_NEB_API_VERSION == 2
# define OBJECT_FIELD_NAME object
#elif CURRENT_NEB_API_VERSION == 3
# define OBJECT_FIELD_NAME object_ptr
#else
# error Unsupported NEB API version.
#endif

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_VERSION                    VERSION

#define DNX_DEFAULT_SERVER_CONFIG_FILE SYSCONFDIR "/dnxServer.cfg"

// specify event broker API version (required)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/** The payload of a new job object. */
typedef struct DnxJobData
{
   service * svc;                   //!< The nagios service check structure.
   int chkopts;                     //!< The nagios 3.x check options.
   int schedule;                    //!< The nagios 3.x schedule flag.
   int reschedule;                  //!< The nagios 3.x reschedule flag.
   double latency;                  //!< The nagios 3.x results latency value.
} DnxJobData;

/** The internal server module configuration data structure. */
typedef struct DnxServerCfg
{
   char * dispatcherUrl;            //!< The dispatcher channel URL.
   char * collectorUrl;             //!< The collector channel URL.
   char * authWorkerNodes;          //!< The authorized worker node address list.
   unsigned maxNodeRequests;        //!< The maximum acceptable node requests.
   unsigned minServiceSlots;        //!< The minimum acceptable node requests.
   unsigned expirePollInterval;     //!< The job expiration timer check interval.
   char * localCheckPattern;        //!< The regular expression for local jobs.
   char * syncScript;               //!< The sync script path and file name.
   char * logFacility;              //!< The syslog logging facility string.
   char * auditWorkerJobs;          //!< The syslog audit facility string.
   unsigned debugLevel;             //!< The global debug level.
} DnxServerCfg;

// module static data
static DnxServerCfg cfg;            //!< The server configuration parameters.
static DnxJobList * joblist;        //!< The master job list.
static DnxRegistrar * registrar;    //!< The client node registrar.
static DnxDispatcher * dispatcher;  //!< The job list dispatcher.
static DnxCollector * collector;    //!< The job list results collector.
static time_t start_time;           //!< The module start time.
static void * myHandle;             //!< Private NEB module handle.
static regex_t regEx;               //!< Compiled regular expression structure.
static int dnxLogFacility;          //!< The syslog logging facility code.
static int auditLogFacility;        //!< The syslog audit facility code.

/** The system default configuration parameters. */
static char * cfgdefs[] =
{
   "channelDispatcher   = udp://0:12480",
   "channelCollector    = udp://0:12481",
   "maxNodeRequests     = 0x7FFFFFFF",
   "minServiceSlots     = 100",
   "expirePollInterval  = 5",
   "logFacility         = LOG_LOCAL7",
   0,
};

/** The configuration parser dictionary. */
static DnxCfgDict dict[] = 
{
   { "channelDispatcher",   DNX_CFG_URL      },
   { "channelCollector",    DNX_CFG_URL      },
   { "authWorkerNodes",     DNX_CFG_STRING   },
   { "maxNodeRequests",     DNX_CFG_UNSIGNED },
   { "minServiceSlots",     DNX_CFG_UNSIGNED },
   { "expirePollInterval",  DNX_CFG_UNSIGNED },
   { "localCheckPattern",   DNX_CFG_STRING   },
   { "syncScript",          DNX_CFG_FSPATH   },
   { "logFacility",         DNX_CFG_STRING   },
   { "auditWorkerJobs",     DNX_CFG_STRING   },
   { "debugLevel",          DNX_CFG_UNSIGNED },
   { 0 },
};

/** The array of cfg variable addresses for the configuration parser. */
static void * ppvals[] =
{
   &cfg.dispatcherUrl,
   &cfg.collectorUrl,
   &cfg.authWorkerNodes,
   &cfg.maxNodeRequests,
   &cfg.minServiceSlots,
   &cfg.expirePollInterval,
   &cfg.localCheckPattern,
   &cfg.syncScript,
   &cfg.logFacility,
   &cfg.auditWorkerJobs,
   &cfg.debugLevel,
};

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
   int ret;

   // parse configuration file; pass defaults
   if ((ret = dnxParseCfgFile(ConfigFile, cfgdefs, dict, ppvals)) == 0)
   {
      int err;

      // clear the regex string, as we may write into it
      memset(&regEx, 0, sizeof regEx);

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
      dnxFreeCfgValues(dict, ppvals);
   
   return ret;
}

//----------------------------------------------------------------------------

/** Cleanup the config file parser. */
void releaseConfig(void) 
{
   if (cfg.localCheckPattern)
      regfree(&regEx);

   dnxFreeCfgValues(dict, ppvals);
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

#if CURRENT_NEB_API_VERSION == 2

//----------------------------------------------------------------------------

/** Post job result information to Nagios 2.x.
 * 
 * @param[in] svc - the nagios service to which the results belong.
 * @param[in] start_time - the check start time in seconds.
 * @param[in] early_timeout - boolean; true (!0) means the service timed out.
 * @param[in] res_code - the check result code.
 * @param[in] res_data - the check result data.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @todo This routine should be in nagios code.
 */
static int nagios2xPostResult(service * svc, time_t start_time, 
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

#endif   // CURRENT_NEB_API_VERSION == 2

#if CURRENT_NEB_API_VERSION == 3

//----------------------------------------------------------------------------

/** Post job result information to Nagios 3.x.
 * 
 * @param[in] svc - the nagios service to which the results belong.
 * @param[in] check_type - nagios 3.x check type value.
 * @param[in] check_options - nagios 3.x bit-wise check options.
 * @param[in] schedule - boolean; nagios 3.x schedule flag.
 * @param[in] reschedule - boolean; nagios 3.x reschedule flag.
 * @param[in] latency - nagios 3.x latency value.
 * @param[in] start_time - the check start time in seconds.
 * @param[in] finish_time - the check finish time in seconds.
 * @param[in] early_timeout - boolean; true (!0) means the service timed out.
 * @param[in] exited_ok - boolean; true (!0) if the external check exited ok.
 * @param[in] res_code - the check result code.
 * @param[in] res_data - the check result data.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @todo This routine should be in nagios code.
 */
static int nagios3xPostResult(service * svc, int check_type, 
      int check_options, int schedule, int reschedule, double latency, 
      time_t start_time, time_t finish_time, int early_timeout, 
      int exited_ok, int res_code, char * res_data)
{
   /** @todo Invent a different temp path strategy. */

   // a nagios 3.x global variable
   extern char * temp_path;

   char * escaped_res_data;
   char filename[512];
   mode_t old_umask;
   FILE * fp = 0;
   int fd;

   // a nagios 3.x core function
   if ((escaped_res_data = escape_newlines(res_data)) == 0)
      return DNX_ERR_MEMORY;

   /* open a temp file for storing check output */
   sprintf(filename, "%s/checkXXXXXX", temp_path);

   old_umask = umask(077);
   if ((fd = mkstemp(filename)) >= 0)
      fp = fdopen(fd, "w");
   umask(old_umask);

   if (fp == 0)
   {
      free(escaped_res_data); // allocated by nagios - use free - not xfree
      if (fd >= 0) close(fd);
      return DNX_ERR_OPEN;
   }

   /* write check result to file */
   fprintf(fp, "### Active Check Result File ###\n");
   fprintf(fp, "file_time=%lu\n\n", (unsigned long)start_time);
   fprintf(fp, "### Nagios Service Check Result ###\n");
   fprintf(fp, "# Time: %s", ctime(&start_time));
   fprintf(fp, "host_name=%s\n", svc->host_name);
   fprintf(fp, "service_description=%s\n", svc->description);
   fprintf(fp, "check_type=%d\n", check_type);
   fprintf(fp, "check_options=%d\n", check_options);
   fprintf(fp, "scheduled_check=%d\n", schedule);
   fprintf(fp, "reschedule_check=%d\n", reschedule);
   fprintf(fp, "latency=%f\n", latency);
   fprintf(fp, "start_time=%lu.0\n", (unsigned long)start_time);
   fprintf(fp, "finish_time=%lu.%lu\n", (unsigned long)finish_time);
   fprintf(fp, "early_timeout=%d\n", early_timeout);
   fprintf(fp, "exited_ok=%d\n", exited_ok);
   fprintf(fp, "return_code=%d\n", res_code);
   fprintf(fp, "output=%s\n", escaped_res_data);

   fclose(fp);

   free(escaped_res_data); // allocated by nagios - use free - not xfree

   // a nagios 3.x core function
   move_check_result_to_queue(filename);

   return 0;
}

#endif   // CURRENT_NEB_API_VERSION == 3

//----------------------------------------------------------------------------

int dnxPostResult(void * data, time_t start_time, unsigned delta, 
      int early_timeout, int res_code, char * res_data)
{
   DnxJobData * jdp = (DnxJobData *)data;

   if (early_timeout)
      res_code = STATE_UNKNOWN;

   /** @todo Nagios 3.x: Collect a better value for exited_ok. */
   /** @todo Nagiod 3.x: Collect a better value for check_type. */

#if CURRENT_NEB_API_VERSION == 2

   return nagios2xPostResult(jdp->svc, start_time, early_timeout, 
         res_code, res_data);

#elif CURRENT_NEB_API_VERSION == 3

   return nagios3xPostResult(jdp->svc, SERVICE_CHECK_ACTIVE, 
         jdp->chkopts, jdp->schedule, jdp->reschedule, jdp->latency, 
         start_time, start_time + delta, early_timeout, 
         1, res_code, res_data);

#else
# error Unsupported NEB API version.
#endif
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
 * @param[in] jdp - a pointer to a job data structure.
 * @param[in] ds - a pointer to the nagios job that's being posted.
 * @param[in] pNode - a dnxClient node request structure that is being 
 *    posted with this job. The dispatcher thread will send the job to the
 *    associated node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxPostNewJob(DnxJobList * joblist, unsigned long serial, 
      DnxJobData * jdp, nebstruct_service_check_data * ds, 
      DnxNodeRequest * pNode)
{
   DnxNewJob Job;
   int ret;

   // fill-in the job structure with the necessary information
   dnxMakeXID(&Job.xid, DNX_OBJ_JOB, serial, 0);
   Job.payload    = jdp;
   Job.cmd        = xstrdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   Job.expires    = Job.start_time + Job.timeout + 5; /* temporary till we have a config variable for it ... */
   Job.pNode      = pNode;

   dnxDebug(2, "DnxNebMain: Posting Job [%lu]: %s", serial, Job.cmd);

   // post to the Job Queue
   if ((ret = dnxJobListAdd(joblist, &Job)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxPostNewJob: Failed to post Job [%lu]; \"%s\": %d", 
            Job.xid.objSerial, Job.cmd, ret);

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
   DnxJobData * jdp;
   int ret;

   if (event_type != NEBCALLBACK_SERVICE_CHECK_DATA)
      return OK;

   if (svcdata == 0)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Received NULL service data structure");
      return ERROR;  // shouldn't happen - internal Nagios error
   }

   if (svcdata->type != NEBTYPE_SERVICECHECK_INITIATE)
      return OK;  // ignore non-initiate service checks

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

   // allocate and populate a new job payload object
   if ((jdp = (DnxJobData *)xmalloc(sizeof *jdp)) == 0)
   {
      dnxDebug(1, "ehSvcCheck: Out of memory!");
      return OK;
   }
   memset(jdp, 0, sizeof *jdp);
   jdp->svc = (service *)svcdata->OBJECT_FIELD_NAME;

   assert(jdp->svc);

#if CURRENT_NEB_API_VERSION == 3
   {
      // a nagios 3.x global variable
      extern check_result check_result_info;

      /** @todo patch nagios to pass these values to the event handler. */

      jdp->chkopts    = check_result_info.check_options;
      jdp->schedule   = check_result_info.scheduled_check;
      jdp->reschedule = check_result_info.reschedule_check;
      jdp->latency    = jdp->svc->latency;
   }
#endif

   if ((ret = dnxPostNewJob(joblist, serial, jdp, svcdata, pNode)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Unable to post job [%lu]: %s", 
            serial, dnxErrorString(ret));
      xfree(jdp);
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
      xfree(pJob->cmd);
      xfree(pJob->payload);
      xfree(pJob->pNode);
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

   // temporarily set defaults so initConfig has somewhere to write
   dnxLogFacility = LOG_LOCAL7;
   auditLogFacility = 0;
   if ((ret = initConfig(args)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxServer: Failed to load configuration: %d", ret);
      return ERROR;
   }

   // set configured debug level and syslog log facility code
   initLogging(&cfg.debugLevel, &dnxLogFacility);

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

