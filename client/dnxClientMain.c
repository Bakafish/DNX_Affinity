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
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#if HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION           "<unknown>"
# define PACKAGE_BUGREPORT "<unknown>"
#endif

#if HAVE_GETOPT_LONG
#define _GNU_SOURCE
#include <getopt.h>
#endif

#ifndef SYSCONFDIR
# define SYSCONFDIR     "/etc"
#endif

#ifndef SYSLOGDIR
# define SYSLOGDIR      "/var/log"
#endif

#ifndef SYSRUNPATH
# define SYSRUNPATH     "/var/run/dnx"
#endif

#ifndef DNXUSER
# define DNXUSER        "nagios"
#endif

#ifndef DNXGROUP
# define DNXGROUP       "nagios"
#endif

#ifndef COMPILE_FLAGS
# define COMPILE_FLAGS  "<unknown>"
#endif

// default configuration
#define DNX_DEFAULT_NODE_CONFIG_FILE      SYSCONFDIR "/dnxClient.cfg"
#define DNX_DEFAULT_LOG                   SYSLOGDIR "/dnxcld.log"
#define DNX_DEFAULT_DBGLOG                SYSLOGDIR "/dnxcld.debug.log"
#define DNX_DEFAULT_RUN_PATH              SYSRUNPATH
#define DNX_DEFAULT_USER                  DNXUSER
#define DNX_DEFAULT_GROUP                 DNXGROUP

#define elemcount(x) (sizeof(x)/sizeof(*(x)))


typedef struct DnxCfgData
{
   char * channelAgent;          //!< The agent management channel URL.
   char * logFilePath;           //!< The normal logging file path.
   char * debugFilePath;         //!< The debug logging file path.
   char * pluginPath;            //!< The file system plugin path.
   char * user;                  //!< The system reduced privileges user.
   char * group;                 //!< The system reduced privileges group.
   char * runPath;               //!< The system lock/pid file path (no file).
   unsigned debugLevel;          //!< The system global debug level.
   DnxWlmCfgData wlm;            //!< WLM specific configuration data.
} DnxCfgData;

// module statics
static DnxCfgData s_cfg;         //!< The system configuration parameters.
static DnxCfgParser * s_parser;  //!< The system configuration parser.
static DnxWlm * s_wlm = 0;       //!< The system worker thread pool.
static DnxChannel * s_agent;     //!< The agent management channel.
static char * s_progname;        //!< The base program name.
static char * s_cfgfile;         //!< The system configuration file name.
static char * s_cmdover = 0;     //!< The command line overrides string.
static int s_dbgflag = 0;        //!< The system debug flag.
static int s_shutdown = 0;       //!< The shutdown signal flag.
static int s_reconfig = 0;       //!< The reconfigure signal flag.
static int s_debugsig = 0;       //!< The debug toggle signal flag.
static int s_lockfd = -1;        //!< The system PID file descriptor.

//----------------------------------------------------------------------------

/** Format version text to an allocated string buffer.
 *
 * Caller is responsible to free memory buffer returned.
 * 
 * @param[in] base - the base file name of this program.
 *
 * @return An allocated string buffer containing version text.
 */
static char * versionText(char * base)
{
   char buf[1024];
   snprintf(buf, sizeof(buf) - 1, 
      "\n"
      "  %s Version " VERSION ", Built " __DATE__ " at " __TIME__ ".\n" 
      "  Distributed Nagios eXecutor (DNX) Client Daemon.\n"
      "  Please report bugs to <" PACKAGE_BUGREPORT ">.\n"
      "\n"
      "  Default configuration:\n"
      "    Default config file: "      DNX_DEFAULT_NODE_CONFIG_FILE "\n"
      "    Default log file: "         DNX_DEFAULT_LOG "\n"
      "    Default debug log file: "   DNX_DEFAULT_DBGLOG "\n"
      "    Default system run path: "  DNX_DEFAULT_RUN_PATH "\n"
      "    Default daemon user: "      DNX_DEFAULT_USER "\n"
      "    Default daemon group: "     DNX_DEFAULT_GROUP "\n"
//    "    Compile flags: "            COMPILE_FLAGS "\n"
#if DEBUG_HEAP
      "    Debug heap is ENABLED.\n"
#endif
#if DEBUG_LOCKS
      "    Debug locks are ENABLED.\n"
#endif
      , base
   );
   return xstrdup(buf);
}

