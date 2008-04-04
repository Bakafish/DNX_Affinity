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

/** Implements the DNX UDP Tranport Layer.
 *
 * @file dnxUdp.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxUdp.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>

static pthread_mutex_t udpMutex;

/** @todo Use GNU reentrant resolver extensions on platforms where available. */

//----------------------------------------------------------------------------

/** Initialize the UDP channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxUdpInit(void)
{
   // create protective mutex for non-reentrant functions (gethostbyname)
   DNX_PT_MUTEX_INIT(&udpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the UDP channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxUdpDeInit(void)
{
   // destroy the mutex
   DNX_PT_MUTEX_DESTROY(&udpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Create a new UDP channel.
 * 
 * @param[out] channel - the address of storage for returning the new channel.
 * @param[in] url - the URL bind address to associate with the new channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxUdpNew(DnxChannel ** channel, char * url)
{
   char tmpUrl[DNX_MAX_URL + 1];
   char * cp, * ep, * lastchar;
   long port;

   assert(channel && url && *url && strlen(url) <= DNX_MAX_URL);

   *channel = 0;

   // make a working copy of the URL
   strcpy(tmpUrl, url);

   // look for transport prefix: '[type]://'
   if ((ep = strchr(tmpUrl, ':')) == NULL || *(ep+1) != '/' || *(ep+2) != '/')
      return DNX_ERR_BADURL;
   *ep = '\0';
   cp = ep + 3;   // Set to beginning of destination portion of the URL

   // search for hostname - port separator
   if ((ep = strchr(cp, ':')) == NULL || ep == cp)
      return DNX_ERR_BADURL;  // No separator found or empty hostname
   *ep++ = '\0';

   // get the port number
   errno = 0;
   if ((port = strtol(ep, &lastchar, 0)) < 1 || port > 65535 || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // allocate a new channel structure
   if ((*channel = (DnxChannel *)xmalloc(sizeof(DnxChannel))) == NULL)
      return DNX_ERR_MEMORY;  // Memory allocation error

   memset(*channel, 0, sizeof(DnxChannel));

   // save host name and port
   (*channel)->type = DNX_CHAN_UDP;
   (*channel)->name = NULL;
   if (((*channel)->host = xstrdup(cp)) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxUdpNew: Out of Memory: strdup(channel->host)");
      xfree(*channel);
      *channel = NULL;
      return DNX_ERR_MEMORY;  // Memory allocation error
   }
   (*channel)->port = (int)port;
   (*channel)->state = DNX_CHAN_CLOSED;

   // set I/O methods
   (*channel)->dnxOpen  = dnxUdpOpen;
   (*channel)->dnxClose = dnxUdpClose;
   (*channel)->dnxRead  = dnxUdpRead;
   (*channel)->dnxWrite = dnxUdpWrite;
   (*channel)->txDelete = dnxUdpDelete;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a UDP channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxUdpDelete(DnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_UDP);

   // make sure this channel is closed
   if (channel->state == DNX_CHAN_OPEN)
      dnxUdpClose(channel);

   // release host name string
   if (channel->host) xfree(channel->host);

   // release channel memory
   memset(channel, 0, sizeof(DnxChannel));
   xfree(channel);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Open a UDP channel.
 * 
 * Possible modes are active (1) or passive (0). 
 * 
 * @param[in] channel - the channel to be opened.
 * @param[in] mode - the mode in which @p channel should be opened.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxUdpOpen(DnxChannel * channel, DnxChanMode mode)   // 0=Passive, 1=Active
{
   struct hostent * he;
   struct sockaddr_in inaddr;
   int sd;

   assert(channel && channel->type == DNX_CHAN_UDP && channel->port > 0);

   // make sure this channel isn't already open
   if (channel->state != DNX_CHAN_CLOSED)
      return DNX_ERR_ALREADY;

   // setup the socket address structure
   inaddr.sin_family = AF_INET;
   inaddr.sin_port = (in_port_t)channel->port;
   inaddr.sin_port = htons(inaddr.sin_port);

   // see if we are listening on any address
   if (!strcmp(channel->host, "INADDR_ANY") || !strcmp(channel->host, "0.0.0.0") || !strcmp(channel->host, "0"))
   {
      // make sure that the request is passive
      if (mode != DNX_CHAN_PASSIVE)
         return DNX_ERR_ADDRESS;

      inaddr.sin_addr.s_addr = INADDR_ANY;
   }
   else  // resolve destination address
   {
      // acquire the lock
      DNX_PT_MUTEX_LOCK(&udpMutex);

      // try to resolve this address
      if ((he = gethostbyname(channel->host)) == NULL)
      {
         DNX_PT_MUTEX_UNLOCK(&udpMutex);
         return DNX_ERR_ADDRESS;
      }
      memcpy(&(inaddr.sin_addr.s_addr), he->h_addr_list[0], he->h_length);

      // release the lock
      DNX_PT_MUTEX_UNLOCK(&udpMutex);
   }

   // create a socket
   if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
   {
      dnxSyslog(LOG_ERR, "dnxUdpOpen: socket failure; failed with %d: %s", 
            errno, strerror(errno));
      return DNX_ERR_OPEN;
   }

   // determine how to handle socket connectivity based upon mode
   if (mode == DNX_CHAN_ACTIVE)
   {
      // for UDP, this sets the default destination address, so we can
      // now use send() and write() in addition to sendto() and sendmsg()
      if (connect(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         dnxSyslog(LOG_ERR, 
               "dnxUdpOpen: connect(%lx) failure; failed with %d: %s", 
               (unsigned long)inaddr.sin_addr.s_addr, errno, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }
   else  // DNX_CHAN_PASSIVE
   {
      // want to listen for incoming packets, so bind to the
      // specified local address and port
      if (bind(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         dnxSyslog(LOG_ERR, "dnxUdpOpen: bind(%lx) failure; failed with %d: %s", 
               (unsigned long)inaddr.sin_addr.s_addr, errno, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }

   // mark the channel as open
   channel->chan  = sd;
   channel->state = DNX_CHAN_OPEN;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a UDP channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxUdpClose(DnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_UDP);

   // make sure this channel isn't already closed
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_ALREADY;

   // shutdown the communication paths on the socket
   // shutdown(channel->chan, SHUT_RDWR);

   // close the socket
   close(channel->chan);

   // mark the channel as closed
   channel->state = DNX_CHAN_CLOSED;
   channel->chan  = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a UDP channel.
 * 
 * @param[in] channel - the channel from which to read data.
 * @param[out] buf - the address of storage into which data should be read.
 * @param[in,out] size - on entry, the maximum number of bytes that may be 
 *    read into @p buf; on exit, returns the number of bytes stored in @p buf.
 * @param[in] timeout - the maximum number of seconds we're willing to wait
 *    for data to become available on @p channel without returning a timeout
 *    error.
 * @param[out] src - the address of storage for the sender's address if 
 *    desired. This parameter is optional, and may be passed as NULL.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxUdpRead(DnxChannel * channel, char * buf, int * size, 
      int timeout, char * src)
{
   struct sockaddr_in src_addr;
   socklen_t slen;
   int mlen, nsd;
   fd_set fd_rds;
   struct timeval tv;

   assert(channel && channel->type == DNX_CHAN_UDP && buf && size && *size > 0);

   // make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // implement timeout logic, if timeout value is > 0
   if (timeout > 0)
   {
      FD_ZERO(&fd_rds);
      FD_SET(channel->chan, &fd_rds);
      tv.tv_sec = (long)timeout;
      tv.tv_usec = 0L;
      if ((nsd = select((channel->chan+1), &fd_rds, NULL, NULL, &tv)) == 0)
         return DNX_ERR_TIMEOUT;
      else if (nsd < 0)
      {
         // treat signal interrupts like timeouts
         if (errno == EINTR) return DNX_ERR_TIMEOUT;
         dnxSyslog(LOG_ERR, "dnxUdpRead: select failure; failed with %d: %s", 
               errno, strerror(errno));
         return DNX_ERR_RECEIVE;
      }
   }

   // read the incoming message
   slen = sizeof(src_addr);
   if ((mlen = recvfrom(channel->chan, buf, *size, 0, (struct sockaddr *)&src_addr, &slen)) < 0)
      return DNX_ERR_RECEIVE;

   // validate the message length
   if (mlen < 1 || mlen > DNX_MAX_MSG)
      return DNX_ERR_RECEIVE;

   // update actual packet data size
   *size = (int)mlen;

   // set source IP/port information, if desired
   if (src)
      memcpy(src, &src_addr, sizeof(src_addr));

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a UDP channel.
 * 
 * @param[in] channel - the channel on which to write data from @p buf.
 * @param[in] buf - a pointer to the data to be written.
 * @param[in] size - the number of bytes to be written on @p channel.
 * @param[in] timeout - the maximum number of seconds to wait for the write
 *    operation to complete without returning a timeout error.
 * @param[in] dst - the address to which the data in @p buf should be sent
 *    using this channel. This parameter is ignored for virtual connection
 *    based channels. This parameter is optional and may be passed as NULL. 
 *
 * @return Zero on success, or a non-zero error value.
 * 
 * @note If this is a stream oriented channel, or if NULL is passed for 
 * the @p dst parameter, The channel destination address is used.
 */
