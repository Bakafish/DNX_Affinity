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

/** Implements the DNX TCP Transport Layer.
 *
 * @file dnxTcp.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxTcp.h"     // temporary
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

/** Number of listen buffers per TCP listen point. */
#define DNX_TCP_LISTEN  5

/** The implementation of the TCP low-level I/O transport. */
typedef struct iDnxTcpChannel_
{
   char * host;         //!< Channel transport host name.
   int port;            //!< Channel transport port number.
   int socket;          //!< Channel transport socket.
   iDnxChannel ichan;   //!< Channel transport I/O (TSPI) methods.
} iDnxTcpChannel;

/** @todo Use GNU reentrant resolver interface on platforms where available. */

static pthread_mutex_t tcpMutex;

/*--------------------------------------------------------------------------
                  TRANSPORT SERVICE PROVIDER INTERFACE
  --------------------------------------------------------------------------*/

/** Open a TCP channel object.
 * 
 * @param[in] icp - the TCP channel object to be opened.
 * @param[in] active - boolean; true (1) indicates the transport will be used
 *    in active mode (as a client); false (0) indicates the transport will be 
 *    used in passive mode (as a server listen point).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxTcpOpen(iDnxChannel * icp, int active)
{
   iDnxTcpChannel * itcp = (iDnxTcpChannel *)
         ((char *)icp - offsetof(iDnxTcpChannel, ichan));
   struct hostent * he;
   struct sockaddr_in inaddr;
   int sd;

   assert(icp && itcp->port > 0);

   inaddr.sin_family = AF_INET;
   inaddr.sin_port = (in_port_t)itcp->port;
   inaddr.sin_port = htons(inaddr.sin_port);

   // see if we are listening on any address
   if (!strcmp(itcp->host, "INADDR_ANY") 
         || !strcmp(itcp->host, "0.0.0.0") 
         || !strcmp(itcp->host, "0"))
   {
      // make sure that the request is passive
      if (active) return DNX_ERR_ADDRESS;
      inaddr.sin_addr.s_addr = INADDR_ANY;
   }
   else  // resolve destination address
   {
      DNX_PT_MUTEX_LOCK(&tcpMutex);
      if ((he = gethostbyname(itcp->host)) != 0)
         memcpy(&inaddr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
      DNX_PT_MUTEX_UNLOCK(&tcpMutex);

      if (!he) return DNX_ERR_ADDRESS;
   }

   if ((sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
   {
      dnxLog("dnxUdpOpen: socket failed: %s.", strerror(errno));
      return DNX_ERR_OPEN;
   }

   // determine how to handle socket connectivity based upon open mode
   if (active)
   {
      if (connect(sd, (struct sockaddr *)&inaddr, sizeof inaddr) != 0)
      {
         close(sd);
         dnxLog("dnxTcpOpen: connect(%lx) failed: %s.", 
               (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         return DNX_ERR_OPEN;
      }
   }
   else
   {
      // bind the socket to a local address and port and listen
      if (bind(sd, (struct sockaddr *)&inaddr, sizeof inaddr) != 0)
      {
         close(sd);
         dnxLog("dnxTcpOpen: bind(%lx) failed: %s.", 
               (unsigned long)inaddr.sin_addr.s_addr, strerror(errno));
         return DNX_ERR_OPEN;
      }
      listen(sd, DNX_TCP_LISTEN);
   }

   itcp->socket = sd;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a TCP channel object.
 * 
 * @param[in] icp - the TCP channel object to be closed.
 * 
 * @return Always returns zero.
 */