//----------------------------------------------------------------------------

/** Display program version information to a specified stream. 
 * 
 * @param[in] fp - the stream to which version info should be printed.
 * @param[in] base - the base file name of this program.
 */
static void version(FILE * fp, char * base)
{
   char * vertxt = versionText(base);
   if (vertxt)
   {
      fprintf(fp, "%s\n", vertxt);
      xfree(vertxt);
   }
}

//----------------------------------------------------------------------------

/** Display program usage text to STDERR and exit with an error. 
 * 
 * @param[in] base - the base file name of this program.
 */
static void usage(char * base)
{

#if HAVE_GETOPT_LONG
# define OL_CFGFILE  ", --cfgfile "
# define OL_LOGFILE  ", --logfile "
# define OL_DBGFILE  ", --dbgfile "
# define OL_DBGLEVEL ", --dbglevel"
# define OL_DEBUG    ", --debug   "
# define OL_RUNPATH  ", --runpath "
# define OL_USER     ", --user    "
# define OL_GROUP    ", --group   "
# define OL_VERSION  ", --version "
# define OL_HELP     ", --help    "
#else
# define OL_CFGFILE
# define OL_LOGFILE
# define OL_DBGFILE
# define OL_DBGLEVEL
# define OL_DEBUG
# define OL_RUNPATH
# define OL_USER
# define OL_GROUP
# define OL_VERSION
# define OL_HELP
#endif

   version(stderr, base);
   fprintf(stderr, 
      "  Usage: %s [options]\n"
      "    Where [options] are:\n"
      "      -c" OL_CFGFILE  " <file>   specify the file and path of the config file.\n"
      "      -l" OL_LOGFILE  " <file>   specify the file and path of the log file.\n"
      "      -D" OL_DBGFILE  " <file>   specify the file and path of the debug log file.\n"
      "      -g" OL_DBGLEVEL " <value>  specify the level of debugging output.\n"
      "      -d" OL_DEBUG    "          enable debug mode (will not become a daemon).\n"
      "      -r" OL_RUNPATH  " <path>   specify the path of the lock/pid file.\n"
      "      -U" OL_USER     " <user>   specify the DNX client user name or id.\n"
      "      -G" OL_GROUP    " <group>  specify the DNX client group name or id.\n"
      "      -v" OL_VERSION  "          display DNX client version and exit.\n"
      "      -h" OL_HELP     "          display this help screen and exit.\n"
      "\n", 
      base
   );
   exit(-1);
}

//----------------------------------------------------------------------------

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

   static char opts[] = "c:dr:g:l:D:U:G:vh";

#if HAVE_GETOPT_LONG
   static struct option longopts[] = 
   {
      { "cfgfile",  required_argument, 0, 'c' },
      { "logfile",  required_argument, 0, 'l' },
      { "dbgfile",  required_argument, 0, 'D' },
      { "dbglevel", required_argument, 0, 'g' },
      { "debug",    no_argument,       0, 'd' },
      { "runpath",  required_argument, 0, 'r' },
      { "version",  no_argument,       0, 'v' },
      { "user",     required_argument, 0, 'U' },
      { "group",    required_argument, 0, 'G' },
      { "help",     no_argument,       0, 'h' },
      { 0, 0, 0, 0 },
   };
#endif

   int ch;
   char * cp;
   char * logfile = 0;
   char * dbgfile = 0;
   char * dbglvl = 0;
   char * user = 0;
   char * group = 0;
   char * runpath = 0;
   size_t rplen;

   // set program base name
   s_progname = (char *)((cp = strrchr(argv[0], '/')) != 0 ? (cp + 1) : argv[0]);

   opterr = 0; /* Disable error messages */

