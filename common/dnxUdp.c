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

/** Implements the DNX UDP Transport Layer.
 *
 * @file dnxUdp.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxUdp.h"     // temporary
#include "dnxTSPI.h"

#include "dnxTransport.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>

/** The implementation of the UDP low-level I/O transport. */
typedef struct iDnxUdpChannel_
{
   char * host;         //!< Channel transport host name.
   int port;            //!< Channel transport port number.
   int socket;          //!< Channel transport socket.
   iDnxChannel ichan;   //!< Channel transport I/O (TSPI) methods.
} iDnxUdpChannel;

/** @todo Use GNU reentrant resolver extensions on platforms where available. */

static pthread_mutex_t udpMutex;

/*--------------------------------------------------------------------------
                  TRANSPORT SERVICE PROVIDER INTERFACE
  --------------------------------------------------------------------------*/

/** Open a UDP channel object.
 * 
 * @param[in] icp - the UDP channel object to be opened.
 * @param[in] active - boolean; true (1) indicates the transport will be used
 *    in active mode (as a client); false (0) indicates the transport will be 
 *    used in passive mode (as a server listen point).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxUdpOpen(iDnxChannel * icp, int active)
{
   iDnxUdpChannel * iucp = (iDnxUdpChannel *)
         ((char *)icp - offsetof(iDnxUdpChannel, ichan));
   struct hostent * he;
   struct sockaddr_in inaddr;
   int sd;

   assert(icp && iucp->port > 0);

   inaddr.sin_family = AF_INET;
   inaddr.sin_port = (in_port_t)iucp->port;
   inaddr.sin_port = htons(inaddr.sin_port);

   // see if we are listening on any address
   if (!strcmp(iucp->host, "INADDR_ANY") 
         || !strcmp(iucp->host, "0.0.0.0") 
         || !strcmp(iucp->host, "0"))
   {
      // make sure that the request is passive
      if (active) return DNX_ERR_ADDRESS;
      inaddr.sin_addr.s_addr = INADDR_ANY;
   }
   else  // resolve destination address
   {
      DNX_PT_MUTEX_LOCK(&udpMutex);
      if ((he = gethostbyname(iucp->host)) != 0)
         memcpy(&inaddr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
      DNX_PT_MUTEX_UNLOCK(&udpMutex);

      if (!he) return DNX_ERR_ADDRESS;
   }

   if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
   {
      dnxLog("dnxUdpOpen: socket failed: %s.", strerror(errno));
      return DNX_ERR_OPEN;
   }

   // determine how to handle socket connectivity based upon open mode
   if (active)
   {
      // for UDP, this sets the default destination address, so we can
      // now use send() and write() in addition to sendto() and sendmsg()
      if (connect(sd, (struct sockaddr *)&inaddr, sizeof inaddr) != 0)
      {
         dnxLog("dnxUdpOpen: connect(%lx) failed: %s.", 
               (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         close(sd);
         return DNX_ERR_OPEN;
      }
   }
   else
   {
      // want to listen for incoming packets, so bind to the
      // specified local address and port
      if (bind(sd, (struct sockaddr *)&inaddr, sizeof inaddr) != 0)
      {
         close(sd);
         dnxLog("dnxUdpOpen: bind(%lx) failed: %s.", 
               (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }

   iucp->socket = sd;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a UDP channel object.
 * 
 * @param[in] icp - the UDP channel object to be closed.
 * 
 * @return Always returns zero.
 */
