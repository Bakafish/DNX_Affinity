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
#include "dnxHeap.h"
#include "dnxLogging.h"
#include "dnxCollector.h"
#include "dnxDispatcher.h"
#include "dnxRegistrar.h"
#include "dnxJobList.h"
#include "dnxNode.h"
#include "stdarg.h"
#include "dnxXml.h"
#include "dnxComStats.h"
#include <netinet/in.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION "<unknown>"
#endif

#ifndef SYSCONFDIR
# define SYSCONFDIR "/etc"
#endif

#ifndef SYSLOGDIR
# define SYSLOGDIR  "/var/log"
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

#define DNX_DEFAULT_SERVER_CONFIG_FILE SYSCONFDIR "/dnxServer.cfg"
#define DNX_DEFAULT_LOG                SYSLOGDIR "/dnxsrv.log"
#define DNX_DEFAULT_DBGLOG             SYSLOGDIR "/dnxsrv.dbg.log"

// specify event broker API version (required)
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/** The internal structure of a new job payload object. */

// typedef struct DnxJobData
// {
//    service * svc;                   //!< The nagios service structure.
//    int chkopts;                     //!< The nagios 3.x check options.
//    int schedule;                    //!< The nagios 3.x schedule flag.
//    int reschedule;                  //!< The nagios 3.x reschedule flag.
//    double latency;                  //!< The nagios 3.x results latency value.
//    int  type;                       //!< The type of nagios check being executed.
// } DnxJobData;

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
   char * bypassHostgroup;          //!< The hostgroup name for local jobs.
   char * syncScript;               //!< The sync script path and file name.
   char * logFilePath;              //!< The system log file path.
   char * debugFilePath;            //!< The debug log file path.
   char * auditFilePath;            //!< The audit log file path.
   unsigned debugLevel;             //!< The global debug level.
} DnxServerCfg;

// module static data
static DnxServerCfg cfg;            //!< The server configuration parameters.
static DnxCfgParser * parser;       //!< The system configuration parser.
static DnxJobList * joblist;        //!< The master job list.
static DnxRegistrar * registrar;    //!< The client node registrar.
static DnxDispatcher * dispatcher;  //!< The job list dispatcher.
static DnxCollector * collector;    //!< The job list results collector.
static DnxAffinityList * hostGrpAffinity;  //!< The list of affinity groups.
static DnxAffinityList * hostAffinity; //!< The affinity list of hosts.
static time_t start_time;           //!< The module start time.
static void * myHandle;             //!< Private NEB module handle.
static regex_t regEx;               //!< Compiled regular expression structure.
static unsigned long serial = 0;    //!< The number of service checks processed
static pthread_mutex_t submitCheckMutex; //!< Make sure we serialize check submissions

//SM 09/08 DnxNodeList
DnxNode * gTopNode = NULL;
DnxNode * pDnxNode = NULL;
extern DCS * gTopDCS;
//SM 09/08 DnxNodeList End

extern circular_buffer service_result_buffer;   //!< Nagios result buffer
extern int check_result_buffer_slots;           //!< Nagios result slot count


/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Cleanup the config file parser. */
static void releaseConfig(void)
{
   if (cfg.localCheckPattern)
      regfree(&regEx);

   dnxCfgParserDestroy(parser);
}

//----------------------------------------------------------------------------

/** Validate a configuration data structure in context.
 *
 * @param[in] dict - the dictionary used by the DnxCfgParser.
 * @param[in] vptrs - an array of opaque objects (either pointers or values)
 *    to be checked.
 * @param[in] passthru - an opaque pointer passed through from
 *    dnxCfgParserCreate. In this routine, it's the regex_t object into which
 *    we should parse the regular expression if one is given.
 *
 * @return Zero on success, or a non-zero error value. This error value is
 * passed back through dnxCfgParserParse.
 */
static int validateCfg(DnxCfgDict * dict, void ** vptrs, void * passthru)
{
   regex_t * rep = (regex_t *)passthru;
   int err, ret = DNX_ERR_INVALID;
   DnxServerCfg cfg;

   assert(dict && vptrs && passthru);

   // setup data structure so we can use the same functionality we had before
   cfg.dispatcherUrl      = (char *)vptrs[ 0];
   cfg.collectorUrl       = (char *)vptrs[ 1];
   cfg.authWorkerNodes    = (char *)vptrs[ 2];
   cfg.maxNodeRequests    = (unsigned)(intptr_t)vptrs[ 3];
   cfg.minServiceSlots    = (unsigned)(intptr_t)vptrs[ 4];
   cfg.expirePollInterval = (unsigned)(intptr_t)vptrs[ 5];
   cfg.localCheckPattern  = (char *)vptrs[ 6];
   cfg.bypassHostgroup    = (char *)vptrs[ 7];
   cfg.syncScript         = (char *)vptrs[ 8];
   cfg.logFilePath        = (char *)vptrs[ 9];
   cfg.debugFilePath      = (char *)vptrs[ 10];
   cfg.auditFilePath      = (char *)vptrs[11];
   cfg.debugLevel         = (unsigned)(intptr_t)vptrs[12];

   // validate configuration items in context
   if (!cfg.dispatcherUrl)
      dnxLog("config: Missing channelDispatcher parameter.");
   else if (!cfg.collectorUrl)
      dnxLog("config: Missing channelCollector parameter.");
   else if (cfg.maxNodeRequests < 1)
      dnxLog("config: Invalid maxNodeRequests parameter.");
   else if (cfg.minServiceSlots < 1)
      dnxLog("config: Invalid minServiceSlots parameter.");
   else if (cfg.expirePollInterval < 1)
      dnxLog("config: Invalid expirePollInterval parameter.");
   else if (cfg.localCheckPattern && (err = regcomp(rep,
         cfg.localCheckPattern, REG_EXTENDED | REG_NOSUB)) != 0)
   {
      char buffer[128];
      regerror(err, rep, buffer, sizeof buffer);
      dnxLog("config: Failed to compile localCheckPattern (\"%s\"): %s.",
             cfg.localCheckPattern, buffer);
      regfree(rep);
   }
   else
      ret = 0;

   return ret;
}

//----------------------------------------------------------------------------

/** Read and parse the dnxServer configuration file.
 *
 * @param[in] cfgfile - the configuration file to be read.
 *
 * @return Zero on success, or a non-zero error value.
 */
static int initConfig(char * cfgfile)
{
   DnxCfgDict dict[] =
   {  // Do NOT change the order, unless you know what you're doing!
      { "channelDispatcher",  DNX_CFG_URL,      &cfg.dispatcherUrl      },
      { "channelCollector",   DNX_CFG_URL,      &cfg.collectorUrl       },
      { "authWorkerNodes",    DNX_CFG_STRING,   &cfg.authWorkerNodes    },
      { "maxNodeRequests",    DNX_CFG_UNSIGNED, &cfg.maxNodeRequests    },
      { "minServiceSlots",    DNX_CFG_UNSIGNED, &cfg.minServiceSlots    },
      { "expirePollInterval", DNX_CFG_UNSIGNED, &cfg.expirePollInterval },
      { "localCheckPattern",  DNX_CFG_STRING,   &cfg.localCheckPattern  },
      { "bypassHostgroup",    DNX_CFG_STRING,   &cfg.bypassHostgroup    },
      { "syncScript",         DNX_CFG_FSPATH,   &cfg.syncScript         },
      { "logFile",            DNX_CFG_FSPATH,   &cfg.logFilePath        },
      { "debugFile",          DNX_CFG_FSPATH,   &cfg.debugFilePath      },
      { "auditFile",          DNX_CFG_FSPATH,   &cfg.auditFilePath      },
      { "debugLevel",         DNX_CFG_UNSIGNED, &cfg.debugLevel         },
      { 0 },
   };
   char cfgdefs[] =
      "channelDispatcher = udp://0:12480\n"
      "channelCollector = udp://0:12481\n"
      "maxNodeRequests = 0x7FFFFFFF\n"
      "minServiceSlots = 100\n"
      "expirePollInterval = 5\n"
      "logFile = " DNX_DEFAULT_LOG "\n"
      "debugFile = " DNX_DEFAULT_DBGLOG "\n";

   int ret;
   regex_t re;

   // clear the regex string, as we may write into it
   memset(&re, 0, sizeof re);

   // create global configuration parser object
   if ((ret = dnxCfgParserCreate(cfgdefs, cfgfile, 0, dict,
         validateCfg, &parser)) != 0)
      return ret;

   // parse configuration file; pass defaults
   if ((ret = dnxCfgParserParse(parser, &re)) == 0)
      regEx = re;
   else
      dnxCfgParserDestroy(parser);

   return ret;
}

//----------------------------------------------------------------------------

/** Return the number of services configured in Nagios.
 *
 * @return The number of services configured in Nagios.
 *
 * @todo This routine should be in nagios code.
 */

