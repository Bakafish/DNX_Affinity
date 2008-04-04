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

/** Implements encapsulated communications transport functions.
 *
 * Exports:
 * 
 *    1. dnxConnect (char *channel_name, ...)
 *    2. dnxDisconnect (dnxChannel *channel)
 *    3. dnxGet (dnxChannel *channel, dnxRequest *req, dnxMsg **msg, int timeout)
 *    4. dnxPut (dnxChannel *channel, dnxRequest req, dnxMsg *msg, int timeout)
 *    5. dnxChannelDebug (dnxChannel *channel, int doDebug)
 * 
 * Connection targets for dnxConnect are specified as Message Queue Names.
 * 
 * Message Queue Names are specified in the configuration file and by DNX_MSG_REGISTER messages.
 * The specification is of the form:
 * 
 *    [MessageQueues]
 *       MessageQueueName = URL
 * 
 * The currently supported URLs are:
 * 
 *    1. tcp://hostname:port/
 *    2. udp://hostname:port/
 *    3. msgq://message-queue-ID/
 * 
 * Currently recognized Message Queue Names are:
 * 
 *    1. Scheduler   - Dispatchers use this to communicate with the Nagios Scheduler
 *    2. Jobs        - Workers use this to receive Jobs from Dispatchers and the WLM (for shutdown)
 *    3. Results     - Workers use this to post completed Jobs to the Collector
 *    4. Collector   - Local Collectors use this to communicate with the Master Collector
 * 
 *    Note: Can multiple heavy processes write to the same inherited socket descriptor?
 * 
 * DNX messages are transport independent and have the following properties:
 * 
 *    1. dnxRequest - enumerated constant identifying the message payload
 *    2. XML (REST-style) body
 * 
 * Currently supported DNX messages are:
 * 
 *    1. DNX_MSG_REGISTER
 *    2. DNX_MSG_DEREGISTER
 *    3. DNX_MSG_GET_JOB
 *    4. DNX_MSG_PUT_JOB
 *    5. DNX_MSG_GET_RESULT
 *    6. DNX_MSG_PUT_RESULT
 * 
@verbatim   
     
   Structure: DNX_MSG_REGISTER
   Issued By: Dispatcher
   Issued To: Scheduler
   
      <dnxMessage>
         <Request>Register</Request>
         <GUID>123456789</GUID>
      </dnxMessage>
   
   Structure: DNX_MSG_DEREGISTER
   Issued By: Dispatcher
   Issued To: Scheduler
   
      <dnxMessage>
         <Request>Deregister</Request>
         <GUID>123456789</GUID>
      </dnxMessage>
   
   Structure: DNX_MSG_GET_JOB
   Issued By: Dispatcher, Worker
   Issued To: Scheduler, Dispatcher
   
      <dnxMessage>
         <Request>GetJob</Request>
         <JobCapacity>5</JobCapacity>
      </dnxMessage>
   
   DNX_MSG_PUT_JOB Structure
   Issued By: Scheduler, Dispatcher
   Issued To: Dispatcher, Worker
   
      <dnxMessage>
         <Request>PutJob</Request>
         <GUID>abc123</GUID>
         <State>Pending</State>
         <Command>check_spam</Command>
         <Param>param1</Param>
         <Param>param2</Param>
         <StartTime>2006-06-20 15:00:00</StartTime>
      </dnxMessage>
   
   DNX_MSG_PUT_RESULT Structure
   Issued By: Worker, Collector
   Issued To: Collector, Reaper
   
      <dnxMessage>
         <Request>PutResult</Request>
         <GUID>abc123</GUID>
         <State>Completed</State>
         <EndTime>2006-06-20 15:00:05</EndTime>
         <ResultCode>0</ResultCode>
         <ResultData>OK: Everything's okie-dokie!</ResultData>
      </dnxMessage>
   
   DNX_MSG_GET_RESULT Structure
   Issued By: Reaper
   Issued To: Collector
   
      <dnxMessage>
         <Request>GetResult</Request>
         <GUID>abc123</GUID>
      </dnxMessage>

@endverbatim   
 * The DNX Objects are:
 * 
 *    1. DNX_JOB
 *    2. DNX_RESULT
 *    3. DNX_AGENT
 * 
 * Each DNX Job Object has:
 * 
 *    1. GUID - Assigned at Job creation by the Scheduler
 *    2. State: Pending, Executing, Completed, Cancelled
 *    3. Execution Command
 *    4. Execution Parameters
 *    5. Execution Start Time
 *    5. Execution End Time
 *    6. Result Code
 *    7. Result Data
 * 
 * When and object is serialized and transmitted, only those relevant portions of the object
 * are transmitted - order to optimize bandwith usage and processing time.
 * 
 * For example, a DNX Job object as transmitted to a Dispatcher, via a DNX_MSG_PUT_JOB, will
 * only include the following DNX Job attributes:
 * 
 *    GUID, State, Execution Command, Execution Parameters and Execution Start Time.
 * 
 * Conversely, a DNX Job object as transmitted to a Collector, via a DNS_PUT_RESULT, will
 * only include the following DNX Job attributes:
 * 
 *    GUID, State, Execution Start Time, Execution End Time, Result Code, Result Data
 * 
 * @file dnxTransport.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxTransport.h"
#include "dnxLogging.h"
//#include "dnxTcp.h"
//#include "dnxMsgQ.h"

typedef struct _urlTypeMap_ 
{
   char * name;         // Transport name: Currently only tcp, udp or msgq
   dnxChanType type;    // Transport type
} urlTypeMap;

static urlTypeMap gTypeMap[] = 
{
   { "tcp", DNX_CHAN_TCP }, 
   { "udp", DNX_CHAN_UDP }, 
   { "msgq", DNX_CHAN_MSGQ }, 
   { NULL, DNX_CHAN_UNKNOWN } 
};

static dnxChanMap gChannelMap[DNX_MAX_CHAN_MAP];

static int dnxInit = 0;

static pthread_mutex_t chanMutex;
static pthread_mutexattr_t chanMutexAttr;

extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind);

// External TCP Transport Functions
extern int dnxTcpInit (void);
extern int dnxTcpDeInit (void);
extern int dnxTcpNew (dnxChannel **channel, char *url);

// External UDP Transport Functions
extern int dnxUdpInit (void);
extern int dnxUdpDeInit (void);
extern int dnxUdpNew (dnxChannel **channel, char *url);

// External Message Queue Transport Functions
extern int dnxMsgQInit (void);
extern int dnxMsgQDeInit (void);
extern int dnxMsgQNew (dnxChannel **channel, char *url);

//----------------------------------------------------------------------------

int dnxChanMapInit (char *fileName)
{
   // Make sure we aren't already initialized
   if (dnxInit)
      return DNX_ERR_ALREADY;

   // Clear global error status variables
   dnxSetLastError(DNX_OK);

   // Clear global channel map
   memset(gChannelMap, 0, sizeof(gChannelMap));

   // Create protective mutex
   pthread_mutexattr_init(&chanMutexAttr);
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
   pthread_mutexattr_settype(&chanMutexAttr, PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
   pthread_mutex_init(&chanMutex, &chanMutexAttr);

   // Initialize lower layer transports
   dnxUdpInit();
   dnxTcpInit();
   dnxMsgQInit();

   // Set initialization flag
   dnxInit = 1;

   // TODO: Load global channel map from file, if specified

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxChanMapRelease (void)
{
   int i;

   // Make sure we aren't already de-initialized
   if (!dnxInit)
      return DNX_ERR_ALREADY;

   // Clean-up global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP; i++)
   {
      if (gChannelMap[i].name)
      {
         free(gChannelMap[i].name);
         if (gChannelMap[i].url)  free(gChannelMap[i].url);
      }
   }

   // Clear global channel map
   memset(gChannelMap, 0, sizeof(gChannelMap));

   // Clear global error status variables
   dnxSetLastError(DNX_OK);

   // Destroy the mutex
   if (pthread_mutex_destroy(&chanMutex) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxChanMapRelease: Unable to destroy mutex: mutex is in use!");
      //return DNX_ERR_BUSY;
   }
   pthread_mutexattr_destroy(&chanMutexAttr);

   // De-Initialize lower layer transports
   dnxUdpDeInit();
   dnxTcpDeInit();
   dnxMsgQDeInit();

   // Clear initialization flag
   dnxInit = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxChanMapAdd (char *name, char *url)
{
   dnxChanMap *chanMap;
   int ret;

   // Validate arguments
   if (!name || !*name || !url || !*url)
      return DNX_ERR_INVALID;

   // Acquire the lock on the global channel map
   if (pthread_mutex_lock(&chanMutex) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // See if this name already exists, otherwise grab an empty channel slot
   chanMap = NULL;
   if ((ret = dnxChanMapFindName(name, &chanMap)) != DNX_OK && (ret = dnxChanMapFindSlot(&chanMap)) != DNX_OK)
      goto abend;

   // Parse and validate the URL
   if ((ret = dnxChanMapUrlParse(chanMap, url)) != DNX_OK)
      goto abend;

   // Set the name, unless we are overriding and existing channel
   if (!(chanMap->name))
   {
      if ((chanMap->name = strdup(name)) == NULL)
      {
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: Out of Memory: chanMap->name");
         ret = DNX_ERR_MEMORY;
         goto abend;
      }
   }

   // Set the url
   if (chanMap->url) free(chanMap->url);  // Free any prior value
   if ((chanMap->url = strdup(url)) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxChanMapAdd: Out of Memory: chanMap->url");
      free(chanMap->name);
      ret = DNX_ERR_MEMORY;
   }

abend:
   // Clear this channel map slot
   if (ret != DNX_OK && chanMap)
      chanMap->type = DNX_CHAN_UNKNOWN;
   
   // Release the lock on the global channel map
   if (pthread_mutex_unlock(&chanMutex) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      ret = DNX_ERR_THREAD;
   }

   return ret;
}

//----------------------------------------------------------------------------
// Sets following ChanMap properties: type

int dnxChanMapUrlParse (dnxChanMap *chanMap, char *url)
{
   char tmpUrl[DNX_MAX_URL+1];
   urlTypeMap *utm;
   char *ep;

   // Validate parameters
   if (!chanMap || !url || !*url || strlen(url) > DNX_MAX_URL)
      return DNX_ERR_INVALID;

   // Make a working copy of the URL
   strcpy(tmpUrl, url);

   // Look for transport prefix: '[type]://'
   if ((ep = strchr(tmpUrl, ':')) == NULL || *(ep+1) != '/' || *(ep+2) != '/')
      return DNX_ERR_BADURL;
   *ep = '\0';

   // Decode destination based upon transport type
   chanMap->type = DNX_CHAN_UNKNOWN;
   for (utm = gTypeMap; utm->name; utm++)
   {
      if (!strcmp(tmpUrl, utm->name))
      {
         chanMap->type = utm->type;
         break;
      }
   }
   switch (chanMap->type)
   {
   case DNX_CHAN_TCP:   // Need hostname and port
      chanMap->txAlloc = dnxTcpNew;
      break;

   case DNX_CHAN_UDP:   // Need hostname and port
      chanMap->txAlloc = dnxUdpNew;
      break;

   case DNX_CHAN_MSGQ:  // Need message queue ID
      chanMap->txAlloc = dnxMsgQNew;
      break;

   default:
      chanMap->type = DNX_CHAN_UNKNOWN;
      return DNX_ERR_BADURL;
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxChanMapDelete (char *name)
{
   dnxChanMap *chanMap;
   int ret;

   // Validate arguments
   if (!name || !*name)
      return DNX_ERR_INVALID;

   // Acquire the lock on the global channel map
   if (pthread_mutex_lock(&chanMutex) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_lock: mutex has not been initialized");
         break;
      case EDEADLK:  // mutex already locked by this thread
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_lock: deadlock condition: mutex already locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_lock: unknown error %d: %s", errno, strerror(errno));
      }
      return DNX_ERR_THREAD;
   }

   // Verify that this channel resource exists
   if ((ret = dnxChanMapFindName(name, &chanMap)) == DNX_OK)
   {
      // Release allocated variables
      free(chanMap->name);
      if (chanMap->url)  free(chanMap->url);
      memset(chanMap, 0, sizeof(dnxChanMap));
      chanMap->type = DNX_CHAN_UNKNOWN;
   }

   // Release the lock on the global channel map
   if (pthread_mutex_unlock(&chanMutex) != 0)
   {
      switch (errno)
      {
      case EINVAL:   // mutex not initialized
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_unlock: mutex has not been initialized");
         break;
      case EPERM:    // mutex not locked by this thread
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_unlock: mutex not locked by this thread!");
         break;
      default:    // Unknown condition
         dnxSyslog(LOG_ERR, "dnxChanMapDelete: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
      }
      ret = DNX_ERR_THREAD;
   }

   return ret;
}

//----------------------------------------------------------------------------

int dnxChanMapFindSlot (dnxChanMap **chanMap)
{
   int i;

   // Validate parameters
   if (!chanMap)
      return DNX_ERR_INVALID;

   // See if we can find an empty slot in the global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP && gChannelMap[i].name; i++);

   *chanMap = (dnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : NULL);

   return ((*chanMap) ? DNX_OK : DNX_ERR_CAPACITY);
}

//----------------------------------------------------------------------------

int dnxChanMapFindName (char *name, dnxChanMap **chanMap)
{
   int i;

   // Validate arguments
   if (!name || !*name || !chanMap)
      return DNX_ERR_INVALID;

   // See if this name exists in the global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP; i++)
   {
      if (gChannelMap[i].name && !strcmp(name, gChannelMap[i].name))
         break;
   }

   *chanMap = (dnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : NULL);

   return ((*chanMap) ? DNX_OK : DNX_ERR_NOTFOUND);
}

//----------------------------------------------------------------------------

int dnxConnect (char *name, dnxChannel **channel, dnxChanMode mode)
{
   dnxChanMap *chanMap;
   int ret;

   // Validate arguments
   if (!name || !*name || !channel)
      return DNX_ERR_INVALID;

   *channel = NULL;

   // Verify that this channel resource exists
   if ((ret = dnxChanMapFindName(name, &chanMap)) == DNX_OK)
   {
      // Have the lower-layer transport allocate a channel
      if ((ret = chanMap->txAlloc(channel, chanMap->url)) == DNX_OK)
      {
         // Connect this channel
         if ((ret = (*channel)->dnxOpen(*channel, mode)) != DNX_OK)
         {
            (*channel)->txDelete(*channel);  // Have transport delete this channel
            *channel = NULL;
         }
      }
   }

   return ret;
}

//----------------------------------------------------------------------------

int dnxDisconnect (dnxChannel *channel)
{
   int ret;

   // Validate arguments
   if (!channel)
      return DNX_ERR_INVALID;

   // Disconnect this channel
   if ((ret = channel->dnxClose(channel)) == DNX_OK)
      channel->txDelete(channel);      // Have transport delete this channel

   return ret;
}

//----------------------------------------------------------------------------

int dnxGet (dnxChannel *channel, char *buf, int *size, int timeout, char *src)
{
   // Validate arguments
   if (!channel || !buf || !size || *size < 1)
      return DNX_ERR_INVALID;

   // Read a protocol packet
   return channel->dnxRead(channel, buf, size, timeout, src);
}

//----------------------------------------------------------------------------

int dnxPut (dnxChannel *channel, char *buf, int size, int timeout, char *dst)
{
   // Validate arguments
   if (!channel || !buf || size < 1)
      return DNX_ERR_INVALID;

   // Read a protocol packet
   return channel->dnxWrite(channel, buf, size, timeout, dst);
}

//----------------------------------------------------------------------------

int dnxChannelDebug (dnxChannel *channel, int doDebug)
{
   // Validate arguments
   if (!channel)
      return DNX_ERR_INVALID;
   
   channel->debug = doDebug;

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

