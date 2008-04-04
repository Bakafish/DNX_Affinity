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

static pthread_mutex_t udpMutex;

//----------------------------------------------------------------------------

int dnxUdpInit (void)
{
   // Create protective mutex for non-reentrant functions (gethostbyname)
   DNX_PT_MUTEX_INIT(&udpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpDeInit (void)
{
   // Destroy the mutex
   DNX_PT_MUTEX_DESTROY(&udpMutex);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpNew (dnxChannel **channel, char *url)
{
   char tmpUrl[DNX_MAX_URL+1];
   char *cp, *ep, *lastchar;
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
   if ((port = strtol(ep, &lastchar, 0)) < 1 || port > 65535 || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // Allocate a new channel structure
   if ((*channel = (dnxChannel *)malloc(sizeof(dnxChannel))) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxUdpNew: Out of Memory: malloc(dnxChannel)");
      return DNX_ERR_MEMORY;  // Memory allocation error
   }
   memset(*channel, 0, sizeof(dnxChannel));

   // Save host name and port
   (*channel)->type = DNX_CHAN_UDP;
   (*channel)->name = NULL;
   if (((*channel)->host = strdup(cp)) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxUdpNew: Out of Memory: strdup(channel->host)");
      free(*channel);
      *channel = NULL;
      return DNX_ERR_MEMORY;  // Memory allocation error
   }
   (*channel)->port = (int)port;
   (*channel)->state = DNX_CHAN_CLOSED;

   // Set I/O methods
   (*channel)->dnxOpen  = dnxUdpOpen;
   (*channel)->dnxClose = dnxUdpClose;
   (*channel)->dnxRead  = dnxUdpRead;
   (*channel)->dnxWrite = dnxUdpWrite;
   (*channel)->txDelete = dnxUdpDelete;

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpDelete (dnxChannel *channel)
{
   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_UDP)
      return DNX_ERR_INVALID;

   // Make sure this channel is closed
   if (channel->state == DNX_CHAN_OPEN)
      dnxUdpClose(channel);

   // Release host name string
   if (channel->host) free(channel->host);

   // Release channel memory
   memset(channel, 0, sizeof(dnxChannel));
   free(channel);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpOpen (dnxChannel *channel, dnxChanMode mode)   // 0=Passive, 1=Active
{
   struct hostent *he;
   struct sockaddr_in inaddr;
   int sd;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_UDP || channel->port < 1)
      return DNX_ERR_INVALID;

   // Make sure this channel isn't already open
   if (channel->state != DNX_CHAN_CLOSED)
      return DNX_ERR_ALREADY;

   // Setup the socket address structure
   inaddr.sin_family = AF_INET;
   inaddr.sin_port = (in_port_t)channel->port;
   inaddr.sin_port = htons(inaddr.sin_port);

   // See if we are listening on any address
   if (!strcmp(channel->host, "INADDR_ANY") || !strcmp(channel->host, "0.0.0.0") || !strcmp(channel->host, "0"))
   {
      // Make sure that the request is passive
      if (mode != DNX_CHAN_PASSIVE)
         return DNX_ERR_ADDRESS;

      inaddr.sin_addr.s_addr = INADDR_ANY;
   }
   else  // Resolve destination address
   {
      // Acquire the lock
      DNX_PT_MUTEX_LOCK(&udpMutex);

      // Try to resolve this address
      if ((he = gethostbyname(channel->host)) == NULL)
      {
         DNX_PT_MUTEX_UNLOCK(&udpMutex);
         return DNX_ERR_ADDRESS;
      }
      memcpy(&(inaddr.sin_addr.s_addr), he->h_addr_list[0], he->h_length);

      // Release the lock
      DNX_PT_MUTEX_UNLOCK(&udpMutex);
   }

   // Create a socket
   if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
   {
      dnxSyslog(LOG_ERR, "dnxUdpOpen: socket failure: %s", strerror(errno));
      return DNX_ERR_OPEN;
   }

   // Determine how to handle socket connectivity based upon mode
   if (mode == DNX_CHAN_ACTIVE)
   {
      // For UDP, this sets the default destination address, so we can
      // now use send() and write() in addition to sendto() and sendmsg()
      if (connect(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         dnxSyslog(LOG_ERR, "dnxUdpOpen: connect(%lx) failure: %s", (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }
   else  // DNX_CHAN_PASSIVE
   {
      // Want to listen for incoming packets, so bind to the
      // specified local address and port
      if (bind(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
      {
         close(sd);
         dnxSyslog(LOG_ERR, "dnxUdpOpen: bind(%lx) failure: %s", (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }

   // Mark the channel as open
   channel->chan  = sd;
   channel->state = DNX_CHAN_OPEN;

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpClose (dnxChannel *channel)
{
   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_UDP)
      return DNX_ERR_INVALID;

   // Make sure this channel isn't already closed
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_ALREADY;

   // Shutdown the communication paths on the socket
   // shutdown(channel->chan, SHUT_RDWR);

   // Close the socket
   close(channel->chan);

   // Mark the channel as closed
   channel->state = DNX_CHAN_CLOSED;
   channel->chan  = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpRead (dnxChannel *channel, char *buf, int *size, int timeout, char *src)
{
   struct sockaddr_in src_addr;
   socklen_t slen;
   int mlen, nsd;
   fd_set fd_rds;
   struct timeval tv;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_UDP || !buf || !size || *size < 1)
      return DNX_ERR_INVALID;

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
         dnxSyslog(LOG_ERR, "dnxUdpRead: select failure: %s", strerror(errno));
         return DNX_ERR_RECEIVE;
      }
   }

   // Read the incoming message
   slen = sizeof(src_addr);
   if ((mlen = recvfrom(channel->chan, buf, *size, 0, (struct sockaddr *)&src_addr, &slen)) < 0)
      return DNX_ERR_RECEIVE;

   // Validate the message length
   if (mlen < 1 || mlen > DNX_MAX_MSG)
      return DNX_ERR_RECEIVE;

   // Update actual packet data size
   *size = (int)mlen;

   // Set source IP/port information, if desired
   if (src)
      memcpy(src, &src_addr, sizeof(src_addr));

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxUdpWrite (dnxChannel *channel, char *buf, int size, int timeout, char *dst)
{
   struct sockaddr_in dst_addr;
   fd_set fd_wrs;
   struct timeval tv;
   int ret, nsd;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_UDP || !buf)
      return DNX_ERR_INVALID;

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
         dnxSyslog(LOG_ERR, "dnxUdpWrite: select failure: %s", strerror(errno));
         return DNX_ERR_SEND;
      }
   }

   // Check for a destination override
   if (dst)
   {
      memcpy(&dst_addr, dst, sizeof(dst_addr));
      // Send the message to the specified address
      ret = sendto(channel->chan, buf, size, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
   }
   else  // Send the message to the previously set (via connect(2)) channel address
      ret = write(channel->chan, buf, size);

   // Check for write error
   if (ret != size)
      return DNX_ERR_SEND;
   
   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

