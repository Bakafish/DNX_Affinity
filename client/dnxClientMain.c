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

/** Main source file for DNX Client.
 * 
 * @file dnxClientMain.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IMPL
 */

/*!@defgroup DNX_CLIENT_IMPL DNX Client Implementation 
 * @defgroup DNX_CLIENT_IFC  DNX Client Interface
 */

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxCfgParser.h"
#include "dnxWLM.h"
#include "dnxPlugin.h"
#include "dnxLogging.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION "<unknown>"
#endif

#ifndef SYSCONFDIR
# define SYSCONFDIR "/etc"
#endif

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_DEFAULT_NODE_CONFIG_FILE   SYSCONFDIR "/dnxClient.cfg"

typedef struct DnxCfgData
{
   char * channelAgent;          //!< The agent management channel URL.
   char * logFacility;           //!< The syslog facility, as a string.
   char * pluginPath;            //!< The file system plugin path.
   unsigned debugLevel;          //!< The system global debug level.
   DnxWlmCfgData wlm;            //!< WLM specific configuration data.
} DnxCfgData;

// module statics
static DnxCfgData s_cfg;         //!< The system configuration parameters.
static DnxWlm * s_wlm;           //!< The system worker thread pool.
static DnxChannel * s_agent;     //!< The agent management channel.
static char * s_progname;        //!< The base program name.
static char * s_cfgfile;         //!< The system configuration file name.
static int s_dbgflag = 0;        //!< The system debug flag.
static int s_shutdown = 0;       //!< The shutdown signal flag.
static int s_reconfig = 0;       //!< The reconfigure signal flag.
static int s_debugsig = 0;       //!< The debug toggle signal flag.
static int s_lockfd = -1;        //!< The system PID file descriptor.
static int s_logFacility;        //!< The syslog facility code as an int.

/** The system default configuration parameters. */
static char * s_cfgdefs[] = 
{
   "channelAgent           = udp://0:12480",
   "poolInitial            = 20",
   "poolMin                = 20",
   "poolMax                = 300",
   "poolGrow               = 10",
   "wlmPollInterval        = 2",
   "wlmShutdownGracePeriod = 35",
   "threadRequestTimeout   = 5",
   "threadMaxRetries       = 12",
   "threadTtlBackoff       = 1",
   "maxResultBuffer        = 1024",
   "logFacility            = LOG_LOCAL7",
   0,
};

/** The configuration parser dictionary. */
static DnxCfgDict s_dict[] = 
{
   { "channelAgent",           DNX_CFG_URL      },
   { "channelDispatcher",      DNX_CFG_URL      },
   { "channelCollector",       DNX_CFG_URL      },
   { "poolInitial",            DNX_CFG_UNSIGNED },
   { "poolMin",                DNX_CFG_UNSIGNED },
   { "poolMax",                DNX_CFG_UNSIGNED },
   { "poolGrow",               DNX_CFG_UNSIGNED },
   { "wlmPollInterval",        DNX_CFG_UNSIGNED },
   { "wlmShutdownGracePeriod", DNX_CFG_UNSIGNED },
   { "threadRequestTimeout",   DNX_CFG_UNSIGNED },
   { "threadMaxRetries",       DNX_CFG_UNSIGNED },
   { "threadTtlBackoff",       DNX_CFG_UNSIGNED },
   { "maxResultBuffer",        DNX_CFG_UNSIGNED },
   { "logFacility",            DNX_CFG_STRING   },
   { "pluginPath",             DNX_CFG_FSPATH   },
   { "debugLevel",             DNX_CFG_UNSIGNED },
   { 0 },
};

/** The array of cfg variable addresses for the configuration parser. */
static void * s_ppvals[] =
{
   &s_cfg.channelAgent,
   &s_cfg.wlm.dispatcher,
   &s_cfg.wlm.collector,
   &s_cfg.wlm.poolInitial,
   &s_cfg.wlm.poolMin,
   &s_cfg.wlm.poolMax,
   &s_cfg.wlm.poolGrow,
   &s_cfg.wlm.pollInterval,
   &s_cfg.wlm.shutdownGrace,
   &s_cfg.wlm.reqTimeout,
   &s_cfg.wlm.maxRetries,
   &s_cfg.wlm.ttlBackoff,
   &s_cfg.wlm.maxResults,
   &s_cfg.logFacility,
   &s_cfg.pluginPath,
   &s_cfg.debugLevel,
};

