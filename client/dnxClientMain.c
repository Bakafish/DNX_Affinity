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
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION           "<unknown>"
# define PACKAGE_BUGREPORT "<unknown>"
#endif

#ifndef SYSCONFDIR
# define SYSCONFDIR "/etc"
#endif

#ifndef SYSRUNPATH
# define SYSRUNPATH "/var/run"
#endif

#define DNX_DEFAULT_RUN_PATH SYSRUNPATH

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_DEFAULT_NODE_CONFIG_FILE   SYSCONFDIR "/dnxClient.cfg"

typedef struct DnxCfgData
{
   char * channelAgent;          //!< The agent management channel URL.
   char * logFilePath;           //!< The normal logging file path.
   char * debugFilePath;         //!< The debug logging file path.
   char * pluginPath;            //!< The file system plugin path.
   unsigned debugLevel;          //!< The system global debug level.
   DnxWlmCfgData wlm;            //!< WLM specific configuration data.
} DnxCfgData;

// module statics
static DnxCfgData s_cfg;         //!< The system configuration parameters.
static DnxCfgParser * s_parser;  //!< The system configuration parser.
static DnxWlm * s_wlm;           //!< The system worker thread pool.
static DnxChannel * s_agent;     //!< The agent management channel.
static char * s_progname;        //!< The base program name.
static char * s_cfgfile;         //!< The system configuration file name.
static char * s_runpath = 0;     //!< The system pid/lock file path.
static char * s_cmdover = 0;     //!< The command line overrides string.
static int s_dbgflag = 0;        //!< The system debug flag.
static int s_shutdown = 0;       //!< The shutdown signal flag.
static int s_reconfig = 0;       //!< The reconfigure signal flag.
static int s_debugsig = 0;       //!< The debug toggle signal flag.
static int s_lockfd = -1;        //!< The system PID file descriptor.

/** The array of cfg variable addresses for the configuration parser. 
 *
 * The order of this array must be kept in sync with the order of the
 * dictionary with which it will be used.
 * 
 * @todo: Make this array position independent.
 */
static void * s_ppvals[] =
{
   &s_cfg.channelAgent,
   &s_cfg.logFilePath,
   &s_cfg.debugFilePath,
   &s_cfg.pluginPath,
   &s_cfg.debugLevel,
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
};

//----------------------------------------------------------------------------

/** Display program usage text to STDERR and exit with an error. 
 * 
 * @param[in] base - the base file name of this program.
 */
static void usage(char * base)
{
   fprintf(stderr, "\nUsage: %s [options]", base);
   fprintf(stderr, "\nWhere [options] are:\n");
   fprintf(stderr, "   -c, --cfgfile <file>    specify the file and path of the config file.\n");
   fprintf(stderr, "   -l, --logfile <file>    specify the file and path of the log file.\n");
   fprintf(stderr, "   -D, --dbgfile <file>    specify the file and path of the debug log file.\n");
   fprintf(stderr, "   -g, --dbglevel <value>  specify the level of debugging output.\n");
   fprintf(stderr, "   -d, --debug             enable debug mode (will not become a daemon).\n");
   fprintf(stderr, "   -r, --runpath <path>    specify the path of the lock/pid file.\n");
   fprintf(stderr, "   -v, --version           display DNX client version and exit.\n");
   fprintf(stderr, "   -h, --help              display this help screen and exit.\n\n");
}

//----------------------------------------------------------------------------

/** Display program version information to STDOUT and exit successfully. 
 * 
 * @param[in] base - the base file name of this program.
 */
static void version(char * base)
{
   printf("\n  %s version %s\n  Bug reports: %s.\n\n", 
         base, VERSION, PACKAGE_BUGREPORT);
}

//----------------------------------------------------------------------------

/** Append text to a string by reallocating the string buffer.
 * 
 * This is a var-args function. Additional parameters following the @p fmt
 * parameter are based on the content of the @p fmt string.
 * 
 * @param[in] spp - the address of a dynamically allocated buffer pointer.
 * @param[in] fmt - a printf-like format specifier string.
 */
static void appendString(char ** spp, char * fmt, ... )
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
      return;
   if (*spp == 0) 
      *newstr = 0;

   // concatenate new string onto exiting string; return updated pointer
   strcat(newstr, buf);
   *spp = newstr;
}

//----------------------------------------------------------------------------