// This is supposedly broken
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


// #if CURRENT_NEB_API_VERSION == 2
// 
// //----------------------------------------------------------------------------
// 
// /** Post job result information to Nagios 2.x.
//  *
//  * @param[in] svc - the nagios service to which the results belong.
//  * @param[in] start_time - the check start time in seconds.
//  * @param[in] early_timeout - boolean; true (!0) means the service timed out.
//  * @param[in] res_code - the check result code.
//  * @param[in] res_data - the check result data.
//  *
//  * @return Zero on success, or a non-zero error value.
//  *
//  * @todo This routine should be in nagios code.
//  */
// static int nagios2xPostResult(service * svc, time_t start_time,
//       int early_timeout, int res_code, char * res_data)
// {
//    extern circular_buffer service_result_buffer;
//    int check_result_buffer_slots = 4096;
// 
//    service_message * new_message;
// 
//    // note that we're using malloc, not xmalloc - nagios takes ownership
//    if ((new_message = (service_message *)malloc(sizeof *new_message)) == 0)
//       return DNX_ERR_MEMORY;
// 
//    gettimeofday(&new_message->finish_time, 0);
//    strncpy(new_message->host_name, svc->host_name,sizeof(new_message->host_name) - 1);
//    new_message->host_name[sizeof(new_message->host_name) - 1] = 0;
//    strncpy(new_message->description, svc->description,
//          sizeof(new_message->description) - 1);
//    new_message->description[sizeof(new_message->description) - 1] = 0;
//    new_message->return_code = res_code;
//    new_message->exited_ok = TRUE;
//    new_message->check_type = SERVICE_CHECK_ACTIVE;
//    new_message->parallelized = svc->parallelize;
//    new_message->start_time.tv_sec = start_time;
//    new_message->start_time.tv_usec = 0L;
//    new_message->early_timeout = early_timeout;
//    strncpy(new_message->output, res_data, sizeof(new_message->output) - 1);
//    new_message->output[sizeof(new_message->output) - 1] = 0;
// 
//    pthread_mutex_lock(&service_result_buffer.buffer_lock);
// 
//    // handle overflow conditions
//    if (service_result_buffer.items == check_result_buffer_slots)
//    {
//       service_result_buffer.overflow++;
//       service_result_buffer.tail = (service_result_buffer.tail + 1)
//             % check_result_buffer_slots;
//    }
// 
//    // save the data to the buffer
//    ((service_message **)service_result_buffer.buffer)
//          [service_result_buffer.head] = new_message;
// 
//    // increment the head counter and items
//    service_result_buffer.head = (service_result_buffer.head + 1)
//          % check_result_buffer_slots;
//    if (service_result_buffer.items < check_result_buffer_slots)
//       service_result_buffer.items++;
//    if (service_result_buffer.items > service_result_buffer.high)
//       service_result_buffer.high = service_result_buffer.items;
// 
//    pthread_mutex_unlock(&service_result_buffer.buffer_lock);
// 
//    return 0;
// }
// 
// #endif   // CURRENT_NEB_API_VERSION == 2
// 
// #if CURRENT_NEB_API_VERSION == 3
// 
// //----------------------------------------------------------------------------
// 
// /** Post job result information to Nagios 3.x.
//  *
//  * @param[in] svc - the nagios service to which the results belong.
//  * @param[in] check_type - nagios 3.x check type value.
//  * @param[in] check_options - nagios 3.x bit-wise check options.
//  * @param[in] schedule - boolean; nagios 3.x schedule flag.
//  * @param[in] reschedule - boolean; nagios 3.x reschedule flag.
//  * @param[in] latency - nagios 3.x latency value.
//  * @param[in] start_time - the check start time in seconds.
//  * @param[in] finish_time - the check finish time in seconds.
//  * @param[in] early_timeout - boolean; true (!0) means the service timed out.
//  * @param[in] exited_ok - boolean; true (!0) if the external check exited ok.
//  * @param[in] res_code - the check result code.
//  * @param[in] res_data - the check result data.
//  *
//  * @return Zero on success, or a non-zero error value.
//  *
//  * @todo This routine should be in nagios code.
//  */
// static int nagios3xPostServiceResult(service * svc,
//       int check_options, int schedule, int reschedule, double latency,
//       time_t start_time, time_t finish_time, int early_timeout,
//       int exited_ok, int res_code, char * res_data)
// {
//    /** @todo Invent a different temp path strategy. */
// 
//    // a nagios 3.x global variable
//    extern char * temp_path;
// 
//    char * escaped_res_data;
//    char filename[512];
//    mode_t old_umask;
//    FILE * fp = 0;
//    int fd;
// 
//    // a nagios 3.x core function
//    if ((escaped_res_data = escape_newlines(res_data)) == 0)
//       return DNX_ERR_MEMORY;
// 
//    /* open a temp file for storing check output */
//    sprintf(filename, "%s/checkXXXXXX", temp_path);
// 
//    old_umask = umask(077);
//    if ((fd = mkstemp(filename)) >= 0)
//       fp = fdopen(fd, "w");
//    umask(old_umask);
// 
//    if (fp == 0)
//    {
//       free(escaped_res_data); // allocated by nagios - use free - not xfree
//       if (fd >= 0) close(fd);
//       return DNX_ERR_OPEN;
//    }
// 
//    /* write check result to file */
//    fprintf(fp, "### Active Check Result File ###\n");
//    fprintf(fp, "file_time=%lu\n\n", (unsigned long)start_time);
//    fprintf(fp, "### Nagios Service Check Result ###\n");
//    fprintf(fp, "# Time: %s", ctime(&start_time));
//    fprintf(fp, "host_name=%s\n", svc->host_name);
//    fprintf(fp, "service_description=%s\n", svc->description);
//    fprintf(fp, "check_type=%d\n", SERVICE_CHECK_ACTIVE);
//    fprintf(fp, "check_options=%d\n", check_options);
//    fprintf(fp, "scheduled_check=%d\n", schedule);
//    fprintf(fp, "reschedule_check=%d\n", reschedule);
//    fprintf(fp, "latency=%f\n", latency);
//    fprintf(fp, "start_time=%lu.0\n", (unsigned long)start_time);
//    fprintf(fp, "finish_time=%lu.%lu\n", (unsigned long)finish_time);
//    fprintf(fp, "early_timeout=%d\n", early_timeout);
//    fprintf(fp, "exited_ok=%d\n", exited_ok);
//    fprintf(fp, "return_code=%d\n", res_code);
//    fprintf(fp, "output=%s\n", escaped_res_data);
// 
//    fclose(fp);
// 
//    free(escaped_res_data); // allocated by nagios - use free - not xfree
// 
//    // a nagios 3.x core function
//    move_check_result_to_queue(filename);
// 
//    return 0;
// }
// 
// static int nagios3xPostResult(host * hst,
//       int check_options, int schedule, int reschedule, double latency,
//       time_t start_time, time_t finish_time, int early_timeout,
//       int exited_ok, int res_code, char * res_data)
// {
//    /** @todo Invent a different temp path strategy. */
// 
//    // a nagios 3.x global variable
//    extern char * temp_path;
// 
//    char * escaped_res_data;
//    char filename[512];
//    mode_t old_umask;
//    FILE * fp = 0;
//    int fd;
// 
//    // a nagios 3.x core function
//    if ((escaped_res_data = escape_newlines(res_data)) == 0)
//       return DNX_ERR_MEMORY;
// 
//    /* open a temp file for storing check output */
//    sprintf(filename, "%s/checkXXXXXX", temp_path);
// 
//    old_umask = umask(077);
//    if ((fd = mkstemp(filename)) >= 0)
//       fp = fdopen(fd, "w");
//    umask(old_umask);
// 
//    if (fp == 0)
//    {
//       free(escaped_res_data); // allocated by nagios - use free - not xfree
//       if (fd >= 0) close(fd);
//       return DNX_ERR_OPEN;
//    }
// 
//    /* write check result to file */
//    fprintf(fp, "### Active Check Result File ###\n");
//    fprintf(fp, "file_time=%lu\n\n", (unsigned long)start_time);
//    fprintf(fp, "### Nagios Host Check Result ###\n");
//    fprintf(fp, "# Time: %s", ctime(&start_time));
//    fprintf(fp, "host_name=%s\n", hst->name);
// //   fprintf(fp, "service_description=%s\n", hst->description);
//    fprintf(fp, "check_type=%d\n", HOST_CHECK_ACTIVE);
//    fprintf(fp, "check_options=%d\n", check_options);
//    fprintf(fp, "scheduled_check=%d\n", schedule);
//    fprintf(fp, "reschedule_check=%d\n", reschedule);
//    fprintf(fp, "latency=%f\n", latency);
//    fprintf(fp, "start_time=%lu.0\n", (unsigned long)start_time);
//    fprintf(fp, "finish_time=%lu.%lu\n", (unsigned long)finish_time);
//    fprintf(fp, "early_timeout=%d\n", early_timeout);
//    fprintf(fp, "exited_ok=%d\n", exited_ok);
//    fprintf(fp, "return_code=%d\n", res_code);
//    fprintf(fp, "output=%s\n", escaped_res_data);
// 
//    fclose(fp);
// 
//    free(escaped_res_data); // allocated by nagios - use free - not xfree
// 
//    // a nagios 3.x core function
//    move_check_result_to_queue(filename);
// 
//    return 0;
// }
// #endif   // CURRENT_NEB_API_VERSION == 3