//----------------------------------------------------------------------------

/** Display program usage text to STDERR and exit with an error.
 */
static void usage(void)
{
   fprintf(stderr, "\nUsage: %s [-c config-file] [-d] [-v]\n", s_progname);
   fprintf(stderr, "\nWhere:\n");
   fprintf(stderr, "\t-c Specify the location of the config file\n");
   fprintf(stderr, "\t-d Enable debug mode (NOTE: Will not become a background daemon)\n");
   fprintf(stderr, "\t-v Display version and exit\n\n");
   exit(1);
}

//----------------------------------------------------------------------------

/** Display program version information to STDOUT and exit successfully.
 */
static void version(void)
{
   printf("%s %s\n", s_progname, VERSION);
   exit(0);
}

//----------------------------------------------------------------------------

/** Parse command line options.
 * 
 * Options:
 *    -c <config-file>     assign configuration file
 *    -d                   enable debug functionality
 *    -v                   display version information and exit
 *     *                   display usage information and exit
 * 
 * @param[in] argc - the number of elements in the @p argv array.
 * @param[in] argv - a null-terminated array of command-line arguments.
 */
static void getOptions(int argc, char ** argv)
{
// extern int optind;
   extern char * optarg;
   extern int opterr, optopt;

   int ch;

   opterr = 0; /* Disable error messages */

   while ((ch = getopt(argc, argv, "c:dv")) != -1)
   {
      switch (ch)
      {
      case 'c':
         s_cfgfile = optarg;
         break;
      case 'd':
         s_dbgflag = 1;
         break;
      case 'v':
         version();
         break;
      default:
         usage();
      }
   }
}

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
   static struct FacilityCode { char * str; int val; } facodes[] = 
   {
      { "LOG_LOCAL0", LOG_LOCAL0 },
      { "LOG_LOCAL1", LOG_LOCAL1 },
      { "LOG_LOCAL2", LOG_LOCAL2 },
      { "LOG_LOCAL3", LOG_LOCAL3 },
      { "LOG_LOCAL4", LOG_LOCAL4 },
      { "LOG_LOCAL5", LOG_LOCAL5 },
      { "LOG_LOCAL6", LOG_LOCAL6 },
      { "LOG_LOCAL7", LOG_LOCAL7 },
      { 0, -1 }
   };
   struct FacilityCode * p;

   for (p = facodes; p->str && strcmp(szFacility, p->str); p++)
      ;

   return *nFacility = p->val;
}

//----------------------------------------------------------------------------

