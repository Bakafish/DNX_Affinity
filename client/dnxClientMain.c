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
# include <config.h>
#else
# define VERSION "<unknown>"
#endif

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_NODE_CONFIG "dnxNode.cfg"

typedef struct DnxCfgData
{
   char * channelAgent;          /*!< The agent management channel URL. */
   char * channelDispatcher;     /*!< The dispatcher channel URL. */
   char * channelCollector;      /*!< The collector channel URL. */
   unsigned poolInitial;         /*!< The initial worker pool size. */
   unsigned poolMin;             /*!< The minimum number of workers. */
   unsigned poolMax;             /*!< The maximum number of workers. */
   unsigned poolGrow;            /*!< The number of workers by which to grow. */
   unsigned wlmPollInterval;     /*!< The WLM poll interval. */
   unsigned wlmShutdownGrace;    /*!< The WLM shutdown grace period. */
   unsigned threadReqTimeout;    /*!< The request timeout in seconds. */
   unsigned threadMaxTimeouts;   /*!< The timeout maximum in seconds. */
   unsigned threadTtlBackoff;    /*!< The time-to-live backoff in seconds. */
   char * logFacility;           /*!< The syslog facility, as a string. */
   char * pluginPath;            /*!< The file system plugin path. */
   unsigned maxResultBuffer;     /*!< The maximum size of the results buffer. */
   unsigned debug;               /*!< The system global debug level. */
} DnxCfgData;

static DnxCfgParser * cfgParser; /*!< The DNX client configuration parser. */
static DnxCfgData dnxCfgData;    /*!< The global configuration data. */
static DnxWLM * wlm;             /*!< The global work load manager. */
static DnxChannel * agent;       /*!< The agent management channel. */

static char * progname;          /*!< The base program name. */
static char * ConfigFile = DNX_NODE_CONFIG;
                                 /*!< The global configuration file name. */
static int Debug = 0;            /*!< The global debug flag. */
static int gotSig = 0;           /*!< Indicates true if we were signaled. */
static int lockFd = -1;          /*!< The PID file descriptor. */
static int dnxLogFacility;       /*!< The syslog facility code as in int. */

//----------------------------------------------------------------------------

/** Display program usage text to STDERR and exit with an error.
 */
static void usage(void)
{
   fprintf(stderr, "\nUsage: %s [-c config-file] [-d] [-v]\n", progname);
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
   printf("%s %s\n", progname, VERSION);
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
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int getOptions(int argc, char ** argv)
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
         ConfigFile = optarg;
         break;
      case 'd':
         Debug = 1;
         break;
      case 'v':
         version();
         break;
      default:
         usage();
      }
   }
   return DNX_OK;
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