int dnxSubmitCheck(DnxNewJob * Job, DnxResult * sResult, time_t check_time)
{
   DNX_PT_MUTEX_LOCK(&submitCheckMutex);

   check_result *chk_result;
   chk_result = (check_result *)malloc(sizeof(check_result));
   /* Set the default values in the check result structure */
   init_check_result(chk_result);
   
   /*
   * Set up the check result structure with information that we were passed
   * Nagios normally reads the check results from a diskfile specified in
   * output_file member. But since we can directly access nagios result list,
   * we bypass the diskfile creation. We set output_file to NULL and
   * the fd to -1, hoping that nagios will have a NULL check.
   */
   chk_result->output_file = NULL;
   chk_result->output_file_fd = -1;
   chk_result->host_name = xstrdup(Job->host_name);


/*
	chk_result->check_options=check_options;
	chk_result->latency=latency;
*/
   if(Job->service_description != NULL) {
      chk_result->service_description = xstrdup(Job->service_description);
      chk_result->object_check_type = SERVICE_CHECK;
      chk_result->check_type = SERVICE_CHECK_ACTIVE;
   } else {
      chk_result->service_description = NULL;
      chk_result->object_check_type = HOST_CHECK;
      chk_result->check_type = HOST_CHECK_ACTIVE;
   }

   if (Job->pNode->xid.objSlot == -1) {
      // this was never dispatched
      dnxDebug(2, "dnxSubmitCheck: job[%lu] dnxClient=(unavailable) hostname=(%s)",
          Job->pNode->xid.objSerial, chk_result->host_name);
      chk_result->output = xstrdup(sResult->resData);
      xfree(sResult->resData);
   } else {
//    normalize_plugin_output(plugin_output, "B2");
   // Encapsulate the additional data into the extended results
      unsigned long long int *temp_aff = dnxGetAffinity(Job->host_name);
      char * hGroup = dnxGetHostgroupFromFlags(*temp_aff, Job->pNode->flags);
      
      dnxDebug(2, "dnxSubmitCheck: dnxClient=(%s:%s) hostgroup=(%s) hostname=(%s) description=(%s)",
         Job->pNode->hn, Job->pNode->addr, hGroup, chk_result->host_name, chk_result->service_description);
   
      /* We want to add an XML token that gives Bronx (and anyone else) the DNX client info
         but doesn't leak it into the main results string

         See the plugin API: http://nagios.sourceforge.net/docs/3_0/pluginapi.html
         
      DISK OK - free space: / 3326 MB (56%); | /=2643MB;5948;5958;0;5968\n <--- perf data (SERVICEPERFDATA)
      / 15272 MB (77%);   <--- Extended (LONGSERVICEOUTPUT)
      /boot 68 MB (69%);
      /home 69357 MB (27%);
      /var/log 819 MB (84%); | /boot=68MB;88;93;0;98 <--- perf data continued (SERVICEPERFDATA)
      /home=69357MB;253404;253409;0;253414 
      /var/log=818MB;970;975;0;980

      We should just be able to split on the first newline and insert our token after it.
         
      */
      char * tokenString;
      size_t tokenLength = asprintf(&tokenString, "<DNX><CLIENT=\"%s\"/><CLIENT_IP=\"%s\"/><HOSTGROUP=\"%s\"/></DNX>", Job->pNode->hn, Job->pNode->addr, hGroup);
      
      char * resultHead;
      char * resultString;
      size_t resultLength;
      
      if (tokenLength > 0) {
         size_t firstMatch = strcspn(sResult->resData, "\n");

         if(firstMatch > 0) { 
            // Just split on the first newline and insert in between 
            resultHead = (char *)xmalloc(firstMatch + 1);
            strncpy(resultHead, sResult->resData, firstMatch);
            resultHead[firstMatch]='\0'; // strncpy doesn't NULL terminate!
            resultLength = asprintf(&resultString, "%s\n%s%s", resultHead, tokenString, strchr(sResult->resData, '\n'));
         } else { // No perf data, no extended data
            resultLength = asprintf(&resultString, "%s\n%s", sResult->resData, tokenString);
         }
         xfree(tokenString);
      } else {
         // Some memory allocation issue creating the token string
         dnxDebug(1, "dnxSubmitCheck: Error allocating string.");
      }
   
      if(0 < resultLength <= MAX_PLUGIN_OUTPUT_LENGTH) {
         chk_result->output = resultString;
         dnxDebug(3, "dnxSubmitCheck: %s", resultString);
      } else {
         dnxDebug(2, "dnxSubmitCheck: Results string with DNX Token is too long!");
         xfree(resultString);
         chk_result->output = xstrdup(sResult->resData);
      }
      xfree(sResult->resData);
   }
   
   
   chk_result->return_code = sResult->resCode;
   chk_result->exited_ok = TRUE;
   chk_result->early_timeout = FALSE; // We should flag this as true if we know we had a real script timeout
   chk_result->scheduled_check = TRUE;
   chk_result->reschedule_check = TRUE;
   
   chk_result->start_time.tv_sec = Job->start_time; // check_time; // check_time should be start + delta
   chk_result->start_time.tv_usec = 0;
   chk_result->finish_time.tv_sec = check_time;
   chk_result->finish_time.tv_usec = 0;
//    chk_result->finish_time = chk_result->start_time;   
//    chk_result->finish_time.tv_sec += sResult->delta;
      
      
   
   /* Call the nagios function to insert the result into the result linklist */
   add_check_result_to_list(chk_result);
   DNX_PT_MUTEX_UNLOCK(&submitCheckMutex);
   return 0;
}


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
      dnxLog("No Nagios services defined! "
             "Defaulting to %d slots in the DNX job queue.", size);
   }

   // check for configuration minServiceSlots override
   if (size < cfg.minServiceSlots)
   {
      dnxLog("Overriding calculated service check slot count. "
             "Increasing from %d to configured minimum: %d.",
             size, cfg.minServiceSlots);
      size = cfg.minServiceSlots;
   }

   // check for configuration maxNodeRequests override
   if (size > cfg.maxNodeRequests)
   {
      dnxLog("Overriding calculated service check slot count. "
             "Decreasing from %d to configured maximum: %d.", size,
             cfg.maxNodeRequests);
      size = cfg.maxNodeRequests;
   }
   return size;
}

//----------------------------------------------------------------------------