/** Validate a configuration data structure in context.
 * 
 * @param[in] pcfg - the configuration data structure to be validated.
 * @param[out] plogfac - the address of storage for the log facility code
 *    that is validated in the logFacility config parameter.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int validateCfg(DnxCfgData * pcfg, int * plogfac)
{
   if (!pcfg->wlm.dispatcher)
      dnxSyslog(LOG_ERR, "config: Missing channelDispatcher parameter");
   else if (!pcfg->wlm.collector)
      dnxSyslog(LOG_ERR, "config: Missing channelCollector parameter");
   if (pcfg->wlm.poolInitial < 1 || pcfg->wlm.poolInitial > pcfg->wlm.poolMax)
      dnxSyslog(LOG_ERR, "reconfig: Invalid poolInitial parameter");
   else if (pcfg->wlm.poolMin < 1 || pcfg->wlm.poolMin > pcfg->wlm.poolMax)
      dnxSyslog(LOG_ERR, "reconfig: Invalid poolMin parameter");
   else if (pcfg->wlm.poolGrow < 1 || pcfg->wlm.poolGrow >= pcfg->wlm.poolMax)
      dnxSyslog(LOG_ERR, "reconfig: Invalid poolGrow parameter");
   else if (pcfg->wlm.pollInterval < 1)
      dnxSyslog(LOG_ERR, "reconfig: Invalid wlmPollInterval parameter");
   else if (pcfg->wlm.shutdownGrace < 0)
      dnxSyslog(LOG_ERR, "reconfig: Invalid wlmShutdownGracePeriod parameter");
   else if (pcfg->wlm.reqTimeout < 1 
         || pcfg->wlm.reqTimeout <= pcfg->wlm.ttlBackoff)
      dnxSyslog(LOG_ERR, "reconfig: Invalid threadRequestTimeout parameter");
   else if (pcfg->wlm.ttlBackoff >= pcfg->wlm.reqTimeout)
      dnxSyslog(LOG_ERR, "reconfig: Invalid threadTtlBackoff parameter");
   else if (pcfg->wlm.maxResults < 1024)
      dnxSyslog(LOG_ERR, "reconfig: Invalid maxResultBuffer parameter");
   else if (verifyFacility(pcfg->logFacility, plogfac) == -1)
      dnxSyslog(LOG_ERR, "reconfig: Invalid syslog facility: %s", 
            pcfg->logFacility);
   else
      return 0;

   return DNX_ERR_INVALID;
}

//----------------------------------------------------------------------------

/** Read and parse the dnxClient configuration file.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initConfig(void)
{
   int ret;

   if (!s_cfgfile)
      s_cfgfile = DNX_DEFAULT_NODE_CONFIG_FILE;

   // parse config file; pass default values
   if ((ret = dnxParseCfgFile(s_cfgfile, s_cfgdefs, s_dict, s_ppvals)) == 0
         && (ret = validateCfg(&s_cfg, &s_logFacility)) != 0)
      dnxFreeCfgValues(s_dict, s_ppvals);

   return ret;
}

//----------------------------------------------------------------------------

/** Cleanup the config file parser. */
void releaseConfig(void) 
{
   dnxFreeCfgValues(s_dict, s_ppvals);
}

//----------------------------------------------------------------------------

/** Initializes a client communication channels and sub-systems.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int initClientComm(void)
{
   int ret;

   s_agent = 0;

   // initialize the DNX comm stack
   if ((ret = dnxChanMapInit(0)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "comm: dnxChanMapInit failed, %d: %s", 
            ret, dnxErrorString(ret));
      return ret;
   }

   // create a channel for receiving DNX Client Requests 
   //    (e.g., Shutdown, Status, etc.)
   if ((ret = dnxChanMapAdd("Agent", s_cfg.channelAgent)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "comm: dnxChanMapInit(Agent) failed, %d: %s",
            ret, dnxErrorString(ret));
      dnxChanMapRelease();
      return ret;
   }

   // attempt to open the Agent channel
   if ((ret = dnxConnect("Agent", 0, &s_agent)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "comm: dnxConnect(Agent) failed, %d: %s", 
            ret, dnxErrorString(ret));
      dnxChanMapDelete("Agent");
      dnxChanMapRelease();
      return ret;
   }
   return 0;
}

//----------------------------------------------------------------------------

/** Release resources associated with the client communications sub-system. */
static void releaseClientComm(void)
{
   dnxDisconnect(s_agent);
   dnxChanMapDelete("Agent");
   dnxChanMapRelease();
}

//----------------------------------------------------------------------------

/** The global signal handler for the dnxClient process.
 * 
 * @param[in] sig - the signal number received from the system.
 */
static void sighandler(int sig)
{
   switch(sig)
   {
      case SIGHUP:   s_reconfig = 1;   break;
      case SIGUSR1:  s_debugsig = 1;   break;
      default:       s_shutdown = 1;   break;
   }
}

//----------------------------------------------------------------------------

/** Create the process id (pid) lock file.
 * 
 * @param[in] base - the base file name to use for the pid file name.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int createPidFile(char * base)
{
   char lockFile[1024];
   char szPid[32];

   // create lock-file name
   sprintf(lockFile, "/var/run/%s.pid", base);

   // open the lock file
   if ((s_lockfd = open(lockFile, O_RDWR | O_CREAT, 0644)) < 0)
   {
      dnxSyslog(LOG_ERR, "Unable to create lock file, %s: %s", 
            lockFile, strerror(errno));
      return (-1);
   }

   // attempt to lock the lock-file
   if (flock(s_lockfd, LOCK_EX | LOCK_NB) != 0)
   {
      close(s_lockfd);
      dnxSyslog(LOG_NOTICE, "Lock file already in-use: %s: %s", 
            lockFile, strerror(errno));
      return (-1);
   }

   // create a string containing our PID
   sprintf(szPid, "%d\n", getpid());

   // write our PID to the lock file
   if (write(s_lockfd, szPid, strlen(szPid)) != strlen(szPid))
   {
      close(s_lockfd);
      dnxSyslog(LOG_NOTICE, "Failed to write pid to lock file, %s: %s", 
            lockFile, strerror(errno));
      return (-1);
   }
   return 0;
}

//----------------------------------------------------------------------------

/** Remove an existing process id (pid) lock file.
 *
 * @param[in] base - the base file name to use for the pid file name.
 */
