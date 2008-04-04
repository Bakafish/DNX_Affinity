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

#include "dnxError.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxClientMain.h"
#include "dnxConfig.h"
#include "dnxWLM.h"
#include "dnxPlugin.h"
#include "dnxLogging.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#else
# define VERSION  "0.20"
#endif

#define MAX_CMD_BUFF 2048

DnxGlobalData dnxGlobalData;
static char * szProg;
static char * ConfigFile = DNX_NODE_CONFIG;

static int Debug = 0;
static int gotSig = 0;
static int lockFd = -1;

extern char * optarg;
extern int optind, opterr, optopt;

extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind);

static int getOptions (int argc, char **argv);
static int getConfig (DnxGlobalData *gData);
static int verifyFacility (char *szFacility, int *nFacility);
static int initClientComm (DnxGlobalData *gData);
static int releaseClientComm (DnxGlobalData *gData);
static int initClientThreads (DnxGlobalData *gData);
static int releaseClientThreads (DnxGlobalData *gData);
static int processCommands(DnxGlobalData *gData);
static void sighandler (int sig);
static void daemonize (void);
static int createPidFile (char *base);
static void removePidFile (char *base);
static void sig_debug (int sig);
static void usage (void);
static void version (void);

//----------------------------------------------------------------------------