#if HAVE_GETOPT_LONG
   while ((ch = getopt_long(argc, argv, opts, longopts, 0)) != -1)
#else
   while ((ch = getopt(argc, argv, opts)) != -1)
#endif
   {
      switch (ch)
      {
         case 'c': s_cfgfile = optarg; break;
         case 'd': s_dbgflag = 1;      break;
         case 'r': runpath   = optarg; break;
         case 'U': user      = optarg; break;
         case 'G': group     = optarg; break;
         case 'g': dbglvl    = optarg; break;
         case 'l': logfile   = optarg; break;
         case 'D': dbgfile   = optarg; break;
         case 'v': version(stdout, s_progname); exit(0);
         case 'h':
         default : usage(s_progname);
      }
   }

   if (!s_cfgfile)
      s_cfgfile = DNX_DEFAULT_NODE_CONFIG_FILE;

   if (runpath && (rplen = strlen(runpath)) != 0 && runpath[rplen - 1] == '/')
      runpath[rplen - 1] = 0;

   if (s_dbgflag)
      appendString(&s_cmdover, "logFile=STDOUT\ndebugFile=STDOUT\n");

   if (logfile)
      appendString(&s_cmdover, "logFile=%s\n", logfile);

   if (dbgfile)
      appendString(&s_cmdover, "debugFile=%s\n", dbgfile);

   if (dbglvl)
      appendString(&s_cmdover, "debugLevel=%s\n", dbglvl);

   if (user)
      appendString(&s_cmdover, "user=%s\n", user);

   if (group)
      appendString(&s_cmdover, "group=%s\n", group);

   if (runpath)
      appendString(&s_cmdover, "runPath=%s\n", runpath);

   return 0;
}

//----------------------------------------------------------------------------

/** Validate a configuration data structure in context.
 * 
 * @param[in] dict - the dictionary used by the DnxCfgParser.
 * @param[in] vptrs - an array of opaque objects (either pointers or values)
 *    to be checked.
 * @param[in] passthru - an opaque pointer passed through from 
 *    dnxCfgParserCreate.
 * 
 * @return Zero on success, or a non-zero error value. This error value is
 * passed back through dnxCfgParserParse.
 */
static int validateCfg(DnxCfgDict * dict, void ** vptrs, void * passthru)
{
   int ret = DNX_ERR_INVALID;
   DnxCfgData cfg;

   // setup data structure so we can use the same functionality we had before
   // NOTE: The order of the vptrs is defined by the order of the dictionary.
   cfg.channelAgent      = (char *)            vptrs[ 0];
   cfg.logFilePath       = (char *)            vptrs[ 1];
   cfg.debugFilePath     = (char *)            vptrs[ 2];
   cfg.pluginPath        = (char *)            vptrs[ 3];
   cfg.debugLevel        = (unsigned)(intptr_t)vptrs[ 4];
   cfg.user              = (char *)            vptrs[ 5];
   cfg.group             = (char *)            vptrs[ 6];
   cfg.runPath           = (char *)            vptrs[ 7];
   cfg.wlm.dispatcher    = (char *)            vptrs[ 8];
   cfg.wlm.collector     = (char *)            vptrs[ 9];
   cfg.wlm.poolInitial   = (unsigned)(intptr_t)vptrs[10];
   cfg.wlm.poolMin       = (unsigned)(intptr_t)vptrs[11];
   cfg.wlm.poolMax       = (unsigned)(intptr_t)vptrs[12];
   cfg.wlm.poolGrow      = (unsigned)(intptr_t)vptrs[13];
   cfg.wlm.pollInterval  = (unsigned)(intptr_t)vptrs[14];
   cfg.wlm.shutdownGrace = (unsigned)(intptr_t)vptrs[15];
   cfg.wlm.reqTimeout    = (unsigned)(intptr_t)vptrs[16];
   cfg.wlm.maxRetries    = (unsigned)(intptr_t)vptrs[17];
   cfg.wlm.ttlBackoff    = (unsigned)(intptr_t)vptrs[18];
   cfg.wlm.maxResults    = (unsigned)(intptr_t)vptrs[19];

   if (!cfg.wlm.dispatcher)
      dnxLog("config: Missing channelDispatcher parameter.");
   else if (!cfg.wlm.collector)
      dnxLog("config: Missing channelCollector parameter.");
   if (cfg.wlm.poolInitial < 1 || cfg.wlm.poolInitial > cfg.wlm.poolMax)
      dnxLog("config: Invalid poolInitial parameter.");
   else if (cfg.wlm.poolMin < 1 || cfg.wlm.poolMin > cfg.wlm.poolMax)
      dnxLog("config: Invalid poolMin parameter.");
   else if (cfg.wlm.poolGrow < 1 || cfg.wlm.poolGrow >= cfg.wlm.poolMax)
      dnxLog("config: Invalid poolGrow parameter.");
   else if (cfg.wlm.pollInterval < 1)
      dnxLog("config: Invalid wlmPollInterval parameter.");
   else if (cfg.wlm.shutdownGrace < 0)
      dnxLog("config: Invalid wlmShutdownGracePeriod parameter.");
   else if (cfg.wlm.reqTimeout < 1 || cfg.wlm.reqTimeout <= cfg.wlm.ttlBackoff)
      dnxLog("config: Invalid threadRequestTimeout parameter.");
   else if (cfg.wlm.ttlBackoff >= cfg.wlm.reqTimeout)
      dnxLog("config: Invalid threadTtlBackoff parameter.");
   else if (cfg.wlm.maxResults < 1024)
      dnxLog("config: Invalid maxResultBuffer parameter.");
   else
      ret = s_wlm? dnxWlmReconfigure(s_wlm, &cfg.wlm): 0;

   return ret;
}