/** Read and parse the dnxClient configuration file.
 * 
 * @param[in] cfg - the configuration data structure. 
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initConfig(void)
{
   static DnxCfgDictionary dict[] = 
   {
      {"channelAgent",           DNX_CFG_URL,      &dnxCfgData.channelAgent      },
      {"channelDispatcher",      DNX_CFG_URL,      &dnxCfgData.channelDispatcher },
      {"channelCollector",       DNX_CFG_URL,      &dnxCfgData.channelCollector  },
      {"poolInitial",            DNX_CFG_UNSIGNED, &dnxCfgData.poolInitial       },
      {"poolMin",                DNX_CFG_UNSIGNED, &dnxCfgData.poolMin           },
      {"poolMax",                DNX_CFG_UNSIGNED, &dnxCfgData.poolMax           },
      {"poolGrow",               DNX_CFG_UNSIGNED, &dnxCfgData.poolGrow          },
      {"wlmPollInterval",        DNX_CFG_UNSIGNED, &dnxCfgData.wlmPollInterval   },
      {"wlmShutdownGracePeriod", DNX_CFG_UNSIGNED, &dnxCfgData.wlmShutdownGrace  },
      {"threadRequestTimeout",   DNX_CFG_UNSIGNED, &dnxCfgData.threadReqTimeout  },
      {"threadMaxTimeouts",      DNX_CFG_UNSIGNED, &dnxCfgData.threadMaxTimeouts },
      {"threadTtlBackoff",       DNX_CFG_UNSIGNED, &dnxCfgData.threadTtlBackoff  },
      {"logFacility",            DNX_CFG_STRING,   &dnxCfgData.logFacility       },
      {"pluginPath",             DNX_CFG_FSPATH,   &dnxCfgData.pluginPath        },
      {"maxResultBuffer",        DNX_CFG_UNSIGNED, &dnxCfgData.maxResultBuffer   },
      {"debug",                  DNX_CFG_UNSIGNED, &dnxCfgData.debug             },
   };
   int ret;

   // set the default logging facility
   dnxLogFacility = LOG_LOCAL7;

   if ((ret = dnxCfgParserCreate(ConfigFile, 
         dict, elemcount(dict), 0, 0, &cfgParser)) != 0)
      return ret;

   // validate configuration items
   ret = DNX_ERR_INVALID;
   if (!dnxCfgData.channelAgent)
      syslog(LOG_ERR, "getConfig: Missing channelAgent parameter");
   else if (!dnxCfgData.channelDispatcher)
      syslog(LOG_ERR, "getConfig: Missing channelDispatcher parameter");
   else if (!dnxCfgData.channelCollector)
      syslog(LOG_ERR, "getConfig: Missing channelCollector parameter");
   else if (dnxCfgData.poolInitial < 1 || dnxCfgData.poolInitial > dnxCfgData.poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolInitial parameter");
   else if (dnxCfgData.poolMin < 1 || dnxCfgData.poolMin > dnxCfgData.poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolMin parameter");
   else if (dnxCfgData.poolGrow < 1 || dnxCfgData.poolGrow >= dnxCfgData.poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolGrow parameter");
   else if (dnxCfgData.wlmPollInterval < 1)
      syslog(LOG_ERR, "getConfig: Missing or invalid wlmPollInterval parameter");
   else if (dnxCfgData.wlmShutdownGrace < 0)
      syslog(LOG_ERR, "getConfig: Missing or invalid wlmShutdownGracePeriod parameter");
   else if (dnxCfgData.threadReqTimeout < 1 
         || dnxCfgData.threadReqTimeout <= dnxCfgData.threadTtlBackoff)
      syslog(LOG_ERR, "getConfig: Missing or invalid threadRequestTimeout parameter");
   else if (dnxCfgData.threadTtlBackoff < 1 
         || dnxCfgData.threadTtlBackoff >= dnxCfgData.threadReqTimeout)
      syslog(LOG_ERR, "getConfig: Missing or invalid threadTtlBackoff parameter");
   else if (dnxCfgData.maxResultBuffer < 1024)
      syslog(LOG_ERR, "getConfig: Missing or invalid maxResultBuffer parameter");
   else if (dnxCfgData.logFacility &&   /* If logFacility is defined, then */
         verifyFacility(dnxCfgData.logFacility, &dnxLogFacility) == -1)
      dnxSyslog(LOG_ERR, 
            "getConfig: Invalid syslog facility for logFacility: %s", 
            dnxCfgData.logFacility);
   else
      ret = DNX_OK;

   if (ret != 0)
      dnxCfgParserDestroy(cfgParser);

   return ret;
}

//----------------------------------------------------------------------------

/** Cleanup the config file parser. */
void releaseConfig(void) { dnxCfgParserDestroy(cfgParser); }

//----------------------------------------------------------------------------

/** Initializes a client communication channels and sub-systems.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int initClientComm(void)
{
   int ret;

   agent = 0;

   // initialize the DNX comm stack
   if ((ret = dnxChanMapInit(0)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxChanMapInit failed, %d: %s", 
            ret, dnxErrorString(ret));
      return ret;
   }

   // create a channel for receiving DNX Client Requests 
   //    (e.g., Shutdown, Status, etc.)
   if ((ret = dnxChanMapAdd("Agent", dnxCfgData.channelAgent)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxChanMapInit(Agent) failed, %d: %s",
            ret, dnxErrorString(ret));
      dnxChanMapRelease();
      return ret;
   }

   // attempt to open the Agent channel
   if ((ret = dnxConnect("Agent", &agent, DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxConnect(Agent) failed, %d: %s", 
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
   dnxDisconnect(agent);
   dnxChanMapDelete("Agent");
   dnxChanMapRelease();
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

   syslog(LOG_INFO, "processCommands: DNX Client agent awaiting commands");

   // wait on the Agent socket for a request
   while (!gotSig && (ret = dnxGetMgmtRequest(agent, 
         &Msg, Msg.address, 10)) != DNX_ERR_RECEIVE && ret != DNX_ERR_OPEN)
   {
      // process the request, if valid
      if (ret == DNX_OK)
      {
         // perform the requested action
         if (!strcmp(Msg.action, "SHUTDOWN"))
         {
            syslog(LOG_INFO, "processCommands: DNX Client agent "
                             "received SHUTDOWN command");
            break;
         }
         if (Msg.action) 
            xfree(Msg.action);
      }
   }
   return ret;
}

//----------------------------------------------------------------------------

/** The global signal handler for the dnxClient process.
 * 
 * @param[in] sig - the signal number received from the system.
 */
