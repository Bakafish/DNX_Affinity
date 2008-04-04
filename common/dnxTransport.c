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
 *    2. dnxDisconnect (DnxChannel *channel)
 *    3. dnxGet (DnxChannel *channel, dnxRequest *req, dnxMsg **msg, int timeout)
 *    4. dnxPut (DnxChannel *channel, dnxRequest req, dnxMsg *msg, int timeout)
 *    5. dnxChannelDebug (DnxChannel *channel, int doDebug)
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
 *    1. GUID (XID) - Assigned at Job creation by the Scheduler
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
 *    GUID (XID), State, Execution Command, Execution Parameters and Execution Start Time.
 * 
 * Conversely, a DNX Job object as transmitted to a Collector, via a DNS_PUT_RESULT, will
 * only include the following DNX Job attributes:
 * 
 *    GUID (XID), State, Execution Start Time, Execution End Time, Result Code, Result Data
 * 
 * @file dnxTransport.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */
 
#include "dnxTransport.h"

#include "dnxDebug.h"
#include "dnxError.h"
#include "dnxLogging.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>


static int dnxInit = 0;
static pthread_mutex_t chanMutex;
static DnxChanMap gChannelMap[DNX_MAX_CHAN_MAP];

// External TCP Transport Functions
extern int dnxTcpInit(void);
extern int dnxTcpDeInit(void);
extern int dnxTcpNew(DnxChannel ** channel, char * url);

// External UDP Transport Functions
extern int dnxUdpInit(void);
extern int dnxUdpDeInit(void);
extern int dnxUdpNew(DnxChannel ** channel, char * url);

// External Message Queue Transport Functions
extern int dnxMsgQInit(void);
extern int dnxMsgQDeInit(void);
extern int dnxMsgQNew(DnxChannel ** channel, char * url);

//----------------------------------------------------------------------------

/** Initialize the channel map sub-system.
 * 
 * @param[in] fileName - a persistent storage file for the channel map. 
 *    Not currently used.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapInit(char * fileName)
{
   assert(!dnxInit);

   /** @todo This is the wrong place to initialize the global errno value. */

   // Clear global error status variables
   dnxSetLastError(DNX_OK);

   // Clear global channel map
   memset(gChannelMap, 0, sizeof(gChannelMap));

   DNX_PT_MUTEX_INIT(&chanMutex);

   /** @todo Check the return values for each individual protocol sub-system. */

   // Initialize lower layer transports
   dnxUdpInit();
   dnxTcpInit();
   dnxMsgQInit();

   /** @todo This should be a debug-only flag. */

   // Set initialization flag
   dnxInit = 1;

   /** @todo Load global channel map from file, if specified. */

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up resources allocated by the channel map sub-system.
 * 
 * @return Always returns zero.
 */
int dnxChanMapRelease(void)
{
   int i;

   /** @todo Objectize the channel map. */

// assert(dnxInit);
   if (!dnxInit)
      return 0;

   // Clean-up global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP; i++)
   {
      if (gChannelMap[i].name)
      {
         xfree(gChannelMap[i].name);
         if (gChannelMap[i].url) xfree(gChannelMap[i].url);
      }
   }

   // Clear global channel map
   memset(gChannelMap, 0, sizeof(gChannelMap));

   // Clear global error status variables
   dnxSetLastError(DNX_OK);

   // Destroy the mutex
   DNX_PT_MUTEX_DESTROY(&chanMutex);

   // De-Initialize lower layer transports
   dnxUdpDeInit();
   dnxTcpDeInit();
   dnxMsgQDeInit();

   // Clear initialization flag
   dnxInit = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Add a new channel to the global channel map.
 * 
 * @param[in] name - the name of the new channel to be added.
 * @param[in] url - the URL to associate with this new channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapAdd(char * name, char * url)
{
   DnxChanMap * chanMap;
   int ret;

   // Validate arguments
   if (!name || !*name || !url || !*url)
      return DNX_ERR_INVALID;

   // Acquire the lock on the global channel map
   DNX_PT_MUTEX_LOCK(&chanMutex);

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
      if ((chanMap->name = xstrdup(name)) == NULL)
      {
         dnxSyslog(LOG_ERR, "dnxChanMapAdd: Out of Memory: chanMap->name");
         ret = DNX_ERR_MEMORY;
         goto abend;
      }
   }

   // Set the url
   if (chanMap->url) xfree(chanMap->url);  // Free any prior value
   if ((chanMap->url = xstrdup(url)) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxChanMapAdd: Out of Memory: chanMap->url");
      xfree(chanMap->name);
      ret = DNX_ERR_MEMORY;
   }

abend:;

   // Clear this channel map slot
   if (ret != DNX_OK && chanMap)
      chanMap->type = DNX_CHAN_UNKNOWN;
   
   // Release the lock on the global channel map
   DNX_PT_MUTEX_UNLOCK(&chanMutex);

   return ret;
}