//----------------------------------------------------------------------------

/** Cleanup the config file parser. */
static void releaseConfig(void) 
{
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
   DnxCfgDict dict[] = 
   {  // Do NOT change the order, unless you know what you're doing!
      { "channelAgent",           DNX_CFG_URL,      &s_cfg.channelAgent      },
      { "logFile",                DNX_CFG_FSPATH,   &s_cfg.logFilePath       },
      { "debugFile",              DNX_CFG_FSPATH,   &s_cfg.debugFilePath     },
      { "pluginPath",             DNX_CFG_FSPATH,   &s_cfg.pluginPath        },
      { "debugLevel",             DNX_CFG_UNSIGNED, &s_cfg.debugLevel        },
      { "user",                   DNX_CFG_STRING,   &s_cfg.user              },
      { "group",                  DNX_CFG_STRING,   &s_cfg.group             },
      { "runPath",                DNX_CFG_FSPATH,   &s_cfg.runPath           },
      { "channelDispatcher",      DNX_CFG_URL,      &s_cfg.wlm.dispatcher    },
      { "channelCollector",       DNX_CFG_URL,      &s_cfg.wlm.collector     },
      { "poolInitial",            DNX_CFG_UNSIGNED, &s_cfg.wlm.poolInitial   },
      { "poolMin",                DNX_CFG_UNSIGNED, &s_cfg.wlm.poolMin       },
      { "poolMax",                DNX_CFG_UNSIGNED, &s_cfg.wlm.poolMax       },
      { "poolGrow",               DNX_CFG_UNSIGNED, &s_cfg.wlm.poolGrow      },
      { "wlmPollInterval",        DNX_CFG_UNSIGNED, &s_cfg.wlm.pollInterval  },
      { "wlmShutdownGracePeriod", DNX_CFG_UNSIGNED, &s_cfg.wlm.shutdownGrace },
      { "threadRequestTimeout",   DNX_CFG_UNSIGNED, &s_cfg.wlm.reqTimeout    },
      { "threadMaxRetries",       DNX_CFG_UNSIGNED, &s_cfg.wlm.maxRetries    },
      { "threadTtlBackoff",       DNX_CFG_UNSIGNED, &s_cfg.wlm.ttlBackoff    },
      { "maxResultBuffer",        DNX_CFG_UNSIGNED, &s_cfg.wlm.maxResults    },
      { "showNodeAddr",           DNX_CFG_BOOL,     &s_cfg.wlm.showNodeAddr  },
      { 0 },
   };
   char cfgdefs[] = 
      "channelAgent = udp://0:12480\n"
      "poolInitial = 20\n"
      "poolMin = 20\n"
      "poolMax = 100\n"
      "poolGrow = 10\n"
      "wlmPollInterval = 2\n"
      "wlmShutdownGracePeriod = 35\n"
      "threadRequestTimeout = 5\n"
      "threadMaxRetries = 12\n"
      "threadTtlBackoff = 1\n"
      "maxResultBuffer = 1024\n"
      "showNodeAddr = Yes\n"
      "logFile = " DNX_DEFAULT_LOG "\n"
      "debugFile = " DNX_DEFAULT_DBGLOG "\n"
      "user = " DNX_DEFAULT_USER "\n"
      "group = " DNX_DEFAULT_GROUP "\n"
      "runPath = " DNX_DEFAULT_RUN_PATH "\n";

   int ret;

   // create global configuration parser object
   if ((ret = dnxCfgParserCreate(cfgdefs, s_cfgfile, s_cmdover, dict, 
         validateCfg, &s_parser)) != 0)
      return ret;

   // parse config file
   if ((ret = dnxCfgParserParse(s_parser, 0)) != 0)
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
   sprintf(lockFile, "%s/%s.pid", s_cfg.runPath, base);

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
   sprintf(lockFile, "%s/%s.pid", s_cfg.runPath, base);

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

   return 0;   // continue execution as a daemon
}