static void sighandler(int sig)
{
   gotSig = 1; // set global 'signaled' variable
   syslog(LOG_WARNING, "%s: Exiting on signal %d", progname, sig);
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
   if ((lockFd = open(lockFile, O_RDWR | O_CREAT, 0644)) < 0)
   {
      syslog(LOG_ERR, "%s: Unable to create lock file: %s: %s", 
            base, lockFile, strerror(errno));
      return (-1);
   }

   // attempt to lock the lock-file
   if (flock(lockFd, LOCK_EX | LOCK_NB) != 0)
   {
      close(lockFd);
      syslog(LOG_NOTICE, "%s: Lock file already in-use: %s: %s", 
            base, lockFile, strerror(errno));
      return (-1);
   }

   // create a string containing our PID
   sprintf(szPid, "%d\n", getpid());

   // write our PID to the lock file
   if (write(lockFd, szPid, strlen(szPid)) != strlen(szPid))
   {
      close(lockFd);
      syslog(LOG_NOTICE, "%s: Failed to write pid to lock file: %s: %s", 
            base, lockFile, strerror(errno));
      return (-1);
   }
   return 0;
}

//----------------------------------------------------------------------------

/** Turn this process into a daemon. */
static void daemonize(void)
{
   int pid, fd;

   // fork to allow parent process to exit
   if ((pid = fork()) < 0)
   {
      syslog(LOG_ERR, "%s: Failed to fork process: %s", progname, strerror(errno));
      exit(1);
   }
   else if (pid != 0)
      exit(0);

   // become process group leader
   setsid();

   // fork again to allow process group leader to exit
   if ((pid = fork()) < 0)
   {
      syslog(LOG_ERR, "%s: Failed to fork process: %s", progname, strerror(errno));
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
   if (createPidFile(progname) != 0)
      exit(1);
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
      syslog(LOG_WARNING, "%s: Failed to remove lock file: %s: %s", 
            base, lockFile, strerror(errno));

   // close/unlock the lock file
   if (lockFd >= 0) close(lockFd);
}

//----------------------------------------------------------------------------

/** The process debug signal handler. 
 * 
 * @param[in] sig - the signal number received from the operating system.
 */
static void sig_debug(int sig)
{
   Debug ^= 1;
   syslog(LOG_NOTICE, "%s: Received signal %d: Debug mode toggled to %s", 
         progname, sig, (char *)(Debug ? "ON" : "OFF"));
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
   progname = (char *)((cp = strrchr(argv[0], '/')) != 0 ? (cp + 1) : argv[0]);

   openlog(progname, LOG_PID, LOG_LOCAL7);
   syslog(LOG_INFO, "***** DNX Client Version %s Startup *****", VERSION);

   // get command line options
   if ((ret = getOptions(argc, argv)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Command-line option processing failure, %d: %s", 
            ret, dnxErrorString(ret));
      goto e0;
   }

   // parse configuration file into global configuration data structure
   if ((ret = initConfig()) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Configuration file processing failure, %d: %s", 
            ret, dnxErrorString(ret));
      goto e0;
   }

   // initialize the logging subsystem with configured settings
   initLogging(&dnxCfgData.debug, &dnxLogFacility);

   // load dynamic plugin modules (e.g., nrpe, snmp, etc.)
   if ((ret = dnxPluginInit(dnxCfgData.pluginPath)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Plugin initialization failure, %d: %s",
            ret, dnxErrorString(ret));
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
   signal(SIGUSR1, sig_debug);
   signal(SIGUSR2, SIG_IGN);

   // daemonize
   if (!Debug) daemonize();

   // initialize the communications stack
   if ((ret = initClientComm()) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Failed to initialize communications, %d: %s", 
            ret, dnxErrorString(ret));
      goto e3;
   }

   if ((ret = dnxWLMCreate(dnxCfgData.poolMin, dnxCfgData.poolInitial, 
         dnxCfgData.poolMax, dnxCfgData.poolGrow, dnxCfgData.wlmPollInterval, 
         dnxCfgData.wlmShutdownGrace, &dnxCfgData.debug, &wlm)) != 0)
   {
      syslog(LOG_ERR, "dnxMain: Failed to create work load manager, %d: %s", 
            ret, dnxErrorString(ret));
      goto e4;
   }

   //----------------------------------------------------------------------
   ret = processCommands();
   //----------------------------------------------------------------------

   if (dnxCfgData.debug)
      syslog(LOG_DEBUG, "dnxMain: Command-loop exit code, %d: %s", 
            ret, dnxErrorString(ret));

   dnxWLMDestroy(wlm);
e4:releaseClientComm();
e3:if (!Debug) removePidFile(progname);
e2:dnxPluginRelease();
e1:releaseConfig();

   syslog(LOG_INFO, "dnxMain: Shutdown complete.");

e0:closelog();

   xheapchk();    // works when debug heap is compiled in

   return ret;
}

/*--------------------------------------------------------------------------*/

