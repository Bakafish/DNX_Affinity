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

/** Implements the TCP Tranport Layer.
 *
 * @file dnxTcp.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxTcp.h"

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

static pthread_mutex_t tcpMutex;

/** @todo Use GNU reentrant resolver interface on platforms where available. */

//----------------------------------------------------------------------------

/** Initialize the TCP channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxTcpInit(void)
{
   // Create protective mutex for non-reentrant functions (gethostbyname)
   DNX_PT_MUTEX_INIT(&tcpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the TCP channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxTcpDeInit(void)
{
   // Destroy the mutex
   DNX_PT_MUTEX_DESTROY(&tcpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Create a new TCP channel.
 * 
 * @param[out] channel - the address of storage for returning the new channel.
 * @param[in] url - the URL bind address to associate with the new channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxTcpNew(dnxChannel ** channel, char * url)
{
   char tmpUrl[DNX_MAX_URL + 1];
   char * cp, * ep, * lastchar;
   long port;

   // Validate parameters
   if (!channel || !url || !*url || strlen(url) > DNX_MAX_URL)
      return DNX_ERR_INVALID;

   *channel = NULL;

   // Make a working copy of the URL
   strcpy(tmpUrl, url);

   // Look for transport prefix: '[type]://'
   if ((ep = strchr(tmpUrl, ':')) == NULL || *(ep+1) != '/' || *(ep+2) != '/')
      return DNX_ERR_BADURL;
   *ep = '\0';
   cp = ep + 3;   // Set to beginning of destination portion of the URL

   // Search for hostname - port separator
   if ((ep = strchr(cp, ':')) == NULL || ep == cp)
      return DNX_ERR_BADURL;  // No separator found or empty hostname
   *ep++ = '\0';

   // Get the port number
   errno = 0;
   if ((port = strtol(ep, &lastchar, 0)) < 1 || port > 65535 
         || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // Allocate a new channel structure
   if ((*channel = (dnxChannel *)xmalloc(sizeof(dnxChannel))) == NULL)
      return DNX_ERR_MEMORY;  // Memory allocation error

   memset(*channel, 0, sizeof(dnxChannel));

   // Save host name and port
   (*channel)->type = DNX_CHAN_TCP;
   (*channel)->name = NULL;
   if (((*channel)->host = xstrdup(cp)) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxTcpNew: Out of Memory: strdup(channel->host)");
      xfree(*channel);
      *channel = NULL;
      return DNX_ERR_MEMORY;  // Memory allocation error
   }
   (*channel)->port = (int)port;
   (*channel)->state = DNX_CHAN_CLOSED;

   // Set I/O methods
   (*channel)->dnxOpen  = dnxTcpOpen;
   (*channel)->dnxClose = dnxTcpClose;
   (*channel)->dnxRead  = dnxTcpRead;
   (*channel)->dnxWrite = dnxTcpWrite;
   (*channel)->txDelete = dnxTcpDelete;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a TCP channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxTcpDelete(dnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_TCP);

   // Make sure this channel is closed
   if (channel->state == DNX_CHAN_OPEN)
      dnxTcpClose(channel);

   // Release host name string
   if (channel->host) xfree(channel->host);

   // Release channel memory
   memset(channel, 0, sizeof(dnxChannel));
   xfree(channel);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Open a TCP channel.
 * 
 * Possible modes are active (1) or passive (0). 
 * 
 * @param[in] channel - the channel to be opened.
 * @param[in] mode - the mode in which @p channel should be opened.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxTcpOpen(dnxChannel * channel, dnxChanMode mode)   // 0=Passive, 1=Active
{
   struct hostent * he;
   struct sockaddr_in inaddr;
   int sd;

   assert(channel && channel->type == DNX_CHAN_TCP && channel->port > 0);

   // Make sure this channel isn't already open
   if (channel->state != DNX_CHAN_CLOSED)
      return DNX_ERR_ALREADY;

   // Setup the socket address structure
   inaddr.sin_family = AF_INET;
   inaddr.sin_port = (in_port_t)channel->port;
   inaddr.sin_port = htons(inaddr.sin_port);

   // See if we are listening on any address
   if (!strcmp(channel->host, "INADDR_ANY") 
         || !strcmp(channel->host, "0.0.0.0") 
         || !strcmp(channel->host, "0"))
   {
      // Make sure that the request is passive
      if (mode != DNX_CHAN_PASSIVE)
         return DNX_ERR_ADDRESS;

      inaddr.sin_addr.s_addr = INADDR_ANY;
   }
   else  // Resolve destination address
   {
      DNX_PT_MUTEX_LOCK(&tcpMutex);

      // Try to resolve this address
      if ((he = gethostbyname(channel->host)) == NULL)
      {
         DNX_PT_MUTEX_UNLOCK(&tcpMutex);
         return DNX_ERR_ADDRESS;
      }
      memcpy(&(inaddr.sin_addr.s_addr), he->h_addr_list[0], he->h_length);

      DNX_PT_MUTEX_UNLOCK(&tcpMutex);
   }

   // Create a socket
   if ((sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      return DNX_ERR_OPEN;

   // Determing whether we are connecting or listening
   if (mode == DNX_CHAN_ACTIVE)
   {
      // Attempt to open the socket connect
      if (connect(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         return DNX_ERR_OPEN;
      }
   }
   else  // DNX_CHAN_PASSIVE
   {
      // Bind the socket to a local address and port
      if (bind(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         return DNX_ERR_OPEN;
      }

      // Set the listen depth
      listen(sd, DNX_TCP_LISTEN);
   }

   // Mark the channel as open
   channel->chan  = sd;
   channel->state = DNX_CHAN_OPEN;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a TCP channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxTcpClose(dnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_TCP);

   // Make sure this channel isn't already closed
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_ALREADY;

   // Shutdown the communication paths on the socket
   shutdown(channel->chan, SHUT_RDWR);

   // Close the socket
   close(channel->chan);

   // Mark the channel as closed
   channel->state = DNX_CHAN_CLOSED;
   channel->chan  = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a TCP channel.
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
int dnxTcpRead(dnxChannel * channel, char * buf, int * size, 
      int timeout, char * src)
{
   struct sockaddr_in src_addr;
   socklen_t slen;
   char mbuf[DNX_MAX_MSG];
   unsigned short mlen;
   fd_set fd_rds;
   struct timeval tv;
   int nsd;

   assert(channel && channel->type == DNX_CHAN_TCP && buf && size && *size > 0);

   // Make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // Implement timeout logic, if timeout value is > 0
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
         dnxSyslog(LOG_ERR, "dnxTcpRead: select failure: %s", strerror(errno));
         return DNX_ERR_RECEIVE;
      }
   }

   // First, read the incoming message length
   if (read(channel->chan, &mlen, sizeof(mlen)) != sizeof(mlen))
      return DNX_ERR_RECEIVE;
   mlen = ntohs(mlen);

   // Validate the message length
   if (mlen < 1 || mlen > DNX_MAX_MSG)
      return DNX_ERR_RECEIVE;

   // Check to see if the message fits within the user buffer
   if (*size >= mlen)
   {
      // User buffer is adequate, read directly into it
      if (read(channel->chan, buf, (int)mlen) != (int)mlen)
         return DNX_ERR_RECEIVE;
      *size = (int)mlen;
   }
   else
   {
      // User buffer is inadequate, read whole message and truncate
      if (read(channel->chan, mbuf, (int)mlen) == (int)mlen)
         return DNX_ERR_RECEIVE;
      memcpy(buf, mbuf, *size);  // Copy portion that fits
      // No need to adjust size variable, since we used the all of it
   }

   // Set source IP/port information, if desired
   if (src)
   {
      if (getpeername(channel->chan, (struct sockaddr *)&src_addr, &slen) == 0)
         memcpy(src, &src_addr, sizeof(src_addr));
      else
         *src = 0;   // Set zero-byte to indicate no source address avavilable
   }
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a TCP channel.
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
int dnxTcpWrite(dnxChannel * channel, char * buf, int size, 
      int timeout, char * dst)
{
   fd_set fd_wrs;
   struct timeval tv;
   int nsd;
   unsigned short mlen;

   assert(channel && channel->type == DNX_CHAN_TCP && buf);

   // Validate that the message size is within bounds
   if (size < 1 || size > DNX_MAX_MSG)
      return DNX_ERR_SIZE;

   // Make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // Implement timeout logic, if timeout value is > 0
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
         dnxSyslog(LOG_ERR, "dnxTcpWrite: select failure: %s", strerror(errno));
         return DNX_ERR_SEND;
      }
   }

   // Convert the size into a network ushort
   mlen = (unsigned short)size;
   mlen = htons(mlen);

   // Send the length of the message as a header
   if (write(channel->chan, &mlen, sizeof(mlen)) != sizeof(mlen))
      return DNX_ERR_SEND;

   // Send the message
   if (write(channel->chan, buf, size) != size)
      return DNX_ERR_SEND;
   
   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