int main (int argc, char **argv)
{
   char *cp;
   int ret;


   // Set program base name
   szProg = (char *)((cp = strrchr(argv[0], '/')) != NULL ? (cp+1) : argv[0]);

   // Open the syslog
   openlog(szProg, LOG_PID, LOG_LOCAL7);

   // Announce our presence
   syslog(LOG_INFO, "***** DNX Client Version %s Startup *****", VERSION);

   // Clear global data structure
   memset(&dnxGlobalData, 0, sizeof(dnxGlobalData));

   // Get command line options
   if ((ret = getOptions(argc, argv)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Command-line option processing failure: %d", ret);
      exit(1);
   }

   // Read configuration file
   if ((ret = getConfig(&dnxGlobalData)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Configuration file processing failure: %d", ret);
      exit(2);
   }

   // Load dynamic plugin modules (e.g., nrpe, snmp, etc.)
   if ((ret = dnxPluginInit(dnxGlobalData.pluginPath)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Plugin initialization failure: %d", ret);
      exit(3);
   }

   // Install signal handlers
   signal(SIGHUP,  sighandler);
   signal(SIGINT,  sighandler);
   signal(SIGQUIT, sighandler);
   signal(SIGABRT, sighandler);
   signal(SIGTERM, sighandler);
   signal(SIGPIPE, SIG_IGN);
   signal(SIGALRM, SIG_IGN);
   signal(SIGUSR1, sig_debug);
   signal(SIGUSR2, SIG_IGN);

   // Daemonize
   if (!Debug) daemonize();

   // Initialize the communications stack
   if ((ret = initClientComm(&dnxGlobalData)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Failed to initialize communications: %d", ret);
      goto abend;
   }

   // Start the Work-Load-Manager thread
   if ((ret = initClientThreads(&dnxGlobalData)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Failed to initialize threads: %d", ret);
      goto abend;
   }


   // Wait for agent commands on UDP channel
   ret = processCommands(&dnxGlobalData);
   if (dnxGlobalData.debug)
      syslog(LOG_DEBUG, "dnxMain: Command-loop exit code: %d", ret);


abend:
   // Wait for thread termination
   releaseClientThreads(&dnxGlobalData);

   // Release the comm stack
   releaseClientComm(&dnxGlobalData);

   // Release dynamic plugin modules
   if ((ret = dnxPluginRelease()) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxMain: Plugin release failure: %d", ret);
   }

   // Remove Pid File
   removePidFile(szProg);

   syslog(LOG_INFO, "dnxMain: Shutdown complete.");

   // Close the syslog
   closelog();

   return 0;
}

//----------------------------------------------------------------------------

static int getOptions (int argc, char **argv)
{
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

static int getConfig (DnxGlobalData *gData)
{
   int ret;

   // Set default logging facility
   gData->dnxLogFacility = LOG_LOCAL7;

   // Initialize global data
   initGlobals();

   // Parse config file
   if ((ret = parseFile(ConfigFile)) != 0)
   {
      syslog(LOG_ERR, "getConfig: Failed to parse config file: %d", ret);
      return ret;
   }

   // Validate configuration items
   ret = DNX_ERR_INVALID;
   if (!gData->channelAgent)
      syslog(LOG_ERR, "getConfig: Missing channelAgent parameter");
   else if (!gData->channelDispatcher)
      syslog(LOG_ERR, "getConfig: Missing channelDispatcher parameter");
   else if (!gData->channelCollector)
      syslog(LOG_ERR, "getConfig: Missing channelCollector parameter");
   else if (gData->poolInitial < 1 || gData->poolInitial > gData->poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolInitial parameter");
   else if (gData->poolMin < 1 || gData->poolMin > gData->poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolMin parameter");
   else if (gData->poolGrow < 1 || gData->poolGrow >= gData->poolMax)
      syslog(LOG_ERR, "getConfig: Missing or invalid poolGrow parameter");
   else if (gData->wlmPollInterval < 1)
      syslog(LOG_ERR, "getConfig: Missing or invalid wlmPollInterval parameter");
   else if (gData->wlmShutdownGracePeriod < 0)
      syslog(LOG_ERR, "getConfig: Missing or invalid wlmShutdownGracePeriod parameter");
   else if (gData->threadRequestTimeout < 1 || gData->threadRequestTimeout <= gData->threadTtlBackoff)
      syslog(LOG_ERR, "getConfig: Missing or invalid threadRequestTimeout parameter");
   else if (gData->threadTtlBackoff < 1 || gData->threadTtlBackoff >= gData->threadRequestTimeout)
      syslog(LOG_ERR, "getConfig: Missing or invalid threadTtlBackoff parameter");
   else if (gData->maxResultBuffer < 1024)
      syslog(LOG_ERR, "getConfig: Missing or invalid maxResultBuffer parameter");
   else if (gData->logFacility &&   /* If logFacility is defined, then */
         verifyFacility(gData->logFacility, &(gData->dnxLogFacility)) == -1)
   {
      dnxSyslog(LOG_ERR, "getConfig: Invalid syslog facility for logFacility: %s", gData->logFacility);
   }
   else
      ret = DNX_OK;

   return ret;
}

//----------------------------------------------------------------------------

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

static int initClientComm (DnxGlobalData *gData)
{
   int ret = DNX_OK;

   gData->pAgent = NULL;

   // Initialize the DNX comm stack
   if ((ret = dnxChanMapInit(NULL)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxChanMapInit failed: %d", ret);
      return ret;
   }

   // Create a channel for receiving DNX Client Requests (e.g., Shutdown, Status, etc.)
   if ((ret = dnxChanMapAdd("Agent", gData->channelAgent)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxChanMapInit(Agent) failed: %d", ret);
      return ret;
   }

   // Attempt to open the Agent channel
   if ((ret = dnxConnect("Agent", &(gData->pAgent), DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      syslog(LOG_ERR, "initClientComm: dnxConnect(Agent) failed: %d", ret);
      return ret;
   }
   
   return ret;
}

//----------------------------------------------------------------------------

static int releaseClientComm (DnxGlobalData *gData)
{
   int ret;

   // Close the Agent channel
   if ((ret = dnxDisconnect(gData->pAgent)) != DNX_OK)
      syslog(LOG_ERR, "releaseClientComm: Failed to disconnect Agent channel: %d", ret);

   // Remove the Agent channel
   if ((ret = dnxChanMapDelete("Agent")) != DNX_OK)
      syslog(LOG_ERR, "releaseClientComm: Failed to delete Agent channel: %d", ret);

   // Release the DNX comm stack
   if ((ret = dnxChanMapRelease()) != DNX_OK)
      syslog(LOG_ERR, "releaseClientComm: Failed to release DNX comm stack: %d", ret);

   return DNX_OK;
}

//----------------------------------------------------------------------------

static int initClientThreads (DnxGlobalData *gData)
{
   int rc, ret = DNX_OK;

   // Initialize the thread data mutex
   pthread_mutexattr_init(&(gData->threadMutexAttr));
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
   pthread_mutexattr_settype(&(gData->threadMutexAttr), PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
   pthread_mutex_init(&(gData->threadMutex), &(gData->threadMutexAttr));

   // Initialize the job data mutex
   pthread_mutexattr_init(&(gData->jobMutexAttr));
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
   pthread_mutexattr_settype(&(gData->jobMutexAttr), PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
   pthread_mutex_init(&(gData->jobMutex), &(gData->jobMutexAttr));

   // Kick-off the Work Load Manager thread
   if ((rc = pthread_create(&(gData->tWLM), NULL, dnxWLM, (void *)gData)) != 0)
   {
      syslog(LOG_ERR, "initClientThreads: Failed to create Work-Load-Manager thread: %d", rc);
      ret = DNX_ERR_THREAD;
   }

   syslog(LOG_INFO, "initClientThreads: Create WLM thread %lx", gData->tWLM);

   return ret;
}

//----------------------------------------------------------------------------

static int releaseClientThreads (DnxGlobalData *gData)
{
   int ret;

   // Signal the WLM thread to clean-up using WLM's confition variable (instead of invoking its cancel routine)
   if (gData->debug)
      syslog(LOG_DEBUG, "releaseClientThreads: Signalling termination condition to WLM thread %lx", gData->tWLM);

   // Lock the thread data mutex
   pthread_mutex_lock(&(gData->threadMutex));

   // Set the latest time by which all worker threads must be terminated
   gData->noLaterThan = time(NULL) + gData->wlmShutdownGracePeriod;
   gData->terminate = 1;      // Set the worker thread term flag
   pthread_cond_signal(&(gData->wlmCond));   // Signal the WLM

   // Unlock the thread data mutex
   pthread_mutex_unlock(&(gData->threadMutex));

   // Wait for the WLM thread to exit
   if (gData->debug)
      syslog(LOG_DEBUG, "releaseClientThreads: Waiting to join WLM thread %lx", gData->tWLM);
   if ((ret = pthread_join(gData->tWLM, NULL)) != 0)
      syslog(LOG_ERR, "releaseClientThreads: pthread_join(Agent) failed with ret = %d", ret);

   // wait for all threads to be gone...
   while (dnxGetThreadsActive() > 0)
      sleep(100);

   // Destroy the thread data mutex
   if (pthread_mutex_destroy(&(gData->threadMutex)) != 0)
   {
      syslog(LOG_ERR, "releaseClientThreads: Unable to destroy thread data mutex: mutex is in use!");
   }
   pthread_mutexattr_destroy(&(gData->threadMutexAttr));

   // Unlock the job data mutex
   pthread_mutex_unlock(&(gData->jobMutex));

   // Destroy the job data mutex
   if (pthread_mutex_destroy(&(gData->jobMutex)) != 0)
   {
      syslog(LOG_ERR, "releaseClientThreads: Unable to destroy job data mutex: mutex is in use!");
   }
   pthread_mutexattr_destroy(&(gData->jobMutexAttr));

   return DNX_OK;
}

//----------------------------------------------------------------------------

static int processCommands(DnxGlobalData *gData)
{
   DnxMgmtRequest Msg;
   int ret;

   syslog(LOG_INFO, "processCommands: DNX Client agent awaiting commands");

   // Wait on the Agent socket for a request
   while (!gotSig && (ret = dnxGetMgmtRequest(gData->pAgent, &Msg, Msg.address, 10)) != DNX_ERR_RECEIVE && ret != DNX_ERR_OPEN)
   {
      // Process the request, if valid
      if (ret == DNX_OK)
      {
         // Perform the requested action
         if (!strcmp(Msg.action, "SHUTDOWN"))
         {
            syslog(LOG_INFO, "processCommands: DNX Client agent received SHUTDOWN command");
            break;
         }

         // Free message string
         if (Msg.action) free(Msg.action);
      }
   }

   return ret;
}

//----------------------------------------------------------------------------

int dnxGetThreadsActive (void)
{
   int value;

   // Acquire the lock on the global thread data
   if (pthread_mutex_lock(&(dnxGlobalData.threadMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   value = dnxGlobalData.threadsActive;

   // Release the lock on the global thread data
   if (pthread_mutex_unlock(&(dnxGlobalData.threadMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxGetThreadsActive: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return value;
}

//----------------------------------------------------------------------------

int dnxSetThreadsActive (int value)
{
   // Acquire the lock on the global thread data
   if (pthread_mutex_lock(&(dnxGlobalData.threadMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // Test value for positive or negative effect
   if (value > 0)
   {
      dnxGlobalData.threadsActive++;
      dnxGlobalData.threadsCreated++;
   }
   else if (value < 0)
   {
      dnxGlobalData.threadsActive--;
      dnxGlobalData.threadsDestroyed++;
   }

   // Set return code to new value of threadsActive global
   value = dnxGlobalData.threadsActive;

   // Release the lock on the global thread data
   if (pthread_mutex_unlock(&(dnxGlobalData.threadMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxSetThreadsActive: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return value;
}

//----------------------------------------------------------------------------

int dnxGetJobsActive (void)
{
   int value;

   // Acquire the lock on the global job data
   if (pthread_mutex_lock(&(dnxGlobalData.jobMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   value = dnxGlobalData.jobsActive;

   // Release the lock on the global job data
   if (pthread_mutex_unlock(&(dnxGlobalData.jobMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxGetJobsActive: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return value;
}

//----------------------------------------------------------------------------

int dnxSetJobsActive (int value)
{
   // Acquire the lock on the global job data
   if (pthread_mutex_lock(&(dnxGlobalData.jobMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // Test value for positive or negative effect
   if (value > 0)
   {
      dnxGlobalData.jobsActive++;
      dnxGlobalData.jobsProcessed++;
   }
   else if (value < 0)
   {
      dnxGlobalData.jobsActive--;
   }

   // Set return code to new value of jobsActive global
   value = dnxGlobalData.jobsActive;

   // Release the lock on the global job data
   if (pthread_mutex_unlock(&(dnxGlobalData.jobMutex)) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         syslog(LOG_ERR, "dnxSetJobsActive: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   return value;
}

//----------------------------------------------------------------------------

static void sighandler (int sig)
{
   // Set global cleanup variable
   gotSig = 1;

   syslog(LOG_WARNING, "%s: Exiting on signal %d", szProg, sig);
}

//----------------------------------------------------------------------------

static void daemonize (void)
{
   int pid, fd;

   /* Fork to allow parent process to exit */
   if ((pid = fork()) < 0)
   {
      syslog(LOG_ERR, "%s: Failed to fork process: %s", szProg, strerror(errno));
      exit(1);
   }
   else if (pid != 0)
      exit(0);

   /* Become process group leader */
   setsid();

   /* Fork again to allow process group leader to exit */
   if ((pid = fork()) < 0)
   {
      syslog(LOG_ERR, "%s: Failed to fork process: %s", szProg, strerror(errno));
      exit(1);
   }
   else if (pid != 0)
      exit(0);

   /* Change our working directory to root directory so as to not keep any filesystems open */
   chdir("/");

   /* Allow us complete control over any newly created files */
   umask(0);

   /* Close and redirect stdin, stdout, stderr */
   fd = open("/dev/null", O_RDWR);
   dup2(fd, 0);
   dup2(fd, 1);
   dup2(fd, 2);

   /* Create pid file */
   if (createPidFile(szProg) != 0)
   {
      exit(1);
   }
}

//----------------------------------------------------------------------------

static int createPidFile (char *base)
{
   char lockFile[1024];
   char szPid[32];

   /* Create lock-file name */
   sprintf(lockFile, "/var/run/%s.pid", base);

   /* Open the lock file */
   if ((lockFd = open(lockFile, O_RDWR | O_CREAT, 0644)) < 0)
   {
      syslog(LOG_ERR, "%s: Unable to create lock file: %s: %s", szProg, lockFile, strerror(errno));
      return (-1);
   }

   /* Attempt to lock the lock-file */
   if (flock(lockFd, LOCK_EX | LOCK_NB) != 0)
   {
      close(lockFd);
      syslog(LOG_NOTICE, "%s: Lock file already in-use: %s: %s", szProg, lockFile, strerror(errno));
      return (-1);
   }

   /* Create a string containing our PID */
   sprintf(szPid, "%d\n", getpid());

   /* Write our PID to the lock file */
   if (write(lockFd, szPid, strlen(szPid)) != strlen(szPid))
   {
      close(lockFd);
      syslog(LOG_NOTICE, "%s: Failed to write pid to lock file: %s: %s", szProg, lockFile, strerror(errno));
      return (-1);
   }

   return 0;
}

//----------------------------------------------------------------------------

static void removePidFile (char *base)
{
   char lockFile[1024];

   /* Create lock-file name */
   sprintf(lockFile, "/var/run/%s.pid", base);

   /* Remove the lock file - we do this before closing it in order to prevent
    * race conditions between the closing and removing operations.
    */
   if (unlink(lockFile) != 0)
   {
      syslog(LOG_WARNING, "%s: Failed to remove lock file: %s: %s", szProg, lockFile, strerror(errno));
   }

   /* Close/unlock the lock file */
   if (lockFd >= 0) close(lockFd);
}

//----------------------------------------------------------------------------

static void sig_debug (int sig)
{
   Debug ^= 1;
   syslog(LOG_NOTICE, "%s: Received signal %d: Debug mode toggled to %s", szProg, sig, (char *)(Debug ? "ON" : "OFF"));
}

//----------------------------------------------------------------------------

static void usage (void)
{
   fprintf(stderr, "\nusage: %s [-c config-file] [-d] [-v]\n", szProg);
   fprintf(stderr, "\nWhere:\n");
   fprintf(stderr, "\t-c Specify the location of the config file\n");
   fprintf(stderr, "\t-d Enable debug mode (NOTE: Will not become a background daemon)\n");
   fprintf(stderr, "\t-v Display version and exit\n\n");
   exit(1);
}

//----------------------------------------------------------------------------

static void version (void)
{
   printf("%s %s\n", szProg, VERSION);
   exit(0);
}

/*--------------------------------------------------------------------------*/