/** Post a new service job from Nagios to the dnxServer job queue.
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
static int dnxPostNewServiceJob(DnxJobList * joblist, unsigned long serial, 
    int check_type, nebstruct_service_check_data * ds, DnxNodeRequest * pNode)
{
   DnxNewJob Job;
   int ret;

   assert(ds);
   assert(ds->command_line);
   time_t now;
   now = time(0);


   // fill-in the job structure with the necessary information
   dnxMakeXID(&Job.xid, DNX_OBJ_JOB, serial, 0);
   Job.host_name  = xstrdup(ds->host_name);
   Job.service_description = xstrdup(ds->service_description);
   Job.object_check_type  = check_type;
   Job.cmd        = xstrdup(ds->command_line);
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   // We need to expire a bit before Nagios does to make sure it get's our reply
   // DNX_DEF_TIMER_SLEEP is the length in ms of the expiration thread timer, 
   // so worst case it should give Nagios an answer.
   // If the job isn't assigned to a client in DNX_DISPATCH_TIMEOUT seconds it will expire
   Job.expires    = Job.start_time + Job.timeout - 5;
   Job.pNode      = pNode;
   Job.ack        = false;

   // post to the Job Queue
   if ((ret = dnxJobListAdd(joblist, &Job)) != DNX_OK) {
      dnxLog("dnxPostNewServiceJob: Failed to post Service Job [%lu:000000]; %s, \"%s\" Reason: %s.", 
         serial, ds->service_description, ds->command_line, dnxErrorString(ret));
      xfree(Job.host_name);
   } else {   
      dnxDebug(2, "dnxPostNewServiceJob: TO:(%i) Expires in (%i)sec. Posting Service (%s) Job [%lu:000000]: %s, %s.", 
         ds->timeout, ((ds->start_time.tv_sec + ds->timeout - 5) - now), ds->host_name, serial, ds->service_description, ds->command_line);
      // free the command line we generated?
      //xfree(ds->command_line);
   }
   return ret;
}

//----------------------------------------------------------------------------

/** Post a new host job from Nagios to the dnxServer job queue.
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
static int dnxPostNewHostJob(DnxJobList * joblist, unsigned long serial, 
    int check_type, nebstruct_host_check_data * ds, DnxNodeRequest * pNode)
{
   DnxNewJob Job;
   int ret;

   assert(ds);
   assert(ds->command_line);
   time_t now;
   now = time(0);

   // fill-in the job structure with the necessary information
   dnxMakeXID(&Job.xid, DNX_OBJ_JOB, serial, 0);
   Job.host_name  = xstrdup(ds->host_name); 
   Job.service_description = NULL;
   Job.object_check_type = check_type;
   Job.cmd        = xstrdup(ds->command_line); //ds->command_line;
   Job.start_time = ds->start_time.tv_sec;
   Job.timeout    = ds->timeout;
   // We need to expire a bit before Nagios does to make sure it get's our reply
   // DNX_DEF_TIMER_SLEEP is the length in ms of the expiration thread timer, 
   // so worst case it should give Nagios an answer.
   // If the job isn't assigned to a client in DNX_DISPATCH_TIMEOUT seconds it will expire
   Job.expires    = Job.start_time + Job.timeout - 5;
   Job.pNode      = pNode;
   Job.ack        = false;


   // post to the Job Queue
   if ((ret = dnxJobListAdd(joblist, &Job)) != DNX_OK) {
      dnxLog("dnxPostNewHostJob: Failed to post Host Job [%lu:000000]; \"%s\": %d.", serial, ds->command_line, ret);
      xfree(Job.host_name);
   } else {
      dnxDebug(2, "dnxPostNewHostJob: TO:(%i) Expires in (%i)sec. Posting Host (%s) Job [%lu:000000]: %s.", 
         ds->timeout, ((ds->start_time.tv_sec + ds->timeout - 5) - now), ds->host_name, serial, ds->command_line);
      // free the command line we generated.
      xfree(ds->command_line);
   }
   
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
   int ret, client_match, nagios_ret = 0;
   nebstruct_service_check_data * svcdata = (nebstruct_service_check_data *)data;

   if ( event_type != NEBCALLBACK_SERVICE_CHECK_DATA )
      return OK;

   if (svcdata == 0)
   {
      dnxLog("Service handler received NULL service data structure.");
      return ERROR;  // shouldn't happen - internal Nagios error
   }

   if (svcdata->type != NEBTYPE_SERVICECHECK_INITIATE)
      return OK;  // ignore non-initiate service checks

   host * hostObj = find_host(svcdata->host_name);

   // check for local execution pattern on command line
   if (cfg.localCheckPattern && regexec(&regEx, svcdata->command_line, 0, 0, 0) == 0)
   {
      dnxDebug(1, "ehSvcCheck: (localCheckPattern match) Service for %s will execute locally: %s.", 
         hostObj->name, svcdata->command_line);
      return OK;     // tell nagios execute locally
   }

   extern check_result check_result_info;

   unsigned long long int affinity = *(dnxGetAffinity(hostObj->name));
   
   dnxDebug(4, "ehSvcCheck: [%s] Affinity flags (%llu)", hostObj->name, affinity);

   if (cfg.bypassHostgroup && (affinity & 1)) // Affinity bypass group is always the LSB
   {
      dnxDebug(1, "ehSvcCheck: (bypassHostgroup match) Service for %s will execute locally: %s.", 
         hostObj->name, svcdata->command_line);
      return OK;     // tell nagios execute locally
   }

   DnxNodeRequest * pNode = dnxCreateNodeReq();
   pNode->flags = affinity;
   pNode->hn = xstrdup(hostObj->name);
   pNode->addr = NULL;
   pNode->xid.objSerial = serial;
   pNode->xid.objSlot = -1;

   dnxDebug(4, "ehSvcCheck: Received Job [%lu:000000] at Now (%lu), Start Time (%lu).",
      serial, (unsigned long)time(0), (unsigned long)svcdata->start_time.tv_sec);
   
//    time_t now = time(0);
//    time_t expires = now + svcdata->timeout + DNX_DISPATCH_TIMEOUT;
   
   if ((ret = dnxGetNodeRequest(registrar, &pNode)) != DNX_OK) { 
   // No available workers
      if (ret == DNX_ERR_NOTFOUND) { // If NOT_FOUND we should try and queue it
         if ((ret = dnxPostNewServiceJob(joblist, serial, check_result_info.object_check_type, svcdata, pNode)) != DNX_OK) {
            dnxLog("ehSvcCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
            dnxDebug(2,"ehSvcCheck: Unable to post job, no matching dnxClients [%lu]: %s.", serial, dnxErrorString(ret));
         } else {
            dnxDebug(2, "ehSvcCheck: Service Check Queued Request");
            nagios_ret = NEBERROR_CALLBACKOVERRIDE;
         }
      } else { // We had some bad error or our time is up
         dnxDebug(1, "ehSvcCheck: No worker nodes for Host:(%s) Service:(%s).",
            pNode->hn, svcdata->command_line);
         gTopNode->jobs_rejected_no_nodes++;
      }
   } else {
   // We got a valid client worker thread
      if ((ret = dnxPostNewServiceJob(joblist, serial, check_result_info.object_check_type, svcdata, pNode)) != DNX_OK) {
         dnxLog("ehSvcCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
         dnxDebug(2, "ehSvcCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
      } else {
         nagios_ret = NEBERROR_CALLBACKOVERRIDE;
      }
   }
   
   if(nagios_ret) {
      serial++; // bump serial number
   } else {
      // We are declining the job so remove our temporary node
      dnxDeleteNodeReq(pNode);
      // Truthfully we should not fail back to Nagios, but give some sort of resource unavailable
      // error so Nagios doesn't try and execute the check itself, but that breaks how
      // DNX originally worked, so we will implement that later
   }
   return  nagios_ret;  
}

//----------------------------------------------------------------------------

/** Host Check Event Handler.
 *
 * @param[in] event_type - the event type for which we're being called.
 * @param[in] data - an opaque pointer to nagios event-specific data.
 *
 * @return Zero if we want Nagios to handle the event;
 *    NEBERROR_CALLBACKOVERRIDE indicates that we want to handle the event
 *    ourselves; any other non-zero value represents an error.
 */