static void removePidFile(char * base)
{
   char lockFile[1024];

   // create lock-file name
   sprintf(lockFile, "/var/run/%s.pid", base);

   // remove the lock file - we do this before closing it in order to prevent
   //    race conditions between the closing and removing operations.
   if (unlink(lockFile) != 0)
      dnxSyslog(LOG_WARNING, "Failed to remove lock file, %s: %s", 
            lockFile, strerror(errno));

   // close/unlock the lock file
   if (s_lockfd >= 0) close(s_lockfd);
}

//----------------------------------------------------------------------------

/** Turn this process into a daemon. */
static void daemonize(void)
{
   int pid, fd;

   // fork to allow parent process to exit
   if ((pid = fork()) < 0)
   {
      dnxSyslog(LOG_ERR, "Failed to fork process: %s", strerror(errno));
      exit(1);
   }
   else if (pid != 0)
      exit(0);

   // become process group leader
   setsid();

   // fork again to allow process group leader to exit
   if ((pid = fork()) < 0)
   {
      dnxSyslog(LOG_ERR, "Failed to fork process: %s", strerror(errno));
      exit(1);
   }
   else if (pid != 0)
      exit(0);

   // change working directory to root so as to not keep any file systems open 
   chdir("/");

   // allow us complete control over any newly created files
   umask(0);

   // close and redirect stdin, stdout, stderr
   fd = open("/dev/null", O_RDWR);
   dup2(fd, 0);
   dup2(fd, 1);
   dup2(fd, 2);

   // create pid file
   if (createPidFile(s_progname) != 0)
      exit(1);
}

//----------------------------------------------------------------------------

/** The main event loop for the dnxClient process.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int processCommands(void)
{
   DnxMgmtRequest Msg;
   int ret;

   dnxSyslog(LOG_INFO, "Agent: Awaiting commands...");

   while (1)
   {
      // wait for a request; process the request, if valid
      if ((ret = dnxWaitForMgmtRequest(s_agent, &Msg, Msg.address, 10)) == DNX_OK)
      {
         // perform the requested action
         if (!strcmp(Msg.action, "SHUTDOWN"))
            s_shutdown = 1;
         else if (!strcmp(Msg.action, "RECONFIGURE"))
            s_reconfig = 1;
         else if (!strcmp(Msg.action, "DEBUGTOGGLE"))
            s_debugsig = 1;
         xfree(Msg.action);
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_INFO, "Agent: Channel failure: %s", dnxErrorString(ret));

      if (s_reconfig)
      {
         static DnxCfgData tmp_cfg;
         static void * tmp_ppvals[] =
         {
            &tmp_cfg.channelAgent,
            &tmp_cfg.wlm.dispatcher,
            &tmp_cfg.wlm.collector,
            &tmp_cfg.wlm.poolInitial,
            &tmp_cfg.wlm.poolMin,
            &tmp_cfg.wlm.poolMax,
            &tmp_cfg.wlm.poolGrow,
            &tmp_cfg.wlm.pollInterval,
            &tmp_cfg.wlm.shutdownGrace,
            &tmp_cfg.wlm.reqTimeout,
            &tmp_cfg.wlm.maxRetries,
            &tmp_cfg.wlm.ttlBackoff,
            &tmp_cfg.wlm.maxResults,
            &tmp_cfg.logFacility,
            &tmp_cfg.pluginPath,
            &tmp_cfg.debugLevel,
         };
         int logfac;

         dnxSyslog(LOG_ERR, "Agent: Received RECONFIGURE request. Reconfiguring...");

         // reparse config file into a temporary cfg structure and validate
         if ((ret = dnxParseCfgFile(s_cfgfile, s_cfgdefs, s_dict, tmp_ppvals)) == 0
               && (ret = validateCfg(&s_cfg, &logfac)) != 0)
            dnxFreeCfgValues(s_dict, tmp_ppvals);
         else if ((ret = dnxWlmReconfigure(s_wlm, &tmp_cfg.wlm)) == 0)
         {
            // reconfigure completed successfully - 
            //    free old values and reassign to new values.
            dnxFreeCfgValues(s_dict, s_ppvals);
            s_cfg = tmp_cfg;
            s_logFacility = logfac;
         }
         dnxSyslog(LOG_ERR, "Reconfiguration: %s", dnxErrorString(ret));
         s_reconfig = 0;
      }
      if (s_debugsig)
      {
         s_dbgflag ^= 1;
         dnxSyslog(LOG_ERR, 
               "Agent: Received DEBUGTOGGLE request. "
               "Debugging is %s", s_dbgflag? "ENABLED" : "DISABLED");
         s_debugsig = 0;
      }
      if (s_shutdown)
      {
         dnxSyslog(LOG_INFO, "Agent: Received SHUTDOWN request. Terminating...");
         break;
      }
   }
   if (ret == DNX_ERR_TIMEOUT)   // timeout is ok
      ret = 0;
   return ret;
}

//----------------------------------------------------------------------------

/** The main program entry point for the dnxClient service.
 * 
 * @param[in] argc - the number of elements in the @p argv array.
 * @param[in] argv - a null-terminated array of command-line arguments.
 * 
 * @return Zero on success, or a non-zero error code that is returned to the
 * shell. Any non-zero codes should be values between 1 and 127.
 */
