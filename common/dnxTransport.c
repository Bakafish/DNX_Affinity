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
 * @file dnxTransport.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */
 
#include "dnxTransport.h"
#include "dnxTSPI.h"

#include "dnxDebug.h"
#include "dnxError.h"
#include "dnxLogging.h"
#include "dnxProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

/** The maximum number of simultaneous open channels. */
#define DNX_MAX_CHAN_MAP   1000

/** The channel map object structure. */
typedef struct DnxChanMap_ 
{
   char * name;         //!< Channel name.
   char * url;          //!< Channel connection specification.
   int (*txAlloc)(char * url, iDnxChannel ** icpp);  //!< Channel factory.
} DnxChanMap;

/** A low-level transport module. */
typedef struct DnxTransport
{
   char * scheme;
   char * libpath;
   int (*txAlloc)(char * url, iDnxChannel ** icpp);
   int (*txInit)(int (**ptxAlloc)(char * url, iDnxChannel ** icpp));
   void (*txExit)(void);
} DnxTransport;

// ---------------------------------------------------------------------------
// This structure is temporary till we start loading from disk, at which point
// URL schemes may then be added at runtime via the configuration file, eg.,
// transport = dnx+transport : /path/to/transport/service/provider/library
 
#include "dnxTcp.h"
#include "dnxUdp.h"
#include "dnxMsgQ.h"

static DnxTransport gTMList[] = 
{
   { "tcp",  0, 0, dnxTcpInit,  dnxTcpDeInit  },
   { "udp",  0, 0, dnxUdpInit,  dnxUdpDeInit  },
   { "msgq", 0, 0, dnxMsgQInit, dnxMsgQDeInit },
};

// ---------------------------------------------------------------------------

static int dnxInit = 0;             //!< The channel map initialization  flag.
static pthread_mutex_t chanMutex;   //!< The channel map mutex.
static DnxChanMap gChannelMap[DNX_MAX_CHAN_MAP]; //!< The global channel map.

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/
char *ntop(const struct sockaddr *sa)
{
    size_t maxlen;
    char * buf = (char *)xcalloc(DNX_MAX_ADDRESS,sizeof(char));
    
    assert(buf);
    
    assert(sa);
    
    if(!sa) {
//        strncpy(buf, "0.0.0.0", maxlen);
//        return xstrdup("DNX Error:  Address Unkown or Corrupt! ");
        xfree(buf);
        return NULL;
    }

    switch(sa->sa_family) {
        case AF_INET:
            maxlen = INET_ADDRSTRLEN;
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),buf, maxlen);
            break;

        case AF_INET6:
            maxlen = INET6_ADDRSTRLEN;
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),buf, maxlen);
            break;

        default:
//            strncpy(buf, "Unknown AF", maxlen);
            xfree(buf);
            return NULL;
    }

    return buf;
}

/** Set a channel map's transport object allocator in a based on URL scheme.
 * 
 * @param[in] chanMap - the channel map on which to set the allocator.
 * @param[in] url - the URL to be parsed.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxChanMapUrlParse(DnxChanMap * chanMap, char * url)
{
   char * ep;
   int i;

   assert(chanMap && url && strlen(url) <= DNX_MAX_URL);

   // locate end of scheme
   if ((ep = strstr(url, "://")) == 0)
      return DNX_ERR_BADURL;

   // set allocator for this transport based on the scheme string
   for (i = 0; i < elemcount(gTMList); i++)
      if (!strncmp(url, gTMList[i].scheme, ep - url))
      {
         chanMap->txAlloc = gTMList[i].txAlloc;
         break;
      }

   return i < elemcount(gTMList) ? DNX_OK : DNX_ERR_BADURL;
}

//----------------------------------------------------------------------------

/** Locate a free channel map slot in the global channel map table.
 * 
 * @param[out] chanMap - the address of storage in which to return the
 *    free channel map slot (address).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxChanMapFindSlot(DnxChanMap ** chanMap)
{
   int i;

   assert(chanMap);

   // see if we can find an empty slot in the global channel map
   for (i = 0; i < DNX_MAX_CHAN_MAP && gChannelMap[i].name; i++)
      ;

   *chanMap = (DnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : 0);

   return *chanMap ? DNX_OK : DNX_ERR_CAPACITY;
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
static int dnxChanMapFindName(char * name, DnxChanMap ** chanMap)
{
   int i;

   assert(name && *name && chanMap);

   // see if this name exists in the global channel map
   for (i = 0; i < DNX_MAX_CHAN_MAP; i++)
      if (gChannelMap[i].name && !strcmp(name, gChannelMap[i].name))
         break;

   *chanMap = (DnxChanMap *)((i < DNX_MAX_CHAN_MAP) ? &gChannelMap[i] : 0);

   return *chanMap ? DNX_OK : DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

/** Allocate an unconnected channel.
 * 
 * @param[in] name - the name of the channel to allocate.
 * @param[out] icpp - the address of storage for the returned object 
 *    reference.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxChanMapAllocChannel(char * name, iDnxChannel ** icpp)
{
   DnxChanMap * chanMap;
   int ret;

   assert(name && *name && icpp);

   DNX_PT_MUTEX_LOCK(&chanMutex);

   if ((ret = dnxChanMapFindName(name, &chanMap)) == DNX_OK)
      ret = chanMap->txAlloc(chanMap->url, icpp);

   DNX_PT_MUTEX_UNLOCK(&chanMutex);

   return ret;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

/** Add a new channel to the global channel map.
 * 
 * @param[in] name - the name of the new channel to be added.
 * @param[in] url - the URL to associate with this new channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxChanMapAdd(char * name, char * url)
{
   DnxChanMap tmp, * chanMap;
   int ret;

   assert(name && *name && url && strlen(url) < DNX_MAX_URL);

   // parse and validate the URL
   if ((ret = dnxChanMapUrlParse(&tmp, url)) != DNX_OK)
      return ret;

   // set the name, unless we are overriding an existing channel
   if ((tmp.name = xstrdup(name)) == 0 || (tmp.url = xstrdup(url)) == 0)
   {
      xfree(tmp.name);
      return DNX_ERR_MEMORY;
   }

   DNX_PT_MUTEX_LOCK(&chanMutex);

   // see if this name already exists, otherwise grab an empty channel slot
   if ((ret = dnxChanMapFindName(name, &chanMap)) == DNX_OK 
         || (ret = dnxChanMapFindSlot(&chanMap)) == DNX_OK)
   {
      xfree(chanMap->name);
      xfree(chanMap->url);
      memcpy(chanMap, &tmp, sizeof *chanMap);
   }
   
   DNX_PT_MUTEX_UNLOCK(&chanMutex);

   // on error, release previously allocated memory
   if (ret != DNX_OK)
   {
      xfree(tmp.name);
      xfree(tmp.url);
   }
   return ret;
}

//----------------------------------------------------------------------------

/** Delete a channel map by name.
 * 
 * @param[in] name - the name of the channel map to be deleted.
 */