//----------------------------------------------------------------------------

/** Parse the URL scheme (type) into a specified channel map.
 * 
 * Set the 'type' channel map property.
 * 
 * @param[in] chanMap - the channel map on which to set the URL type property.
 * @param[in] url - the URL to be parsed into @p chanMap.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapUrlParse(DnxChanMap * chanMap, char * url)
{
   static struct urlTypeMap { char * name; DnxChanType type; } typeMap[] = 
   {
      { "tcp", DNX_CHAN_TCP }, 
      { "udp", DNX_CHAN_UDP }, 
      { "msgq", DNX_CHAN_MSGQ }, 
      { NULL, DNX_CHAN_UNKNOWN } 
   };
   
   char tmpUrl[DNX_MAX_URL + 1];
   struct urlTypeMap * utm;
   char * ep;

   assert(chanMap && url);

   // Validate parameters
   if (!*url || strlen(url) > DNX_MAX_URL)
      return DNX_ERR_INVALID;

   // Make a working copy of the URL
   strcpy(tmpUrl, url);

   // Look for transport prefix: '[type]://'
   if ((ep = strchr(tmpUrl, ':')) == NULL || *(ep+1) != '/' || *(ep+2) != '/')
      return DNX_ERR_BADURL;
   *ep = 0;

   // Decode destination based upon transport type
   chanMap->type = DNX_CHAN_UNKNOWN;
   for (utm = typeMap; utm->name; utm++)
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

/** Delete a channel map by name.
 * 
 * @param[in] name - the name of the channel map to be deleted.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapDelete(char * name)
{
   DnxChanMap * chanMap;
   int ret;

   // Validate arguments
   assert(name && *name);
   if (!name || !*name)
      return DNX_ERR_INVALID;

   DNX_PT_MUTEX_LOCK(&chanMutex);

   // Verify that this channel resource exists
   if ((ret = dnxChanMapFindName(name, &chanMap)) == DNX_OK)
   {
      // Release allocated variables
      xfree(chanMap->name);
      if (chanMap->url)  xfree(chanMap->url);
      memset(chanMap, 0, sizeof(DnxChanMap));
      chanMap->type = DNX_CHAN_UNKNOWN;
   }

   DNX_PT_MUTEX_UNLOCK(&chanMutex);

   return ret;
}

//----------------------------------------------------------------------------

/** Locate a free channel map slot in the global channel map table.
 * 
 * @param[out] chanMap - the address of storage in which to return the
 *    free channel map slot (address).
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapFindSlot(DnxChanMap ** chanMap)
{
   int i;

   assert(chanMap);

   // See if we can find an empty slot in the global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP && gChannelMap[i].name; i++)
      ;

   *chanMap = (DnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : NULL);

   return (*chanMap) ? DNX_OK : DNX_ERR_CAPACITY;
}

//----------------------------------------------------------------------------

/** Locate a channel map by name.
 * 
 * @param[in] name - the name of the channel map to locate and return.
 * @param[out] chanMap - the address of storage for returning the located
 *    channel map.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapFindName(char * name, DnxChanMap ** chanMap)
{
   int i;

   // Validate arguments
   if (!name || !*name || !chanMap)
      return DNX_ERR_INVALID;

   // See if this name exists in the global channel map
   for (i=0; i < DNX_MAX_CHAN_MAP; i++)
      if (gChannelMap[i].name && !strcmp(name, gChannelMap[i].name))
         break;

   *chanMap = (DnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : NULL);

   return (*chanMap) ? DNX_OK : DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

/** Connect a specified channel.
 * 
 * The @p mode parameter specifies either active (1) or passive (0) for the 
 * connection mode. An active connection is basically a client connection, or
 * a connection on which we send data to a server. A passive connection is 
 * basically a server connection, or a connection on which we listen for 
 * incoming messages.
 * 
 * @param[in] name - the name of the channel to open.
 * @param[out] channel - the address of storage for the returned connected 
 *    channel.
 * @param[in] mode - the connection mode for the newly connected channel.
 *    This mode value may be either active (1) or passive (0). 
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxConnect(char * name, DnxChannel ** channel, DnxChanMode mode)
{
   DnxChanMap * chanMap;
   int ret;

   // Validate arguments
   assert(name && *name && channel);
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

/** Disconnect a specified channel.
 * 
 * @param[in] channel - the channel to be disconnected.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxDisconnect(DnxChannel * channel)
{
   int ret;

   assert(channel);

   // close and delete the channel.
   if ((ret = channel->dnxClose(channel)) == DNX_OK)
      channel->txDelete(channel);

   return ret;
}

//----------------------------------------------------------------------------

/** Read data from an open channel.
 * 
 * @param[in] channel - the channel from which to read.
 * @param[out] buf - the address of storage for data read from @p channel.
 * @param[in,out] size - on entry, the maximum number of bytes that may be 
 *    read into @p buf; on exit, returns the number of bytes actually read.
 * @param[in] timeout - the maximum number of seconds the caller is willing
 *    to wait for data on @p channel before returning a timeout error.
 * @param[out] src - the address of storage for the remote sender's address.
 *    This parameter is optional, and may be passed as NULL. The caller must
 *    ensure that the buffer pointed to is large enough to hold the returned
 *    address. Passing the address of a struct sockaddr_storage is more than 
 *    adequate for any address type available.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxGet(DnxChannel * channel, char * buf, int * size, 
      int timeout, char * src)
{
   assert(channel && buf && size && *size > 0);
   return channel->dnxRead(channel, buf, size, timeout, src);
}

//----------------------------------------------------------------------------

/** Write data to an open channel.
 * 
 * @param[in] channel - the channel to which data should be written.
 * @param[in] buf - the data to be written to @p channel.
 * @param[in] size - the number of bytes in @p buf to be written.
 * @param[in] timeout - the maximum number of seconds the caller is willing
 *    to wait for the write operation to complete on @p channel before 
 *    returning a timeout error.
 * @param[in] dst - the address to which data should be sent. This parameter 
 *    is optional, and may be passed as NULL. If NULL, the channel configured
 *    address will be used.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxPut(DnxChannel * channel, char * buf, int size, int timeout, char * dst)
{
   assert(channel && buf && size > 0);
   return channel->dnxWrite(channel, buf, size, timeout, dst);
}

//----------------------------------------------------------------------------

/** Debug a channel.
 * 
 * @param[in] channel - the channel to be debugged.
 * @param[in] doDebug - boolean: 1 for enable debug, 0 for disable debug.
 * 
 * @return Always returns zero.
 */
int dnxChannelDebug(DnxChannel * channel, int doDebug)
{
   assert(channel);
  
   channel->debug = doDebug;

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