int dnxUdpWrite(DnxChannel * channel, char * buf, int size, 
      int timeout, char * dst)
{
   struct sockaddr_in dst_addr;
   fd_set fd_wrs;
   struct timeval tv;
   int ret, nsd;

   assert(channel && channel->type == DNX_CHAN_UDP && buf);

   // validate that the message size is within bounds
   if (size < 1 || size > DNX_MAX_MSG)
      return DNX_ERR_SIZE;

   // make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // implement timeout logic, if timeout value is > 0
   if (timeout > 0)
   {
      FD_ZERO(&fd_wrs);
      FD_SET(channel->chan, &fd_wrs);
      tv.tv_sec = (long)timeout;
      tv.tv_usec = 0L;
      if ((nsd = select((channel->chan+1), NULL, &fd_wrs, NULL, &tv)) == 0)
         return DNX_ERR_TIMEOUT;
      else if (nsd < 0)
      {
         // treat signal interrupts like timeouts
         if (errno == EINTR) return DNX_ERR_TIMEOUT;
         dnxSyslog(LOG_ERR, "dnxUdpWrite: select failure; failed with %d: %s", 
               errno, strerror(errno));
         return DNX_ERR_SEND;
      }
   }

   // check for a destination override
   if (dst)
   {
      memcpy(&dst_addr, dst, sizeof(dst_addr));
      // send the message to the specified address
      ret = sendto(channel->chan, buf, size, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
   }
   else  // send the message to the previously set (via connect(2)) channel address
      ret = write(channel->chan, buf, size);

   // check for write error
   if (ret != size)
      return DNX_ERR_SEND;
   
   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