//----------------------------------------------------------------------------

/** Drop privileges to configured user and group.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dropPrivileges(void)
{
   // drop privileges if running as root
   if (getuid() == 0)
   {
      struct passwd * pwd;
      struct group * grp;
      uid_t uid;
      gid_t gid;

      dnxLog("Running as root; attempting to drop privileges...");

      if ((pwd = getpwnam(s_cfg.user)) != 0)
         uid = pwd->pw_uid;
      else
      {
         char * ep;
         uid = (uid_t)strtoul(s_cfg.user, &ep, 10);
         if (s_cfg.user + strlen(s_cfg.user) > ep)
         {
            dnxLog("Invalid user name or id specified: %s.", s_cfg.user);
            return -1;
         }
      }

      if ((grp = getgrnam(s_cfg.group)) != 0)
         gid = grp->gr_gid;
      else
      {
         char * ep;
         gid = (gid_t)strtoul(s_cfg.group, &ep, 10);
         if (s_cfg.group + strlen(s_cfg.group) > ep)
         {
            dnxLog("Invalid group name or id specified: %s.", s_cfg.group);
            return -1;
         }
      }

      // drop privileges if root user not requested
      if (uid != 0)
      {
         int ret;
         if ((ret = setgid(gid)) == -1 || (ret = setuid(uid)) == -1)
         {
            dnxLog("Failed to drop privileges: %s. Terminating.", strerror(errno));
            return -1;
         }

         grp = getgrgid(getgid());
         pwd = getpwuid(getuid());

         assert(grp && pwd);

         dnxLog("Privileges dropped to %s:%s.", pwd->pw_name, grp->gr_name);
      }
      else
         dnxLog("Root user requested; oh well...");
   }
   return 0;
}

//----------------------------------------------------------------------------

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
            ocp? ocp->channelAgent: "<unknown>", ncp->channelAgent);

   if (strcmp(ocp->logFilePath, ncp->logFilePath) != 0)
      dnxLog("Config parameter 'logFile' changed from %s to %s. "
            "NOTE: Changing the log file path requires a restart.", 
            ocp? ocp->logFilePath: "<unknown>", ncp->logFilePath);

   if (strcmp(ocp->debugFilePath, ncp->debugFilePath) != 0)
      dnxLog("Config parameter 'debugFile' changed from %s to %s. "
            "NOTE: Changing the debug log file path requires a restart.", 
            ocp? ocp->debugFilePath: "<unknown>", ncp->debugFilePath);

   if (strcmp(ocp->pluginPath, ncp->pluginPath) != 0)
      dnxLog("Config parameter 'pluginPath' changed from %s to %s.",
            ocp? ocp->pluginPath: "<unknown>", ncp->pluginPath);

   if (strcmp(ocp->user, ncp->user) != 0)
      dnxLog("Config parameter 'user' changed from %s to %s. "
            "NOTE: Changing the dnx user requires a restart.", 
            ocp? ocp->user: "<unknown>", ncp->user);

   if (strcmp(ocp->group, ncp->group) != 0)
      dnxLog("Config parameter 'group' changed from %s to %s. "
            "NOTE: Changing the dnx group requires a restart.", 
            ocp? ocp->group: "<unknown>", ncp->group);

   if (strcmp(ocp->runPath, ncp->runPath) != 0)
      dnxLog("Config parameter 'runPath' changed from %s to %s. "
            "NOTE: Changing the dnx pid/lock file directory requires a restart.", 
            ocp? ocp->runPath: "<unknown>", ncp->runPath);

   if (ocp->debugLevel != ncp->debugLevel)
      dnxLog("Config parameter 'debugLevel' changed from %u to %u.", 
            ocp? ocp->debugLevel: 0, ncp->debugLevel);
}

//----------------------------------------------------------------------------

/** Build an allocated response buffer for requested stats values.
 * 
 * @param[in] req - The requested stats in comma-separated string format.
 * 
 * @return A pointer to an allocated response buffer, or 0 if out of memory.
 */