static int ehHstCheck(int event_type, void * data)
{
// Global now   static unsigned long serial = 0; // the number of service checks processed

   nebstruct_host_check_data * hstdata = (nebstruct_host_check_data *)data;
   if ( event_type != NEBCALLBACK_HOST_CHECK_DATA )
      return OK;
 
   if (hstdata == 0)
   {
      dnxDebug(1, "Service handler received NULL service data structure.");
      return ERROR;  // shouldn't happen - internal Nagios error
   }
  
   if ( hstdata->type != NEBTYPE_HOSTCHECK_ASYNC_PRECHECK ){      
      return OK;  // ignore non-setup service checks
   } else {
      dnxDebug(4, "ehHstCheck: Processing Host Check");
   }

   host * hostObj = find_host(hstdata->host_name);
   char * raw_command = NULL;
   char * processed_command = NULL;
   int ret, nagios_ret = 0;


   dnxDebug(1, "ehHstCheck: type (%i) host(%s) check (%s)(%s)(%s)(%s)", 
            hstdata->type, hstdata->host_name, hstdata->command_name,
            hstdata->command_line,hstdata->command_args,hstdata->output);



    /*  Because this callback doesn't short circuit like a service check
    *   we have to intercept the event earlier in it's lifecycle
    *   this requires us to do some additional setup to put the check structs
    *   into a viable configuration
    */

	/* grab the host macro variables */
	clear_volatile_macros();
	grab_host_macros(hostObj);

	/* get the raw command line */
	get_raw_command_line(hostObj->check_command_ptr, hostObj->host_check_command, 
	    &raw_command,   0);

	if(raw_command==NULL){
		dnxDebug(1,"ehHstCheck: Raw check command for host '%s' was NULL - aborting.\n",
		    hostObj->name);
		return OK;
    }

	/* process any macros contained in the argument */
	process_macros(raw_command, &processed_command, 0);
	xfree(raw_command);
 	
	if(processed_command==NULL){
		dnxDebug(1,"ehHstCheck: Processed check command for host '%s' was NULL - aborting.\n",
		    hostObj->name);
		return OK;
   } else {
		dnxDebug(4,"ehHstCheck: Processed check command for host '%s' was (%s)",
		    hostObj->name, processed_command);
      // Set the command_line instruction
      hstdata->command_line = xstrdup(processed_command); // Leak
      xfree(processed_command);
   }

   // check for local execution pattern on command line
   if (cfg.localCheckPattern && regexec(&regEx, hstdata->command_line, 0, 0, 0) == 0)
   {
      dnxDebug(1, "ehHstCheck: (localCheckPattern match) Service for %s will execute locally: %s.", 
         hostObj->name, hstdata->command_line);
      xfree(hstdata->command_line);
      return OK;     // tell nagios execute locally
   }

   unsigned long long int affinity = *(dnxGetAffinity(hostObj->name));

   dnxDebug(3, "ehHstCheck: [%s] Affinity flags (%llu)", hostObj->name, affinity);

   if (cfg.bypassHostgroup && (affinity & 1)) // Affinity bypass group is always the LSB
   {
      dnxDebug(1, "ehHstCheck: (bypassHostgroup match) Service for %s will execute locally: %s.", 
         hostObj->name, hstdata->command_line);      
      xfree(hstdata->command_line);
      return OK;     // tell nagios execute locally
   } 
      
   DnxNodeRequest * pNode = dnxCreateNodeReq();
   pNode->flags = affinity;
   pNode->hn = xstrdup(hostObj->name);
   pNode->addr = NULL;
   pNode->xid.objSerial = serial;
   pNode->xid.objSlot = -1;
   

	/* adjust host check attempt */
	adjust_host_check_attempt_3x(hostObj, TRUE);

	/* set latency (temporarily) for macros and event broker */
// 	old_latency=hostObj->latency;
// 	hostObj->latency=latency;

	/* Set the command start time */
// 	gettimeofday(hstdata->start_time.tv_sec, NULL);
    hstdata->start_time.tv_sec = time(0);

	/* set check time for on-demand checks, so they're not incorrectly detected as being orphaned - Luke Ross 5/16/08 */
	/* NOTE: 06/23/08 EG not sure if there will be side effects to this or not.... */
//     extern int scheduled_check;
// 	if(scheduled_check==FALSE) 
// 	{
// 		hostObj->next_check=hstdata->start_time.tv_sec;
//     } else {
// 	/* clear check options - we don't want old check options retained */
// 	/* only clear options if this was a scheduled check - on demand check options shouldn't affect retained info */
// 		hostObj->check_options=CHECK_OPTION_NONE;
//     }

	/* increment number of host checks that are currently running... */
	extern int currently_running_host_checks;
	currently_running_host_checks++;
   dnxDebug(4,"ehHstCheck: Host checks in progress (%i)", 
        currently_running_host_checks);

	/* set the execution flag */
	hostObj->is_executing=TRUE;
	
   if ((ret = dnxGetNodeRequest(registrar, &pNode)) != DNX_OK) { // If OK we dispatch
      // If NOT_FOUND we should try and queue it
      if (ret == DNX_ERR_NOTFOUND) {    
         if ((ret = dnxPostNewHostJob(joblist, serial, HOST_CHECK, hstdata, pNode)) != DNX_OK) {
            dnxLog("ehHstCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
            dnxDebug(2,"ehHstCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
            xfree(hstdata->command_line);
         } else {
            dnxDebug(5, "ehHstCheck: Host Check Queued Request");
            nagios_ret = NEBERROR_CALLBACKOVERRIDE;
         }
      } else {// We had some bad error or our time is up
         dnxDebug(1, "ehHstCheck: No worker nodes for Host:(%s) Service:(%s).",
            pNode->hn, hstdata->command_line);
         xfree(hstdata->command_line);
         gTopNode->jobs_rejected_no_nodes++;
      }
   } else {
      if ((ret = dnxPostNewHostJob(joblist, serial, HOST_CHECK, hstdata, pNode)) != DNX_OK)
      {
         dnxLog("ehHstCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
         dnxDebug(2,"ehHstCheck: Unable to post job [%lu:000000]: %s.", serial, dnxErrorString(ret));
         xfree(hstdata->command_line);
      } else {
         nagios_ret = NEBERROR_CALLBACKOVERRIDE;
      }
   }


   if(nagios_ret) {
      serial++; // bump serial number
   } else {
      // We are declining the job so remove our temporary node
      dnxDeleteNodeReq(pNode);
      // Truthfully we should not fail back to Nagios, but give some sort of resource unavailable
      // error so Nagios doesn't try and execute the check itself, but that breaks how
      // DNX originally worked, so we will have to implement that later
   }
   return  nagios_ret;  
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
   neb_deregister_callback(NEBCALLBACK_HOST_CHECK_DATA, ehHstCheck);
//   neb_deregister_callback(NEBTYPE_PROCESS_EVENTLOOPEND, dnxCleanup);
   // ensure we don't destroy non-existent objects from here on out...
   if (registrar)
      dnxRegistrarDestroy(registrar);

   if (dispatcher)
      dnxDispatcherDestroy(dispatcher);

   if (collector)
      dnxCollectorDestroy(collector);

   if (joblist)
      dnxJobListDestroy(joblist);
      
   // Should make sure that the affinity list is freed


   // it doesn't matter if we haven't initialized the
   // channel map - it can figure that out for itself
   dnxChanMapRelease();

   releaseConfig();

    //SM 09/08 DnxNodeList
   dnxNodeListDestroy();
   //SM 09/08 END DnxNodeList

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
   hostGrpAffinity = (DnxAffinityList *)malloc(sizeof(DnxAffinityList));
   hostAffinity = (DnxAffinityList *)malloc(sizeof(DnxAffinityList));
   hostAffinity->flag = 0ULL;
   hostAffinity->name = NULL;
   hostAffinity->next = hostAffinity;
   hostGrpAffinity->flag = 0ULL;
   hostGrpAffinity->name = NULL;
   hostGrpAffinity->next = hostGrpAffinity;
   DnxAffinityList * temp_aff;
   hostgroup * hostgroupObj;
   DNX_PT_MUTEX_INIT(&submitCheckMutex);

   if ((ret = dnxChanMapInit(0)) != 0)
   {
      dnxLog("Failed to initialize channel map: %s.", dnxErrorString(ret));
      return ret;
   }
   
   // These need to be initialized before threads start trying to record stats....
   gTopNode = dnxNodeListCreateNode("127.0.0.1", "localhost");
//    dnxNodeListSetNodeAffinity("127.0.0.1", "localhost");

   // Create the list of affinity groups (Nagios Hostgroups)
   // Get the list of host groups
   extern hostgroup *hostgroup_list;
   hostgroup * temp_hostgroup;
   // Create affinity linked list
   unsigned long long int flag = 2ULL;
   for (temp_hostgroup=hostgroup_list; temp_hostgroup!=NULL; temp_hostgroup=temp_hostgroup->next) 
   {
     dnxDebug(1, "dnxServerInit: Entering hostgroup init loop: %s", temp_hostgroup->group_name);
     if(strcmp(cfg.bypassHostgroup, temp_hostgroup->group_name)==0) {
        // This is the bypass group and should be assigned the NULL flag
        dnxAddAffinity(hostGrpAffinity, temp_hostgroup->group_name, 1);
        dnxDebug(1, "dnxServerInit: (bypassHostgroup match) Service for %s hostgroup will execute locally.", 
        temp_hostgroup->group_name);
     } else {
        dnxDebug(1, "dnxServerInit: Hostgroup [%s] uses (%llu) flag.", temp_hostgroup->group_name, flag);
        dnxAddAffinity(hostGrpAffinity, temp_hostgroup->group_name, flag); 
        flag <<= 1ULL;
     }
   }

   /* Note:
      We need to change this flag system so that
         A) The flag bit's represent dnxClients instead of hostgroups
         B) The check looks at the hostgroup being used, not the host
            so that a host can be a member of several groups, but the check
            will always go to a node designed to handle that check.
   */
   // Create initial host list
   extern host *host_list;
   host * temp_host;
   for (temp_host=host_list; temp_host!=NULL; temp_host=temp_host->next ) 
   {
      dnxDebug(2, "Adding host [%s] to hostAffinity cache.", temp_host->name);
      flag = 0ULL;
      temp_aff = hostGrpAffinity;
      while (temp_aff != NULL) {
         // Recurse through the affinity list
         dnxDebug(6, "dnxServerInit: Recursing affinity list - [%s] = (%llu)", 
         temp_aff->name, temp_aff->flag);
         // Is host in this group?
         hostgroupObj = find_hostgroup(temp_aff->name);
         if(is_host_member_of_hostgroup(hostgroupObj, temp_host))
         {
            flag |= temp_aff->flag;
            dnxDebug(2, "dnxServerInit: matches [%s] flag is now (%llu)", temp_aff->name, flag);
         } else {
            dnxDebug(6, "dnxServerInit: no match with [%s]", temp_aff->name);
         }
         temp_aff = temp_aff->next;
      }
      dnxAddAffinity(hostAffinity, temp_host->name, flag);
   }
   
   unsigned long long clientless = 0ULL;
   // Make a bitmask where the 'holes' represent non-dnxClient hostgroups
   // by bitwise OR ing all the dnxClients
   for (temp_host=host_list; temp_host!=NULL; temp_host=temp_host->next ) 
   {
      flag = *(dnxGetAffinity(temp_host->name));
      if(dnxIsDnxClient(flag)) {
         clientless |= flag;
         dnxDebug(2, "dnxServerInit: [%s] is a dnxClient  covered groups now (%llu)", temp_host->name, clientless);
      }
   }
  
   // Check a hosts bitmask flag against the clientless hostgroups flag
   // and if it's not covered by a dnxClient, force it into the locals group
   // FIXME?
   for (temp_host=host_list; temp_host!=NULL; temp_host=temp_host->next ) 
   {
      flag = *(dnxGetAffinity(temp_host->name));
      if((flag | clientless) != clientless) {
         dnxDebug(2, "dnxServerInit: [%s] is in a hostgroup with no dnxClient",
            temp_host->name);
         dnxAddAffinity(hostAffinity, temp_host->name, 1ULL);
      }
   }
 
   joblistsz = dnxCalculateJobListSize();

   dnxLog("Allocating %d service request slots in the DNX job list.", joblistsz);

   if ((ret = dnxJobListCreate(joblistsz, &joblist)) != 0)
   {
      dnxLog("Failed to initialize DNX job list with %d slots.", joblistsz);
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

   pthread_t tid;
   if ((ret = pthread_create(&tid, NULL, (void *(*)(void *))dnxStatsRequestListener, NULL)) != 0)
   //if ((ret = pthread_create(&tid, 0, dnxStatsRequestListener, NULL)) != 0)
   {
      dnxLog("dnx dnxServerInit: thread creation failed for stats listener: %s.",dnxErrorString(ret));
      ret = DNX_ERR_THREAD;
   }

   // registration for this event starts everything rolling
   neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, myHandle, 0, ehSvcCheck);
   dnxLog("Registered for SERVICE_CHECK_DATA event.");
   neb_register_callback(NEBCALLBACK_HOST_CHECK_DATA, myHandle, 0, ehHstCheck);
   dnxLog("Registered for HOST_CHECK_DATA event.");

   dnxLog("Server initialization completed.");

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
      dnxLog("Failed to exec script: %s.", strerror(errno));
      ret = DNX_ERR_INVALID;
   }
   else
      ret = DNX_OK;

   dnxLog("Sync script returned %d.", WEXITSTATUS(ret));

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
      dnxLog("Startup handler received NULL process data structure.");
      return ERROR;
   }

   // look for process event loop start event
   if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPSTART)
   {
      dnxDebug(2, "Startup handler received PROCESS_EVENTLOOPSTART event.");

      // execute sync script, if defined
      if (cfg.syncScript)
      {
         dnxLog("Startup handler executing plugin sync script: %s.", cfg.syncScript);

         // NB: This halts Nagios execution until the script exits...
         launchScript(cfg.syncScript);
      }

      // if server init fails, do server shutdown
      if (dnxServerInit() != 0)
         dnxServerDeInit();
   }

   // look for process event loop end event
   if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPEND)
   {
      dnxDebug(2, "Startup handler received PROCESS_EVENTLOOPEND event.");
      // See if we have any outstanding checks and get them back, we may need
      // to save state or write out the checks to a temp file in the queue
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
      dnxDebug(1, "dnxJobCleanup: Job [%lu:%lu] object freed for (%s) [%s].", 
            pJob->xid.objSerial, pJob->xid.objSlot, pJob->host_name, pJob->pNode->addr);
      xfree(pJob->cmd);
      pJob->cmd = NULL;
      xfree(pJob->host_name);
      pJob->host_name = NULL;
      xfree(pJob->service_description);
      pJob->service_description = NULL;
      pJob->state = DNX_JOB_NULL;
      dnxDeleteNodeReq(pJob->pNode);
      pJob->pNode = NULL;
   }
   else
   {
      dnxDebug(1, "dnxJobCleanup: Unable to free job.");
   }
}