static int dnxTcpClose(iDnxChannel * icp)
{
   iDnxTcpChannel * itcp = (iDnxTcpChannel *)
         ((char *)icp - offsetof(iDnxTcpChannel, ichan));

   assert(icp && itcp->socket);

   shutdown(itcp->socket, SHUT_RDWR);
   close(itcp->socket);
   itcp->socket = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a TCP channel object.
 * 
 * @param[in] icp - the TCP channel object from which to read data.
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
static int dnxTcpRead(iDnxChannel * icp, char * buf, int * size, 
      int timeout, char * src)
{
   iDnxTcpChannel * itcp = (iDnxTcpChannel *)
         ((char *)icp - offsetof(iDnxTcpChannel, ichan));
   char mbuf[DNX_MAX_MSG];
   unsigned short mlen;

   assert(icp && itcp->socket && buf && size && *size > 0);

   // implement timeout logic, if timeout value is greater than zero
   if (timeout > 0)
   {
      struct timeval tv;
      fd_set fd_rds;
      int nsd;

      FD_ZERO(&fd_rds);
      FD_SET(itcp->socket, &fd_rds);

      tv.tv_usec = 0L;
      tv.tv_sec = timeout;

      if ((nsd = select(itcp->socket + 1, &fd_rds, 0, 0, &tv)) == 0)
         return DNX_ERR_TIMEOUT;

      if (nsd < 0)
      {
         if (errno != EINTR) 
         {
            dnxLog("dnxTcpRead: select failed: %s.", strerror(errno));
            return DNX_ERR_RECEIVE;
         }
         return DNX_ERR_TIMEOUT;
      }
   }

   // read the incoming message length
   if (read(itcp->socket, &mlen, sizeof mlen) != sizeof mlen)
      return DNX_ERR_RECEIVE;
   mlen = ntohs(mlen);

   // validate the message length
   if (mlen < 1 || mlen > DNX_MAX_MSG)
      return DNX_ERR_RECEIVE;

   // check to see if the message fits within the user buffer
   if (*size >= mlen)
   {
      if (read(itcp->socket, buf, (int)mlen) != (int)mlen)
         return DNX_ERR_RECEIVE;
      *size = (int)mlen;
   }
   else  // user buffer too small, read what we can, throw the rest away
   {
      if (read(itcp->socket, mbuf, (int)mlen) != (int)mlen)
         return DNX_ERR_RECEIVE;
      memcpy(buf, mbuf, *size);
   }

   // set source addr/port information, if desired
   if (src)
   {
      socklen_t slen;
      *src = 0;   // clear first byte in case getpeeraddr fails
      getpeername(itcp->socket, (struct sockaddr *)src, &slen);
   }
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a TCP channel object.
 * 
 * @param[in] icp - the TCP channel object on which to write data.
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
 * the @p dst parameter, the channel destination address is not used.
 * 
 * @note On Linux, @em select updates the timeout value to reflect the 
 * remaining timeout value when @em select returns an EINTR (system interrupt) 
 * error. On most other systems, @em select does not touch the timeout value, 
 * so we have to update it ourselves between calls to @em select.
 */
static int dnxTcpWrite(iDnxChannel * icp, char * buf, int size, 
      int timeout, char * dst)
{
   iDnxTcpChannel * itcp = (iDnxTcpChannel *)
         ((char *)icp - offsetof(iDnxTcpChannel, ichan));
   unsigned short mlen;

   assert(icp && itcp->socket && buf && size);

   // implement timeout logic, if timeout value is greater than zero
   if (timeout > 0)
   {
      struct timeval tv;
      fd_set fd_wrs;
      int nsd;

      FD_ZERO(&fd_wrs);
      FD_SET(itcp->socket, &fd_wrs);

      tv.tv_usec = 0L;
      tv.tv_sec = timeout;

      if ((nsd = select(itcp->socket + 1, 0, &fd_wrs, 0, &tv)) == 0)
         return DNX_ERR_TIMEOUT;

      if (nsd < 0)
      {
         if (errno != EINTR)
         {
            dnxLog("dnxTcpWrite: select failed: %s.", strerror(errno));
            return DNX_ERR_SEND;
         }
         return DNX_ERR_TIMEOUT;
      }
   }

   // convert the size into a network ushort
   mlen = (unsigned short)size;
   mlen = htons(mlen);

   // send the length of the message as a header
   if (write(itcp->socket, &mlen, sizeof mlen) != sizeof mlen)
      return DNX_ERR_SEND;

   // send the message
   if (write(itcp->socket, buf, size) != size)
      return DNX_ERR_SEND;
   
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a TCP channel object.
 * 
 * @param[in] icp - the TCP channel object to be deleted.
 */
static void dnxTcpDelete(iDnxChannel * icp)
{
   iDnxTcpChannel * itcp = (iDnxTcpChannel *)
         ((char *)icp - offsetof(iDnxTcpChannel, ichan));

   assert(icp && itcp->socket == 0);

   xfree(itcp->host);
   xfree(itcp);
}

//----------------------------------------------------------------------------

/** Create a new TCP transport.
 * 
 * @param[in] url - the URL containing the host name and port number.
 * @param[out] icpp - the address of storage for returning the new low-
 *    level TCP transport object (as a generic transport object).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxTcpNew(char * url, iDnxChannel ** icpp)
{
   char * cp, * ep, * lastchar;
   iDnxTcpChannel * itcp;
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

   // allocate a new iDnxTcpChannel object
   if ((itcp = (iDnxTcpChannel *)xmalloc(sizeof *itcp)) == 0)
      return DNX_ERR_MEMORY;
   memset(itcp, 0, sizeof *itcp);

   // save host name and port
   if ((itcp->host = (char *)xmalloc(ep - cp + 1)) == 0)
   {
      xfree(itcp);
      return DNX_ERR_MEMORY;
   }
   memcpy(itcp->host, cp, ep - cp);
   itcp->host[ep - cp] = 0;
   itcp->port = (int)port;

   // set I/O methods
   itcp->ichan.txOpen   = dnxTcpOpen;
   itcp->ichan.txClose  = dnxTcpClose;
   itcp->ichan.txRead   = dnxTcpRead;
   itcp->ichan.txWrite  = dnxTcpWrite;
   itcp->ichan.txDelete = dnxTcpDelete;

   *icpp = &itcp->ichan;

   return DNX_OK;
}

/*--------------------------------------------------------------------------
                           EXPORTED INTERFACE
  --------------------------------------------------------------------------*/

/** Initialize the TCP transport sub-system; return TCP channel contructor.
 * 
 * @param[out] ptxAlloc - the address of storage in which to return the 
 *    address of the TCP channel object constructor (dnxTcpNew).
 * 
 * @return Always returns zero.
 */
int dnxTcpInit(int (**ptxAlloc)(char * url, iDnxChannel ** icpp))
{
   DNX_PT_MUTEX_INIT(&tcpMutex);

   *ptxAlloc = dnxTcpNew;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the TCP channel sub-system. 
 */
void dnxTcpDeInit(void)
{
   DNX_PT_MUTEX_DESTROY(&tcpMutex);
}

/*--------------------------------------------------------------------------*/