static int dnxUdpClose(iDnxChannel * icp)
{
   iDnxUdpChannel * iucp = (iDnxUdpChannel *)
         ((char *)icp - offsetof(iDnxUdpChannel, ichan));

   assert(icp && iucp->socket);

   close(iucp->socket);
   iucp->socket = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a UDP channel object.
 * 
 * @param[in] icp - the UDP channel object from which to read data.
 * @param[out] buf - the address of storage into which data should be read.
 * @param[in,out] size - on entry, the maximum number of bytes that may be 
 *    read into @p buf; on exit, returns the number of bytes stored in @p buf.
 * @param[in] timeout - the maximum number of seconds we're willing to wait
 *    for data to become available on @p icp without returning a timeout
 *    error.
 * @param[out] src - the address of storage for the sender's address if 
 *    desired. This parameter is optional, and may be passed as NULL. If
 *    non-NULL, the buffer pointed to by @p src must be at least the size
 *    of a @em sockaddr_in structure.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note On Linux, @em select updates the timeout value to reflect the 
 * remaining timeout value when @em select returns an EINTR (system interrupt) 
 * error. On most other systems, @em select does not touch the timeout value, 
 * so we have to update it ourselves between calls to @em select.
 */
static int dnxUdpRead(iDnxChannel * icp, char * buf, int * size, 
      int timeout, char * src)
{
   iDnxUdpChannel * iucp = (iDnxUdpChannel *)
         ((char *)icp - offsetof(iDnxUdpChannel, ichan));
   struct sockaddr_in bit_bucket;
   socklen_t slen = sizeof bit_bucket;
   struct timeval tv;
   int mlen;

#ifndef linux
   time_t expires = time(0) + timeout;
#endif

   assert(icp && iucp->socket && buf && size && *size > 0);

   // implement timeout logic, if timeout value is > 0
   tv.tv_usec = 0L;
   tv.tv_sec = timeout;

   while (tv.tv_sec > 0)
   {
      fd_set fd_rds;
      int nsd;

      FD_ZERO(&fd_rds);
      FD_SET(iucp->socket, &fd_rds);

      if ((nsd = select(iucp->socket + 1, &fd_rds, 0, 0, &tv)) == 0)
         return DNX_ERR_TIMEOUT;

      if (nsd > 0)
         break;

      if (errno != EINTR) 
      {
         dnxLog("dnxUdpRead: select failed: %s.", strerror(errno));
         return DNX_ERR_RECEIVE;
      }

#ifndef linux
      tv.tv_usec = 0L;
      tv.tv_sec = timeout - (int)(expires - time(0));
#endif
   }

   // read the incoming message
   if (!src) src = (char *)&bit_bucket;
   if ((mlen = recvfrom(iucp->socket, buf, *size, 0, 
         (struct sockaddr *)src, &slen)) < 0)
   {
      dnxDebug(4, "recvfrom failed: %s.", strerror(errno));
      return DNX_ERR_RECEIVE;
   }

   if (mlen < 1 || mlen > DNX_MAX_MSG)
      return DNX_ERR_RECEIVE;

   *size = mlen;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a UDP channel object.
 * 
 * @param[in] icp - the UDP channel object on which to write data.
 * @param[in] buf - a pointer to the data to be written.
 * @param[in] size - the number of bytes to be written on @p icp.
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
 * 
 * @note On Linux, @em select updates the timeout value to reflect the 
 * remaining timeout value when @em select returns an EINTR (system interrupt) 
 * error. On most other systems, @em select does not touch the timeout value, 
 * so we have to update it ourselves between calls to @em select.
 */
static int dnxUdpWrite(iDnxChannel * icp, char * buf, int size, 
      int timeout, char * dst)
{
   iDnxUdpChannel * iucp = (iDnxUdpChannel *)
         ((char *)icp - offsetof(iDnxUdpChannel, ichan));
   struct timeval tv;
   int ret;

#ifndef linux
   time_t expires = time(0) + timeout;
#endif

   assert(icp && iucp->socket && buf && size);

   // implement timeout logic, if timeout value is > 0
   tv.tv_usec = 0L;
   tv.tv_sec = timeout;

   while (tv.tv_sec > 0)   // signal interrupt management
   {
      fd_set fd_wrs;
      int nsd;

      FD_ZERO(&fd_wrs);
      FD_SET(iucp->socket, &fd_wrs);

      if ((nsd = select(iucp->socket + 1, 0, &fd_wrs, 0, &tv)) == 0)
         return DNX_ERR_TIMEOUT;

      if (nsd > 0)
         break;

      if (errno != EINTR)
      {
         dnxLog("dnxUdpWrite: select failed: %s.", strerror(errno));
         return DNX_ERR_SEND;
      }

#ifndef linux
      tv.tv_usec = 0L;
      tv.tv_sec = timeout - (int)(expires - time(0));
#endif
   }

   // check for a destination address override
   if (dst)
      ret = sendto(iucp->socket, buf, size, 0,
            (struct sockaddr *)dst, sizeof(struct sockaddr_in));
   else 
      ret = write(iucp->socket, buf, size);

   if (ret == -1)
      dnxDebug(4, "sendto/write failed: %s.", strerror(errno));

   if (ret != size)
      return DNX_ERR_SEND;
   
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a UDP channel object.
 * 
 * @param[in] icp - the UDP channel object to be deleted.
 */
static void dnxUdpDelete(iDnxChannel * icp)
{
   iDnxUdpChannel * iucp = (iDnxUdpChannel *)
         ((char *)icp - offsetof(iDnxUdpChannel, ichan));

   assert(icp && iucp->socket == 0);
   
   xfree(iucp->host);
   xfree(iucp);
}

//----------------------------------------------------------------------------

/** Create a new UDP transport.
 * 
 * @param[in] url - the URL containing the host name and port number.
 * @param[out] icpp - the address of storage for returning the new low-
 *    level UDP transport object (as a generic transport object).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxUdpNew(char * url, iDnxChannel ** icpp)
{
   char * cp, * ep, * lastchar;
   iDnxUdpChannel * iucp;
   long port;

   assert(icpp && url && *url);

   // search for host:port in URL
   if ((cp = strstr(url, "://")) == 0)
      return DNX_ERR_BADURL;
   cp += 3;

   // find host/port separator ':' - copy host name
   if ((ep = strchr(cp, ':')) == 0 || ep == cp || ep - cp > HOST_NAME_MAX)
      return DNX_ERR_BADURL;

   // extract port number
   if ((port = strtol(ep + 1, &lastchar, 0)) < 1 || port > 65535 
         || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // allocate a new iDnxUdpChannel object
   if ((iucp = (iDnxUdpChannel *)xmalloc(sizeof *iucp)) == 0)
      return DNX_ERR_MEMORY;

   memset(iucp, 0, sizeof *iucp);

   // save host name and port
   if ((iucp->host = (char *)xmalloc(ep - cp + 1)) == 0)
   {
      xfree(iucp);
      return DNX_ERR_MEMORY;
   }
   memcpy(iucp->host, cp, ep - cp);
   iucp->host[ep - cp] = 0;
   iucp->port = (int)port;

   // set I/O methods
   iucp->ichan.txOpen   = dnxUdpOpen;
   iucp->ichan.txClose  = dnxUdpClose;
   iucp->ichan.txRead   = dnxUdpRead;
   iucp->ichan.txWrite  = dnxUdpWrite;
   iucp->ichan.txDelete = dnxUdpDelete;

   *icpp = &iucp->ichan;

   return DNX_OK;
}

/*--------------------------------------------------------------------------
                           EXPORTED INTERFACE
  --------------------------------------------------------------------------*/

/** Initialize the UDP transport sub-system; return UDP channel contructor.
 * 
 * @param[out] ptxAlloc - the address of storage in which to return the 
 *    address of the UDP channel object constructor (dnxUdpNew).
 * 
 * @return Always returns zero.
 */
int dnxUdpInit(int (**ptxAlloc)(char * url, iDnxChannel ** icpp))
{
   DNX_PT_MUTEX_INIT(&udpMutex);

   *ptxAlloc = dnxUdpNew;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the UDP transport sub-system. 
 */
void dnxUdpDeInit(void)
{
   DNX_PT_MUTEX_DESTROY(&udpMutex);
}

/*--------------------------------------------------------------------------*/