//----------------------------------------------------------------------------

int dnxAuditJob(DnxNewJob * pJob, char * action)
{
   /*
   if (cfg.auditFilePath)
   {
      struct sockaddr_in src_addr;
      in_addr_t addr;

      // Convert opaque Worker Node address to IPv4 address

      /** @todo This conversion should take place in the dnxUdpRead function
       * and the resultant address string stored in the DnxNewJob
       * structure. This would have two benefits:
       *
       *    1. Encapsulates conversion at the protocol level.
       *    2. Saves some time during logging.
       */
      //memcpy(&src_addr, pJob->pNode->address, sizeof(src_addr));
      //addr = ntohl(src_addr.sin_addr.s_addr);

    /*
      return dnxAudit(
            "%s: Job %lu: Worker %u.%u.%u.%u-%lx: %s",
                  action, pJob->xid.objSerial,
                  (unsigned)((addr >> 24) & 0xff),
                  (unsigned)((addr >> 16) & 0xff),
                  (unsigned)((addr >>  8) & 0xff),
                  (unsigned)( addr        & 0xff),
                  pJob->pNode->xid.objSlot, pJob->cmd);

    */
   //}

   dnxLog("%s: Job %lu: Worker %s-%lx: %s, %s",action, pJob->xid.objSerial,pJob->pNode->addr,pJob->pNode->xid.objSlot, pJob->service_description, pJob->cmd);
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
   dnxLog("-------- DNX Server Module Shutdown Initiated --------");
   dnxServerDeInit();

   xheapchk();

   dnxLog("-------- DNX Server Module Shutdown Completed --------");
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

   gTopDCS = dnxComStatCreateDCS("127.0.0.1");
   myHandle = handle;

   // module args string should contain a fully-qualified config file path
   if (!args || !*args)
      args = DNX_DEFAULT_SERVER_CONFIG_FILE;

   if ((ret = initConfig(args)) != 0)
      return ERROR;

   // set configured debug level and syslog log facility code
   dnxLogInit(cfg.logFilePath, cfg.debugFilePath, cfg.auditFilePath,
         &cfg.debugLevel);

   dnxLog("-------- DNX Server Module Version %s Startup --------", VERSION);
   dnxLog("Copyright (c) 2006-2008 Intellectual Reserve. All rights reserved.");
   dnxLog("Configuration file: %s.", args);
   if (cfg.auditFilePath)
      dnxLog("Auditing enabled to %s.", cfg.auditFilePath);
   if (cfg.debugLevel)
      dnxLog("Debug logging enabled at level %d to %s.",
            cfg.debugLevel, cfg.debugFilePath);

   // subscribe to PROCESS_DATA call-backs in order to defer initialization
   //    until after Nagios validates its configuration and environment.
   if ((ret = neb_register_callback(NEBCALLBACK_PROCESS_DATA,
         myHandle, 0, ehProcessData)) != OK)
   {
      dnxLog("PROCESS_DATA event registration failed: %s.", dnxErrorString(ret));
      releaseConfig();
      return ERROR;
   }
   start_time = time(0);

   dnxLog("-------- DNX Server Module Startup Complete --------");

   return OK;
}

/*--------------------------------------------------------------------------*/
//Added 09/08 SM dnxNode

/** Append text to a string by reallocating the string buffer.
 *
 * This is a var-args function. Additional parameters following the @p fmt
 * parameter are based on the content of the @p fmt string.
 *
 * @param[in] spp - the address of a dynamically allocated buffer pointer.
 * @param[in] fmt - a printf-like format specifier string.
 *
 * @return Zero on success, or DNX_ERR_MEMORY on out-of-memory condition.
 */
static int appendString(char ** spp, char * fmt, ... )
{
   char buf[1024];
   char * newstr;
   size_t strsz;
   va_list ap;

   // build new string
   va_start(ap, fmt);
   vsnprintf(buf, sizeof buf, fmt, ap);
   va_end(ap);

   // reallocate buffer; initialize if necessary
   strsz = strlen(buf) + 1;
   if ((newstr = xrealloc(*spp, (*spp? strlen(*spp): 0) + strsz)) == 0)
      return DNX_ERR_MEMORY;
   if (*spp == 0)
      *newstr = 0;

   // concatenate new string onto exiting string; return updated pointer
   strcat(newstr, buf);
   *spp = newstr;
   return 0;
}
/*--------------------------------------------------------------------------*/

void trim(char * str, char c)
{
       size_t len = strlen(str);
       if (len && str[len - 1] == c) str[len - 1] = 0;
}