static char * buildMgmtStatsReply(char * req)
{
   DnxWlmStats ws;
   struct { char * str; unsigned * stat; } rs[] = 
   {
      { "jobsok",       &ws.jobs_succeeded     },
      { "jobsfailed",   &ws.jobs_failed        },
      { "thcreated",    &ws.threads_created    },
      { "thdestroyed",  &ws.threads_destroyed  },
      { "thexist",      &ws.total_threads      },
      { "thactive",     &ws.active_threads     },
      { "reqsent",      &ws.requests_sent      },
      { "jobsrcvd",     &ws.jobs_received      },
      { "minexectm",    &ws.min_exec_time      },
      { "avgexectm",    &ws.avg_exec_time      },
      { "maxexectm",    &ws.max_exec_time      },
      { "avgthexist",   &ws.avg_total_threads  },
      { "avgthactive",  &ws.avg_active_threads },
      { "threadtm",     &ws.thread_time        },
      { "jobtm",        &ws.job_time           },
   };
   char * rsp = 0;

   assert(req);

   dnxWlmGetStats(s_wlm, &ws);

   // trim leading ws
   while (isspace(*req)) req++;

   while (*req)
   {
      char * ep, * np;
      unsigned i;

      // find start of next string or end
      if ((np = strchr(req, ',')) == 0)
         np = req + strlen(req);

      // trim trailing ws
      ep = np;
      while (ep > req && isspace(ep[-1])) ep--;

      // search table for sub-string, append requested stat to rsp
      for (i = 0; i < elemcount(rs); i++)
         if (memcmp(req, rs[i].str, ep - req) == 0)
         {
            if (appendString(&rsp, "%u,", *rs[i].stat) != 0)
            {
               xfree(rsp);
               return 0;
            }
            break;
         }

      // move to next sub-string or end
      if (*(req = np)) req++;

      // trim leading ws
      while (isspace(*req)) req++;
   }
   if (rsp)
   {
      size_t len = strlen(rsp);
      if (len && rsp[len - 1] == ',') rsp[len - 1] = 0;
   }
   return rsp;
}

//----------------------------------------------------------------------------

/** Build an allocated response buffer for the current configuration.
 * 
 * @return A pointer to an allocated response buffer, or 0 if out of memory.
 */