int main(int argc, char ** argv)
{
   char * cp;
   int ret;

   // set program base name
   s_progname = (char *)((cp = strrchr(argv[0], '/')) != 0 ? (cp + 1) : argv[0]);

   // parse command line options
   getOptions(argc, argv);

   // initialize the logging subsystem with configured settings
   openlog(s_progname, LOG_PID, LOG_LOCAL7);
   dnxSyslog(LOG_INFO, "***** DNX Client (Version %s) Startup *****", VERSION);

   // parse configuration file into global configuration data structure
   if ((ret = initConfig()) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "Config file processing failed: %s", dnxErrorString(ret));
      goto e0;
   }

   // set configured debug level and syslog log facility code
   initLogging(&s_cfg.debugLevel, &s_logFacility);

   // load dynamic plugin modules (e.g., nrpe, snmp, etc.)
   if ((ret = dnxPluginInit(s_cfg.pluginPath)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "Plugin init failed: %s", dnxErrorString(ret));
      goto e1;
   }

   // install signal handlers
   signal(SIGHUP,  sighandler);
   signal(SIGINT,  sighandler);
   signal(SIGQUIT, sighandler);
   signal(SIGABRT, sighandler);
   signal(SIGTERM, sighandler);
   signal(SIGPIPE, SIG_IGN);
   signal(SIGALRM, SIG_IGN);
   signal(SIGUSR1, sighandler);
   signal(SIGUSR2, SIG_IGN);

   // daemonize
   if (!s_dbgflag) daemonize();

   // initialize the communications stack
   if ((ret = initClientComm()) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "Communications init failed: %s", dnxErrorString(ret));
      goto e3;
   }

   if ((ret = dnxWlmCreate(&s_cfg.wlm, &s_wlm)) != 0)
   {
      dnxSyslog(LOG_ERR, "Thread pool init failed: %s", dnxErrorString(ret));
      goto e4;
   }

   //----------------------------------------------------------------------
   ret = processCommands();
   //----------------------------------------------------------------------

   dnxDebug(1, "main: Command-loop exit code, %d: %s", ret, dnxErrorString(ret));

   dnxWlmDestroy(s_wlm);
e4:releaseClientComm();
e3:if (!s_dbgflag) removePidFile(s_progname);
e2:dnxPluginRelease();
e1:releaseConfig();
e0:dnxSyslog(LOG_INFO, "*** Shutdown complete ***");
   closelog();

   xheapchk();    // works when debug heap is compiled in

   return ret;
}

/*--------------------------------------------------------------------------*/