void buildStatsReplyForNode(DnxNode* pDnxNode, char* requested_stat, DnxMgmtReply* pReply)
{

    assert(requested_stat);
    static int pass = 1;

    int i,count = 0;

    char * token = requested_stat;
    //char * token = strtok(requested_action,",");
    assert(token);

    unsigned node_count= dnxNodeListCountNodes();

    bool allstats = (strncmp("ALLSTATS",token,strlen(token)) ==0);
    bool match = false;

    if(!pDnxNode)
    {
            pDnxNode = gTopNode;
            pass = 0;
    }

    DCS * pDCS = dnxComStatFindDCS(pDnxNode->address);
    unsigned packets_in = 0;
    unsigned packets_out = 0;
    unsigned packets_failed = 0;

    if(pDCS)
    {
            packets_in = pDCS->packets_in;
            packets_out = pDCS->packets_out;
            packets_failed = pDCS->packets_failed;
    }



    //Create a struct to hold all possible responses
    struct { char * str; unsigned * stat; } response_struct[] =
    {
        { "job_requests_recieved",  &pDnxNode->jobs_req_recv           },
        { "jobs_dispatched",        &pDnxNode->jobs_dispatched         },
        { "jobs_handled",           &pDnxNode->jobs_handled            },
        { "job_requests_expired",   &pDnxNode->jobs_req_exp            },
        { "jobs_rejected_no_nodes", &pDnxNode->jobs_rejected_no_nodes  },
        { "jobs_rejected_no_memory",&pDnxNode->jobs_rejected_oom       },
        { "packets_out",            &packets_out                       },
        { "packets_in",             &packets_in                        },
        { "packets_failed",         &packets_failed                    },
        { "nodes_registered",       &node_count                        },
    };



    //Lets build our Stats Reply
    //do
    //{

        //They want to clear stats on a node
        if(strncmp("CLEAR",token,strlen(token))==0)
        {
            if(strncmp(pDnxNode->address,"127.0.0.1",strlen(pDnxNode->address)) != 0)
            {
                appendString(&pReply->reply, "Reset Node %s\n",pDnxNode->address);
                dnxComStatClear(pDnxNode->address);
                dnxNodeListRemoveNode(pDnxNode);
            }else{
                appendString(&pReply->reply,"Error: Cannot Clear Top Node, did you mean reset instead?\n");
            }
            return;
        }

        //They want to reset a node
        if(strncmp("RESETSTATS",token,strlen(token))==0)
        {
            appendString(&pReply->reply, "Reseting All Nodes\n");
            dnxComStatReset();
            dnxNodeListReset();
            return;
        }

        //They want help
        if(strncmp("HELP",token,strlen(token))==0)
        {
            appendString(&pReply->reply,"HELP: Format is [node ip address* (optional)], HELP, CLEAR, RESETSTATS, ALLSTATS, AFFINITY");
            return;
        }

       //build the response by looping through the response_struct looking for matching values
       for (i = 0; i < elemcount(response_struct); i++)
        {
            dnxDebug(2,"buildStatsReply: request = %s\n",token);
            dnxDebug(2,"buildStatsReply: element = %s\n",response_struct[i].str);
            //If it's help or we need the headers do this
            if(strncmp("help",token,strlen(token))==0 || (allstats && pass ==0))
            {
                appendString(&pReply->reply,"%s,",response_struct[i].str);
                count++;
            }else{
                //Otherwise lets get those values out of that struct
                match = (strncmp(response_struct[i].str,token,strlen(token)) == 0);

                if (match || allstats)
                {
                    count++;
                    dnxDebug(2,"buildStatsReply: Found a match for request %s value is %u\n",token,*response_struct[i].stat);
                    if (appendString(&pReply->reply, "%u,", *response_struct[i].stat) != 0)
                    {
                        dnxDebug(2,"buildStatsReply: Error! appendString Failed!\n");
                    }

                    //We found what we were looking for, lets get out of here, unless of course allstats is true
                    if(!allstats)
                       break;
                }
            }
        }

        //Place the word NULL in for values not found
        if(!count)
        {
            appendString(&pReply->reply, "NULL,");
        }

        count = 0;
        pass++;

    //}while(token = strtok(NULL,","));
}


/** Build an allocated response buffer for requested stats values.
 *
 * @param[in] request - The requested stats in comma-separated string format.
 * @param[in] pReply - A pointer to an allocated reply buffer
 * @return false if out of memory true otherwise
 */
bool buildStatsReply(char * request, DnxMgmtReply * pReply)
{

    assert(request);

    DnxNode * pDnxNode = gTopNode->next; // skip the first node.
    DnxAffinityList * temp_aff;
    temp_aff = hostAffinity;   // Temp hostlist

    char * response = NULL;
    char * token = NULL;
    char * action; // = (char*) xcalloc(DNX_MAX_MSG+1,sizeof(char));

    int count = 0;
    DnxXmlBuf xreq_buf;
    unsigned i;

    pthread_mutex_t mutex;
    DNX_PT_MUTEX_INIT(&mutex);

    dnxDebug(2,"buildStatsReply:  Request is %s",request);

    //De XMLify request
    strcpy(xreq_buf.buf,request);
    xreq_buf.size = strlen(request);
    dnxXmlGet(&xreq_buf, "XID", DNX_XML_STR, &pReply->xid);
    dnxXmlGet(&xreq_buf, "Action", DNX_XML_STR, &action);
    pReply->status = DNX_REQ_ACK;

    // search table for sub-string, append requested stat to response
    DNX_PT_MUTEX_LOCK(&mutex);
        if(strcmp("AFFINITY",action) == 0){
            do {
                appendString(&pReply->reply,"dnxClient (%s) IP: [%s]  Hostgroup flag [%llu]\n", 
                  pDnxNode->hostname, pDnxNode->address, pDnxNode->flags);
            } while (pDnxNode = pDnxNode->next);
            
            do {
                appendString(&pReply->reply,"host (%s) Hostgroup flag [%llu]\n", temp_aff->name, temp_aff->flag);
            } while (temp_aff = temp_aff->next);
        }
        else
        {
            if(strcmp("ALLSTATS",action) == 0)
            {
                //The requested action is the keyword ALLSTATS, short circuit all normal functionality and just dump all the stats
    
                //Build the header
    
                    appendString(&pReply->reply,"IP ADDRESS: ");
                    buildStatsReplyForNode(NULL,action,pReply);
                    trim(pReply->reply,',');
                    appendString(&pReply->reply,"\n");
                //Build the response by looping through all the nodes in order
                do
                {
                    appendString(&pReply->reply,"%s,",pDnxNode->address);
                    buildStatsReplyForNode(pDnxNode,action,pReply);
                    trim(pReply->reply,',');
                    appendString(&pReply->reply,"\n");
                }while(pDnxNode = pDnxNode->next);
    
            }
            else
            {
    
               //Get first token
                token = strtok(action,",");
    
                do
                {
                     //Check request to see if it's an IP address
                    if(strchr(token,'.'))
                    {
                        dnxLog("buildStatsReply: Request appears to contain an IP address, address is %s",token);
    
                        //it does contain an IP address so we want to find the DnxNode with that address and set pDnxNode
                        pDnxNode = dnxNodeListFindNode(token);
    
                        if(!pDnxNode)
                        {
                            //We couldn't find a node for that IP address
                            appendString(&pReply->reply, "%s","Invalid Worker Node Requested");
                            return false;
                        }else{
                            //We did find a node for it
                            //Prefix the result with an IP address
                            appendString(&pReply->reply, "%s,",token);
                        }
                    }else{
                        buildStatsReplyForNode(pDnxNode,token,pReply);
                    }
                }while(token = strtok(NULL,","));
            }
        }
        //Get rid of that very annoying trailing comma

        if (pReply->reply)
        {
            trim(pReply->reply,',');
            appendString(&pReply->reply,"\n");
            dnxDebug(2,"buildStatsReply: Response completed, response is:\n%s\n",pReply->reply);
        }

    DNX_PT_MUTEX_UNLOCK(&mutex);
    xfree(action);
    return true;
}

/*--------------------------------------------------------------------------*/