static char * buildMgmtCfgReply(void)
{
   char * buf;
   size_t bufsz = 0;

   if (dnxCfgParserGetCfg(s_parser, 0, &bufsz) != 0)
      return 0;

   if ((buf = (char *)xmalloc(bufsz)) != 0)
      if (dnxCfgParserGetCfg(s_parser, buf, &bufsz) != 0)
         xfree(buf), (buf = 0);

   return buf;
}

//----------------------------------------------------------------------------

/** Build an allocated response buffer for the HELP request.
 * 
 * @return A pointer to an allocated response buffer, or 0 if out of memory.
 */
static char * buildHelpReply(void)
{
   static char * help = 
         "DNX Client Management Commands:\n"
         "  SHUTDOWN\n"
         "  RECONFIGURE\n"
         "  DEBUGTOGGLE\n"
         "  RESETSTATS\n"
         "  GETSTATS stat-list\n"
         "    stat-list is a comma-delimited list of stat names:\n"
         "      jobsok      - number of successful jobs\n"
         "      jobsfailed  - number of unsuccessful jobs\n"
         "      thcreated   - number of threads created\n"
         "      thdestroyed - number of threads destroyed\n"
         "      thexist     - number of threads currently in existence\n"
         "      thactive    - number of threads currently active\n"
         "      reqsent     - number of requests sent to DNX server\n"
         "      jobsrcvd    - number of jobs received from DNX server\n"
         "      minexectm   - minimum job execution time\n"
         "      avgexectm   - average job execution time\n"
         "      maxexectm   - maximum job execution time\n"
         "      avgthexist  - average threads in existence\n"
         "      avgthactive - average threads processing jobs\n"
         "      threadtm    - total thread life time\n"
         "      jobtm       - total job processing time\n"
         "    Note: Stats are returned in the order they are requested.\n"
         "  GETCONFIG\n"
         "  GETVERSION\n"
         "  HELP";
   return xstrdup(help);
}

//----------------------------------------------------------------------------

/** Release a previously copied configuration data structure.
 * 
 * @param[in] cpy - the structure to be freed.
 */
static void freeCfgData(DnxCfgData * cpy)
{
   xfree(cpy->channelAgent);
   xfree(cpy->logFilePath);
   xfree(cpy->debugFilePath);
   xfree(cpy->pluginPath);
   xfree(cpy->user);
   xfree(cpy->group);
   xfree(cpy->runPath);
   xfree(cpy->wlm.dispatcher);
   xfree(cpy->wlm.collector);
   xfree(cpy);
}

//----------------------------------------------------------------------------

/** Make a dynamic copy of all configuration data.
 * 
 * @param[in] org - the structure to be copied.
 * 
 * @return Pointer to allocated copy, or NULL on memory allocation failure.
 */