/** Parse command line options.
 * 
 * @param[in] argc - the number of elements in the @p argv array.
 * @param[in] argv - a null-terminated array of command-line arguments.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int getOptions(int argc, char ** argv)
{
// extern int optind;
   extern char * optarg;
   extern int opterr, optopt;

   static char opts[] = "c:dr:g:l:D:vh";
   static struct option longopts[] = 
   {
      { "cfgfile",  required_argument, 0, 'c' },
      { "logfile",  required_argument, 0, 'l' },
      { "dbgfile",  required_argument, 0, 'D' },
      { "dbglevel", required_argument, 0, 'g' },
      { "debug",    no_argument,       0, 'd' },
      { "runpath",  required_argument, 0, 'r' },
      { "version",  no_argument,       0, 'v' },
      { "help",     no_argument,       0, 'h' },
      { 0, 0, 0, 0 },
   };

   int ch;
   char * cp;
   char * logfile = 0;
   char * dbgfile = 0;
   char * dbglvl = 0;

   // set program base name
   s_progname = (char *)((cp = strrchr(argv[0], '/')) != 0 ? (cp + 1) : argv[0]);

   opterr = 0; /* Disable error messages */

   while ((ch = getopt_long(argc, argv, opts, longopts, 0)) != -1)
   {
      switch (ch)
      {
         case 'c': s_cfgfile = optarg; break;
         case 'd': s_dbgflag = 1;      break;
         case 'r': s_runpath = optarg; break;
         case 'g': dbglvl    = optarg; break;
         case 'l': logfile   = optarg; break;
         case 'D': dbgfile   = optarg; break;
         case 'v': version(s_progname); exit(0);
         case 'h':
         default : return usage(s_progname), -1;
      }
   }

   if (!s_cfgfile)
      s_cfgfile = DNX_DEFAULT_NODE_CONFIG_FILE;

   if (!s_runpath)
      s_runpath = DNX_DEFAULT_RUN_PATH;

   if (s_dbgflag)
      appendString(&s_cmdover, "logFile=STDOUT\ndebugFile=STDOUT\n");

   if (logfile)
      appendString(&s_cmdover, "logFile=%s\n", logfile);

   if (dbgfile)
      appendString(&s_cmdover, "debugFile=%s\n", dbgfile);

   if (dbglvl)
      appendString(&s_cmdover, "debugLevel=%s\n", dbglvl);

   return 0;
}

//----------------------------------------------------------------------------

/** Validate a configuration data structure in context.
 * 
 * @param[in] pcfg - the configuration data structure to be validated.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int validateCfg(DnxCfgData * pcfg)
{
   if (!pcfg->wlm.dispatcher)
      dnxLog("config: Missing channelDispatcher parameter.");
   else if (!pcfg->wlm.collector)
      dnxLog("config: Missing channelCollector parameter.");
   if (pcfg->wlm.poolInitial < 1 || pcfg->wlm.poolInitial > pcfg->wlm.poolMax)
      dnxLog("config: Invalid poolInitial parameter.");
   else if (pcfg->wlm.poolMin < 1 || pcfg->wlm.poolMin > pcfg->wlm.poolMax)
      dnxLog("config: Invalid poolMin parameter.");
   else if (pcfg->wlm.poolGrow < 1 || pcfg->wlm.poolGrow >= pcfg->wlm.poolMax)
      dnxLog("config: Invalid poolGrow parameter.");
   else if (pcfg->wlm.pollInterval < 1)
      dnxLog("config: Invalid wlmPollInterval parameter.");
   else if (pcfg->wlm.shutdownGrace < 0)
      dnxLog("config: Invalid wlmShutdownGracePeriod parameter.");
   else if (pcfg->wlm.reqTimeout < 1 
         || pcfg->wlm.reqTimeout <= pcfg->wlm.ttlBackoff)
      dnxLog("config: Invalid threadRequestTimeout parameter.");
   else if (pcfg->wlm.ttlBackoff >= pcfg->wlm.reqTimeout)
      dnxLog("config: Invalid threadTtlBackoff parameter.");
   else if (pcfg->wlm.maxResults < 1024)
      dnxLog("config: Invalid maxResultBuffer parameter.");
   else
      return 0;

   return DNX_ERR_INVALID;
}

//----------------------------------------------------------------------------

/** Cleanup the config file parser. */
static void releaseConfig(void) 
{
   dnxCfgParserFreeCfgValues(s_parser, s_ppvals);
   dnxCfgParserDestroy(s_parser);
   xfree(s_cmdover);
}

//----------------------------------------------------------------------------

/** Read and parse the dnxClient configuration file.
 * 
 * @param[in] cfgfile - the configuration file to use.
 * @return Zero on success, or a non-zero error value.
 */