void dnxChanMapDelete(char * name)
{
   DnxChanMap * chanMap;

   assert(name && *name);

   DNX_PT_MUTEX_LOCK(&chanMutex);

   // locate resource by name
   if (dnxChanMapFindName(name, &chanMap) == DNX_OK)
   {
      // release allocated variables, clear object
      xfree(chanMap->name);
      xfree(chanMap->url);
      memset(chanMap, 0, sizeof *chanMap);
   }

   DNX_PT_MUTEX_UNLOCK(&chanMutex);
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
 * @param[in] active - boolean; true (1) means this will be an active (client-
 *    side) connection to a server; false (0) means this will be a passive
 *    (server-side) listen point.
 * @param[out] channel - the address of storage for the returned connected 
 *    channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxConnect(char * name, int active, DnxChannel ** channel)
{
   DnxChanMap * chanMap;
   iDnxChannel * icp;
   int ret;

   assert(name && *name && channel);

   // locate channel resource by name, return a new unopened channel
   if ((ret = dnxChanMapAllocChannel(name, &icp)) == DNX_OK)
   {
      if ((ret = icp->txOpen(icp, active)) != DNX_OK)
         icp->txDelete(icp);
   }
   if (ret == DNX_OK)
      *channel = (DnxChannel *)icp;
   return ret;
}

//----------------------------------------------------------------------------

/** Disconnect and delete a specified channel.
 * 
 * @param[in] channel - the channel to be disconnected (and deleted).
 */
void dnxDisconnect(DnxChannel * channel)
{
   iDnxChannel * icp = (iDnxChannel *)channel;

   assert(channel);

   if (icp->txClose(icp) == DNX_OK)
      icp->txDelete(icp);
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
int dnxGet(DnxChannel * channel, char * buf, int * size,int timeout, char * src)
{
   iDnxChannel * icp = (iDnxChannel *)channel;
   assert(channel && buf && size && *size > 0);
   return icp->txRead(icp, buf, size, timeout, src);
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
   iDnxChannel * icp = (iDnxChannel *)channel;
   assert(channel && buf && size > 0 && size < DNX_MAX_MSG);
   return icp->txWrite(icp, buf, size, timeout, dst);
}

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
   int i;

   assert(!dnxInit);

   memset(gChannelMap, 0, sizeof gChannelMap);

   DNX_PT_MUTEX_INIT(&chanMutex);

   // initialize transport module table
   for (i = 0; i < elemcount(gTMList); i++)
   {
      int ret;
      if ((ret = gTMList[i].txInit(&gTMList[i].txAlloc)) != DNX_OK)
      {
         while (i--) gTMList[i].txExit();
         DNX_PT_MUTEX_DESTROY(&chanMutex);
         return ret;
      }
   }

   /** @todo Load global channel map from file, if specified. */

   dnxInit = 1;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up resources allocated by the channel map sub-system.
 */
void dnxChanMapRelease(void)
{
   if (dnxInit)
   {
      int i;

      DNX_PT_MUTEX_LOCK(&chanMutex);

      for (i = 0; i < DNX_MAX_CHAN_MAP; i++)
      {
         xfree(gChannelMap[i].name);
         xfree(gChannelMap[i].url);
      }
   
      memset(gChannelMap, 0, sizeof gChannelMap);
   
      DNX_PT_MUTEX_UNLOCK(&chanMutex);
      DNX_PT_MUTEX_DESTROY(&chanMutex);
   
      // de-initialize transport module table
      i = elemcount(gTMList);
      while (i--) gTMList[i].txExit();
   
      dnxInit = 0;
   }
}

/*--------------------------------------------------------------------------*/