static DnxCfgData * copyCfgData(DnxCfgData * org)
{
   DnxCfgData * cpy;

   // make new config structure
   if ((cpy = (DnxCfgData *)xmalloc(sizeof *cpy)) == 0)
      return 0;

   // copy all values
   *cpy = *org;

   // attempt to make string buffer copies
   cpy->channelAgent = xstrdup(org->channelAgent);
   cpy->logFilePath = xstrdup(org->logFilePath);
   cpy->debugFilePath = xstrdup(org->debugFilePath);
   cpy->pluginPath = xstrdup(org->pluginPath);
   cpy->user = xstrdup(org->user);
   cpy->group = xstrdup(org->group);
   cpy->runPath = xstrdup(org->runPath);
   cpy->wlm.dispatcher = xstrdup(org->wlm.dispatcher);
   cpy->wlm.collector = xstrdup(org->wlm.collector);

   // if any buffer copies failed, free everything, return NULL
   if (cpy->channelAgent == 0 || cpy->logFilePath == 0
         || cpy->debugFilePath == 0 || cpy->pluginPath == 0
         || cpy->user == 0 || cpy->group == 0 || cpy->runPath == 0
         || cpy->wlm.dispatcher == 0 || cpy->wlm.collector == 0)
      freeCfgData(cpy), cpy = 0;

   return cpy;
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
      // wait 1 second for a request; process the request, if valid
      if ((ret = dnxWaitForMgmtRequest(s_agent, &Msg, Msg.address, 1)) == DNX_OK)
      {
         DnxMgmtReply Rsp;

         // setup some default response values
         Rsp.xid = Msg.xid;
         Rsp.status = DNX_REQ_ACK;
         Rsp.reply = 0;

         // perform the requested action
         if (!strcmp(Msg.action, "SHUTDOWN"))
         {
            s_shutdown = 1;
            Rsp.reply = xstrdup("OK");
         }
         else if (!strcmp(Msg.action, "RECONFIGURE"))
         {
            s_reconfig = 1;
            Rsp.reply = xstrdup("OK");
         }
         else if (!strcmp(Msg.action, "DEBUGTOGGLE"))
         {
            s_debugsig = 1;
            Rsp.reply = xstrdup("OK");
         }
         else if (!strcmp(Msg.action, "RESETSTATS"))
         {
            dnxWlmResetStats(s_wlm);
            Rsp.reply = xstrdup("OK");
         }
         if (!memcmp(Msg.action, "GETSTATS ", 9))
         {
            if ((Rsp.reply = buildMgmtStatsReply(Msg.action + 9)) == 0)
               Rsp.status = DNX_REQ_NAK;
         }
         else if (!strcmp(Msg.action, "GETCONFIG"))
         {
            if ((Rsp.reply = buildMgmtCfgReply()) == 0)
               Rsp.status = DNX_REQ_NAK;
         }
         else if (!strcmp(Msg.action, "GETVERSION"))
         {
            if ((Rsp.reply = versionText(s_progname)) == 0)
               Rsp.status = DNX_REQ_NAK;
         }
         else if (!strcmp(Msg.action, "HELP"))
         {
            if ((Rsp.reply = buildHelpReply()) == 0)
               Rsp.status = DNX_REQ_NAK;
         }

         // send response, log response failures
         if ((ret = dnxSendMgmtReply(s_agent, &Rsp, Msg.address)) != 0)
            dnxLog("Agent response failure: %s.", dnxErrorString(ret));

         // free request and reply message buffers
         xfree(Rsp.reply);
         xfree(Msg.action);
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxLog("Agent channel failure: %s.", dnxErrorString(ret));

      if (s_reconfig)
      {
         DnxCfgData * old;

         dnxLog("Agent received RECONFIGURE request. Reconfiguring...");

         // reparse config file into temporary cfg structure and validate
         old = copyCfgData(&s_cfg);
         if ((ret = dnxCfgParserParse(s_parser, 0)) == 0)
            logGblConfigChanges(old, &s_cfg);
         if (old) freeCfgData(old);
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
   dnxLogInit(s_cfg.logFilePath, s_cfg.debugFilePath, 0, &s_cfg.debugLevel);

   dnxLog("-------- DNX Client Daemon Version %s Startup --------", VERSION);
   dnxLog("Copyright (c) 2006-2008 Intellectual Reserve. All rights reserved.");
   dnxLog("Configuration file: %s.", s_cfgfile);
   dnxLog("Agent: %s.", s_cfg.channelAgent);
   dnxLog("Dispatcher: %s.", s_cfg.wlm.dispatcher);
   dnxLog("Collector: %s.", s_cfg.wlm.collector);
   if (s_cfg.debugFilePath && s_cfg.debugLevel != 0)
   {
      dnxLog("Debug logging enabled at level %d to %s.", 
            s_cfg.debugLevel, s_cfg.debugFilePath);
#if DEBUG_HEAP
      dnxLog("Debug heap is enabled.");
#endif
#if DEBUG_LOCKS
      dnxLog("Debug locks are enabled.");
#endif
   }

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

   // drop privileges as per configuration
   if ((ret = dropPrivileges()) != 0)
      goto e2;

   // create pid file if not running in debug mode
   if (!s_dbgflag && (ret = createPidFile(s_progname)) != 0)
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
e0:dnxLog("-------- DNX Client Daemon Shutdown Complete --------");

   xheapchk();    // works when debug heap is compiled in

   return ret;
}

/*--------------------------------------------------------------------------*/