static int initConfig(char * cfgfile)
{
   static DnxCfgDict dict[] = 
   {
      { "channelAgent",           DNX_CFG_URL      },
      { "logFile",                DNX_CFG_FSPATH   },
      { "debugFile",              DNX_CFG_FSPATH   },
      { "pluginPath",             DNX_CFG_FSPATH   },
      { "debugLevel",             DNX_CFG_UNSIGNED },
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
      { 0 },
   };
   static char cfgdefs[] = 
      "channelAgent = udp://0:12480\n"
      "poolInitial = 20\n"
      "poolMin = 20\n"
      "poolMax = 300\n"
      "poolGrow = 10\n"
      "wlmPollInterval = 2\n"
      "wlmShutdownGracePeriod = 35\n"
      "threadRequestTimeout = 5\n"
      "threadMaxRetries = 12\n"
      "threadTtlBackoff = 1\n"
      "maxResultBuffer = 1024\n"
      "logFile = /var/log/dnxcld.log\n"
      "debugFile = /var/log/dnxcld.debug.log";

   int ret;

   // create global configuration parser object
   if ((ret = dnxCfgParserCreate(cfgdefs, s_cfgfile, s_cmdover, dict, 
         &s_parser)) != 0)
      return ret;

   // parse config file
   if ((ret = dnxCfgParserParse(s_parser, s_ppvals)) != 0
         || (ret = validateCfg(&s_cfg)) != 0)
      releaseConfig();

   return ret;
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
      dnxLog("Failed to initialize channel map: %s.", dnxErrorString(ret));
      return ret;
   }

   // create a channel for receiving DNX Client Requests 
   //    (e.g., Shutdown, Status, etc.)
   if ((ret = dnxChanMapAdd("Agent", s_cfg.channelAgent)) != DNX_OK)
   {
      dnxLog("Failed to initialize AGENT channel: %s.", dnxErrorString(ret));
      dnxChanMapRelease();
      return ret;
   }

   // attempt to open the Agent channel
   if ((ret = dnxConnect("Agent", 0, &s_agent)) != DNX_OK)
   {
      dnxLog("Failed to open AGENT channel: %s.", dnxErrorString(ret));
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
   sprintf(lockFile, "%s/%s.pid", s_runpath, base);

   // open the lock file
   if ((s_lockfd = open(lockFile, O_RDWR | O_CREAT, 0644)) < 0)
   {
      dnxLog("Unable to create lock file, %s: %s.", lockFile, strerror(errno));
      return (-1);
   }

   // attempt to lock the lock-file
   if (flock(s_lockfd, LOCK_EX | LOCK_NB) != 0)
   {
      close(s_lockfd);
      dnxLog("Lock file already in-use: %s: %s.", lockFile, strerror(errno));
      return (-1);
   }

   // create a string containing our PID
   sprintf(szPid, "%d\n", getpid());

   // write our PID to the lock file
   if (write(s_lockfd, szPid, strlen(szPid)) != strlen(szPid))
   {
      close(s_lockfd);
      dnxLog("Failed to write pid to lock file, %s: %s.", 
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
   sprintf(lockFile, "%s/%s.pid", s_runpath, base);

   // remove the lock file - we do this before closing it in order to prevent
   //    race conditions between the closing and removing operations.
   unlink(lockFile);

   // close/unlock the lock file
   if (s_lockfd >= 0) close(s_lockfd);
}

//----------------------------------------------------------------------------

/** Turn this process into a daemon. 
 * 
 * @param[in] base - the base file name of this program.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int daemonize(char * base)
{
   int pid, fd;

   // fork to allow parent process to exit
   if ((pid = fork()) < 0)
   {
      dnxLog("Failed 1st fork: %s.", strerror(errno));
      return -1;
   }
   else if (pid != 0)
      exit(0);

   // become process group leader
   setsid();

   // fork again to allow process group leader to exit
   if ((pid = fork()) < 0)
   {
      dnxLog("Failed 2nd fork: %s.", strerror(errno));
      return -1;
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
   if (createPidFile(base) != 0)
      return -1;

   return 0;   // continue execution as a daemon
}

/** Log changes between old and new global configuration data sets.
 * 
 * Dynamic reconfiguration of dispatcher and collector URL's is not allowed
 * so we don't need to check differences in those string values.
 * 
 * @param[in] ocp - a reference to the old configuration data set.
 * @param[in] ncp - a reference to the new configuration data set.
 */
static void logGblConfigChanges(DnxCfgData * ocp, DnxCfgData * ncp)
{
   if (strcmp(ocp->channelAgent, ncp->channelAgent) != 0)
      dnxLog("Config parameter 'channelAgent' changed from %s to %s. "
            "NOTE: Changing the agent URL requires a restart.", 
            ocp->channelAgent, ncp->channelAgent);

   if (strcmp(ocp->logFilePath, ncp->logFilePath) != 0)
      dnxLog("Config parameter 'logFile' changed from %s to %s. "
            "NOTE: Changing the log file path requires a restart.", 
            ocp->logFilePath, ncp->logFilePath);

   if (strcmp(ocp->debugFilePath, ncp->debugFilePath) != 0)
      dnxLog("Config parameter 'debugFile' changed from %s to %s. "
            "NOTE: Changing the debug log file path requires a restart.", 
            ocp->debugFilePath, ncp->debugFilePath);

   if (strcmp(ocp->pluginPath, ncp->pluginPath) != 0)
      dnxLog("Config parameter 'pluginPath' changed from %s to %s.",
            ocp->pluginPath, ncp->pluginPath);

   if (ocp->debugLevel != ncp->debugLevel)
      dnxLog("Config parameter 'debugLevel' changed from %u to %u.", 
            ocp->debugLevel, ncp->debugLevel);
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

   dnxLog("DNX Client Agent awaiting commands...");

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
         dnxLog("Agent channel failure: %s.", dnxErrorString(ret));

      if (s_reconfig)
      {
         static DnxCfgData tmp_cfg;
         static void * tmp_ppvals[] =
         {
            &tmp_cfg.channelAgent,
            &tmp_cfg.logFilePath,
            &tmp_cfg.debugFilePath,
            &tmp_cfg.pluginPath,
            &tmp_cfg.debugLevel,
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
         };

         dnxLog("Agent received RECONFIGURE request. Reconfiguring...");

         // reparse config file into temporary cfg structure and validate
         if ((ret = dnxCfgParserParse(s_parser, tmp_ppvals)) == 0
               && (ret = validateCfg(&tmp_cfg)) != 0)
            dnxCfgParserFreeCfgValues(s_parser, tmp_ppvals);
         else if ((ret = dnxWlmReconfigure(s_wlm, &tmp_cfg.wlm)) == 0)
         {
            // reconfigure completed successfully - log diffs; 
            //    free old values and reassign to new values.

            logGblConfigChanges(&s_cfg, &tmp_cfg);

            dnxCfgParserFreeCfgValues(s_parser, s_ppvals);
            s_cfg = tmp_cfg;
         }
         dnxLog("Reconfiguration: %s.", dnxErrorString(ret));
         s_reconfig = 0;
      }
      if (s_debugsig)
      {
         s_dbgflag ^= 1;
         dnxLog("Agent: Received DEBUGTOGGLE request. Debugging is %s.", 
               s_dbgflag? "ENABLED" : "DISABLED");
         s_debugsig = 0;
      }
      if (s_shutdown)
      {
         dnxLog("Agent: Received SHUTDOWN request. Terminating...");
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
   int ret;

   // parse command line options; read configuration file
   if ((ret = getOptions(argc, argv)) != DNX_OK
         || (ret = initConfig(s_cfgfile)) != DNX_OK)
      goto e0;

   // initialize the logging subsystem with configured settings
   if ((ret = dnxLogInit(s_cfg.logFilePath, s_cfg.debugFilePath, 0,
         &s_cfg.debugLevel)) != 0)
   {
      dnxLog("Failed to initialize logging: %s.", dnxErrorString(ret));
      goto e1;
   }

   dnxLog("-------- DNX Client Daemon Version %s Startup --------", VERSION);
   dnxLog("Copyright (c) 2006-2008 Intellectual Reserve. All rights reserved.");
   dnxLog("Configuration file: %s.", s_cfgfile);
   dnxLog("Agent: %s.", s_cfg.channelAgent);
   dnxLog("Dispatcher: %s.", s_cfg.wlm.dispatcher);
   dnxLog("Collector: %s.", s_cfg.wlm.collector);
   if (s_cfg.debugFilePath)
      dnxLog("Debug logging enabled at level %d to %s.", 
            s_cfg.debugLevel, s_cfg.debugFilePath);

   // load dynamic plugin modules (e.g., nrpe, snmp, etc.)
   if ((ret = dnxPluginInit(s_cfg.pluginPath)) != DNX_OK)
   {
      dnxLog("Plugin init failed: %s.", dnxErrorString(ret));
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

   // daemonize if not running in debug mode
   if (!s_dbgflag && (ret = daemonize(s_progname)) != 0)
      goto e2;

   // initialize the communications stack
   if ((ret = initClientComm()) != DNX_OK)
   {
      dnxLog("Communications init failed: %s.", dnxErrorString(ret));
      goto e3;
   }

   if ((ret = dnxWlmCreate(&s_cfg.wlm, &s_wlm)) != 0)
   {
      dnxLog("Thread pool init failed: %s.", dnxErrorString(ret));
      goto e4;
   }

   //----------------------------------------------------------------------
   ret = processCommands();
   //----------------------------------------------------------------------

   dnxDebug(1, "Command-loop exited: %s.", dnxErrorString(ret));

   dnxWlmDestroy(s_wlm);
e4:releaseClientComm();
e3:removePidFile(s_progname);
e2:dnxPluginRelease();
e1:releaseConfig();
e0:
   xheapchk();    // works when debug heap is compiled in

   dnxLog("-------- DNX Client Daemon Shutdown Complete --------");
   dnxLogExit();
   return ret;
}

/*--------------------------------------------------------------------------*/