/** dnxStatsRequestListener
*   Start the stats request listener and run it.
*   @return void : returns nothing
*/
static void * dnxStatsRequestListener(void * vpargs)
{
    dnxLog("dnxStatsRequestListener: Starting up!\n");
    int maxsize = DNX_MAX_MSG; //This is an ugly hack we have to do this because dnxGet requires a point to an INT and DNX_MAX_MSG is a macro
    bool ret = false;
    int timeout = 0;
    bool quit = false;
    char *pHost = xstrdup("127.0.0.1");
    char *pPort = xstrdup("12482");
    bool result;
    DnxMgmtReply reply;

//     if(!gTopNode)
//         gTopNode = dnxNodeListCreateNode(pHost);


    dnxDebug(2,"dnxStatsRequestListener: init comm sub-system\n");

    char url[1024];
    snprintf(url, sizeof url, "udp://%s:%s", pHost, pPort);
    dnxLog("dnxStatsRequestListener: Adding Channel Map\n");
    xfree(pHost);
    xfree(pPort);
    if ((ret = dnxChanMapAdd("StatsServer", url)) != 0)
    {
        dnxLog("dnxStatsRequestListener Error: adding channel (%s): %s.\n", url, dnxErrorString(ret));
    }else{
        dnxDebug(2,"dnxStatsRequestListener: Connecting Channel!\n");
        DnxChannel * channel;
        if ((ret = dnxConnect("StatsServer", 0, &channel)) != 0)
        {
            dnxLog("dnxStatsRequestListener Error: opening stats listener (%s): %s.\n", url, dnxErrorString(ret));
        }else{
            while(!quit)
            {
               struct sockaddr_in * addr = (struct sockaddr_in*) xcalloc(1,sizeof(struct sockaddr_in));
               
               char * buf = (char*) xcalloc(DNX_MAX_MSG+1,sizeof(char));
               reply.reply = (char*) xcalloc(DNX_MAX_MSG+1,sizeof(char));
               
               dnxDebug(2,"dnxStatsRequestListener: Listening For Data!\n");
               if ((ret = dnxGet(channel, buf, &maxsize, timeout, (char *)addr)) != DNX_OK)
               {
                  quit = true;
                  dnxLog("dnxStatsRequestListener Error: Error reading from socket, data retrieved if any was %s\n", buf);
               }else{
                 
                  int maxlen = INET_ADDRSTRLEN + 1;
                  pHost = (char *)xcalloc(maxlen,sizeof(char));
                  inet_ntop(AF_INET, &(((struct sockaddr_in *)addr)->sin_addr), pHost, maxlen); 
               
                  dnxDebug(2,"dnxStatsRequestListener: Recieved a request from %s, request was %s\n", pHost, buf);
                  result = buildStatsReply(buf, &reply);
                  if(result)
                  {
                     dnxDebug(2,"dnxStatsRequestListener:  Source of request is %s", pHost);
               
                     if(dnxSendMgmtReply(channel, &reply, (char *)addr) != 0)
                     {
                        dnxLog("dnxStatsRequestListener Error: Error writing to socket for reply to %s\n",pHost);
                     }else{
                        dnxDebug(2,"dnxStatsRequestListener: Sent requested data to source %s, reply was %s\n", pHost, reply.reply);
                     }
                  }else{
                     dnxLog("dnxStatsRequestListener Error: building stats result failed, stats result was NULL\n");
                  }
               }

               maxsize = DNX_MAX_MSG; //We have to do this because dnxUdpRead is changing the size of the maxsize variable to whatever was read from last time.
               xfree(buf);
               xfree(addr);
               xfree(pHost);
 
               if(reply.reply)
               {
                  xfree(reply.reply);
               }else{
                  dnxLog("dnxStatsRequestListener Error: reply had a length less thqan or equal to 0 or reply was NULL\n");
                  quit = true;
               }
            }
            dnxDisconnect(channel);
        }
        dnxChanMapDelete("StatsServer\n");
    }
    xheapchk();
    dnxLog("dnxStatsRequestListener: Exiting Listener!\n");
    return NULL;
}
//End dnxNode changes 09/08

//----------------------------------------------------------------------------

unsigned long long int* dnxGetAffinity(char * name)
{

   dnxDebug(6, "dnxGetAffinity: entering with [%s]", name);
   extern hostgroup *hostgroup_list;
   hostgroup * hostgroupObj;
   unsigned long long int* pFlag = (unsigned long long int*) xcalloc (1, sizeof(unsigned long long int));
   short int match = 0;
   DnxAffinityList * temp_aff;
   temp_aff = hostAffinity;   // We are probably looking for a host or dnxClient

   if(name == NULL) {
        // We were passed either the local host or an unnamed (old) client
      // This is a dnxClient that is unaffiliated with a hostgroup
      // the default behavior should be that it can handle all requests
      // for backwards compatibility. This is dangerous though as a rogue or
      // misconfigured client could steal requests that it can't service.
      *pFlag = (unsigned long long)-2; // Match all affinity but local(LSB)
      dnxAddAffinity(hostAffinity, name, *pFlag);
      dnxDebug(2, "dnxGetAffinity: Adding unnamed dnxClient to host cache with (%llu) flags."
      " This host is not a member of any hostgroup and will service ALL requests!", *pFlag);
      return pFlag;
   }

   host * hostObj = find_host(name); 

   if(!hostObj) {
      // We might be looking for a specific affinity group flag otherwise it is
      // a dynamically registered dnxClient that isn't in the Nagios hostlist
      hostgroupObj = find_hostgroup(name);
      if(hostgroupObj) {
         temp_aff = hostGrpAffinity;
      }
   }

   // Check the host cache first
   while (temp_aff != NULL) {
      if(temp_aff->name == NULL) { break; }
      dnxDebug(6, "dnxGetAffinity: Checking cache for [%s]", name);
      if (strcmp(temp_aff->name, name) == 0) { // We have a cached copy so return
         dnxDebug(4, "dnxGetAffinity: Found [%s] in cache with (%llu) flags.", name, temp_aff->flag);
         xfree(pFlag);
         return &temp_aff->flag;
      }
      temp_aff = temp_aff->next;
   }

   // This is the first time we've seen this host/dnxClient
   temp_aff = hostGrpAffinity;
   while (temp_aff != NULL) {
      if(temp_aff->name == NULL) { break; }
      // Recurse through the host group affinity list
      dnxDebug(6, "dnxGetAffinity: Recursing Host Group list - [%s] = (%llu)", 
      temp_aff->name, temp_aff->flag);

      // Is host in this group?
      hostgroupObj = find_hostgroup(temp_aff->name);
      if(is_host_member_of_hostgroup(hostgroupObj, hostObj)) {
         *pFlag |= (unsigned long long) temp_aff->flag;
         match++;
         dnxDebug(4, "dnxGetAffinity: matches [%s] flag is now (%llu)", temp_aff->name, *pFlag);
      } else {
         dnxDebug(6, "dnxGetAffinity: no match with [%s]", temp_aff->name);
      }
      temp_aff = temp_aff->next;
   }

   if(match)
   {
      // Push this into the host cache
      dnxAddAffinity(hostAffinity, name, *pFlag);
      dnxDebug(2, "dnxGetAffinity: Adding [%s] dnxClient to host cache with (%llu) flags.",
         name, *pFlag);
      return pFlag;
   } else {
      // This is a dnxClient that is unaffiliated with a hostgroup
      // the default behavior should be that it can handle all requests
      // for backwards compatibility. This is dangerous though as a rogue or
      // misconfigured client could steal requests that it can't service.
      *pFlag = (unsigned long long)-2; // Match all affinity but local(LSB)
      dnxAddAffinity(hostAffinity, name, *pFlag);
      dnxDebug(2, "dnxGetAffinity: Adding [%s] dnxClient to host cache with (%llu) flags."
      " This host is not a member of any hostgroup and can service ALL requests!",
         name, *pFlag);
      return pFlag; 
   }
}

/*--------------------------------------------------------------------------*/
// This is a Hamming Weight function that will count the number of flags set 
// in the affinity bitmask.

int dnxHammingWeight(unsigned long long x) {
    unsigned long long count;
    unsigned long long y = x;
    for (count=0; y; count++)
        y &= y - 1ULL;
    return count; // Returns the number of binary 1's in a bitmask
}

int dnxIsDnxClient(unsigned long long x) {
   // If we are in more than 1 hostgroup and also in the local checks
   // we are a dnxClient
    unsigned long long y = x;      
    if ((dnxHammingWeight(y) > 1) && (y & 1ULL)) {
        return 1;
    } else {
        return 0;
    }
}

DnxRegistrar * dnxGetRegistrar() {
   return registrar;
}

char * dnxGetHostgroupFromFlags (unsigned long long host, unsigned long long client) {
   if(host == 1ULL && cfg.bypassHostgroup != NULL) {
      // If the host is only in the bypass group, there is no need to do a lookup
      dnxDebug(2, "dnxGetHostgroupFromFlags: Host is only in bypass group (%s)",
         cfg.bypassHostgroup);
      return cfg.bypassHostgroup;
   }
   
   unsigned long long int flagUnion = host & client;
   if (!flagUnion) {
//       if(dnxHammingWeight(host) == 1) {
//          flagUnion = host;
//       }
      return NULL;
   }
   
   DnxAffinityList * temp_aff;
   temp_aff = hostGrpAffinity;
   while (temp_aff != NULL) {
      // Recurse through the hostgroup affinity list
      dnxDebug(6, "dnxGetHostgroupFromFlags: Recursing hostgroup affinity list - [%s] = (%llu)", 
      temp_aff->name, temp_aff->flag);
      // Is host in this group?
      if(flagUnion & temp_aff->flag) {
         dnxDebug(3, "dnxGetHostgroupFromFlags: Found host in (%s)",  temp_aff->name);
         return temp_aff->name;
      }
      temp_aff = temp_aff->next;
   }
   
}
