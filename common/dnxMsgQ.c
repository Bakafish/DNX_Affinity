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

/** Implements the DNX System V Message Queue IPC Transport Layer.
 *
 * @file dnxMsgQ.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxMsgQ.h"    // temporary
#include "dnxTSPI.h"

#include "dnxTransport.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define DNX_MSGQ_STANDARD  1  // Message type

typedef struct _dnxMsgBuf_ 
{
   long mtype;       /* message type, must be > 0 */
   char * mtext;     /* message data */
} dnxMsgBuf;

/** The implementation of the UDP low-level I/O transport. */
typedef struct iDnxMsgQChannel_
{
   key_t queuekey;      //!< Channel transport message queue key.
   int queueid;         //!< Channel transport message queue ID.
   iDnxChannel ichan;   //!< Channel transport I/O (TSPI) methods.
} iDnxMsgQChannel;

/*--------------------------------------------------------------------------
                  TRANSPORT SERVICE PROVIDER INTERFACE
  --------------------------------------------------------------------------*/

/** Open a MSGQ channel object.
 * 
 * @param[in] icp - the UDP channel object to be opened.
 * @param[in] active - boolean; true (1) indicates the transport will be used
 *    in active mode (as a client); false (0) indicates the transport will be 
 *    used in passive mode (as a server listen point).
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxMsgQOpen(iDnxChannel * icp, int active)
{
   iDnxMsgQChannel * imcp = (iDnxMsgQChannel *)
         ((char *)icp - offsetof(iDnxMsgQChannel, ichan));
   int qid;

   assert(icp && imcp->queuekey > 0);

   // attempt to create/open the message queue
   if ((qid = msgget(imcp->queuekey, IPC_CREAT | 0x660)) == (key_t)(-1))
      return DNX_ERR_OPEN;

   imcp->queueid = qid;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a MSGQ channel object.
 * 
 * @param[in] icp - the UDP channel object to be closed.
 * 
 * @return Always returns zero.
 */
static int dnxMsgQClose(iDnxChannel * icp)
{
   iDnxMsgQChannel * imcp = (iDnxMsgQChannel *)
         ((char *)icp - offsetof(iDnxMsgQChannel, ichan));

   assert(icp && imcp->queueid);

   // This is really a NOP, since we don't "close" our handle to the message queue.
   //
   // However, the message queue should be deleted when no longer in use by
   // any process; but that will have to be implemented in dnxMsgQDeinit().

   imcp->queueid = 0;      // temporary till we get global close implemented

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a MSGQ channel object.
 * 
 * @param[in] icp - the MSGQ channel object from which to read data.
 * @param[out] buf - the address of storage into which data should be read.
 * @param[in,out] size - on entry, the maximum number of bytes that may be 
 *    read into @p buf; on exit, returns the number of bytes stored in @p buf.
 * @param[in] timeout - the maximum number of seconds we're willing to wait
 *    for data to become available on @p icp without returning a timeout
 *    error.
 * @param[out] src - the address of storage for the sender's address if 
 *    desired. This parameter is not used by this transport, however, it's
 *    optional, and so it may be passed as NULL by the caller.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxMsgQRead(iDnxChannel * icp, char * buf, int * size, 
      int timeout, char * src)
{
   iDnxMsgQChannel * imcp = (iDnxMsgQChannel *)
         ((char *)icp - offsetof(iDnxMsgQChannel, ichan));
   dnxMsgBuf msg;

   assert(icp && imcp->queueid && buf && size && *size > 0);

   msg.mtext = buf;

   // wait for a message, truncate if larger than the specified buffer size
   if ((*size = (int)msgrcv(imcp->queueid, &msg, *size, 0L, MSG_NOERROR)) == -1)
      return DNX_ERR_RECEIVE;
   
   /** @todo Implement timeout logic. */

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a MSGQ channel object.
 * 
 * @param[in] icp - the MSGQ channel object on which to write data.
 * @param[in] buf - a pointer to the data to be written.
 * @param[in] size - the number of bytes to be written on @p icp.
 * @param[in] timeout - the maximum number of seconds to wait for the write
 *    operation to complete without returning a timeout error.
 * @param[in] dst - the address to which the data in @p buf should be sent
 *    using this channel. This parameter is not used by this transport, 
 *    however, it's optional, and so it may be passed as NULL by the caller.
 *
 * @return Zero on success, or a non-zero error value.
 */
static int dnxMsgQWrite(iDnxChannel * icp, char * buf, int size, 
      int timeout, char * dst)
{
   iDnxMsgQChannel * imcp = (iDnxMsgQChannel *)
         ((char *)icp - offsetof(iDnxMsgQChannel, ichan));
   dnxMsgBuf msg;

   assert(icp && imcp->queueid && buf && size > 0);

   msg.mtype = (long)DNX_MSGQ_STANDARD;
   msg.mtext = buf;

   // send the message
   if (msgsnd(imcp->queueid, &msg, size, 0) == -1)
      return DNX_ERR_SEND;
   
   /** @todo Implement timeout logic. */

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a MSGQ channel object.
 * 
 * @param[in] icp - the MSGQ channel object to be deleted.
 */
static void dnxMsgQDelete(iDnxChannel * icp)
{
   iDnxMsgQChannel * imcp = (iDnxMsgQChannel *)
         ((char *)icp - offsetof(iDnxMsgQChannel, ichan));

   assert(icp && imcp->queueid == 0);

   xfree(icp);
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
static int dnxMsgQNew(char * url, iDnxChannel ** icpp)
{
   char * cp, * ep, * lastchar;
   iDnxMsgQChannel * imcp;
   long queuekey;

   assert(icpp && url && *url);

   // search for messageqid in URL
   if ((cp = strstr(url, "://")) == 0)
      return DNX_ERR_BADURL;
   cp += 3;

   // get the message queue ID
   errno = 0;
   if ((queuekey = strtol(cp, &lastchar, 0)) < 1 || errno == ERANGE 
         || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // no private keys are allowed
   if ((key_t)queuekey == IPC_PRIVATE)
      return DNX_ERR_BADURL;

   // allocate a new iDnxMsgQChannel object
   if ((imcp = (iDnxMsgQChannel *)xmalloc(sizeof *imcp)) == 0)
      return DNX_ERR_MEMORY;

   memset(imcp, 0, sizeof *imcp);

   // save message queue ID
   imcp->queuekey = (key_t)queuekey;

   // set I/O methods
   imcp->ichan.txOpen   = dnxMsgQOpen;
   imcp->ichan.txClose  = dnxMsgQClose;
   imcp->ichan.txRead   = dnxMsgQRead;
   imcp->ichan.txWrite  = dnxMsgQWrite;
   imcp->ichan.txDelete = dnxMsgQDelete;

   *icpp = &imcp->ichan;

   return DNX_OK;
}

/*--------------------------------------------------------------------------
                           EXPORTED INTERFACE
  --------------------------------------------------------------------------*/

/** Initialize the MSGQ transport sub-system; return MSGQ channel contructor.
 * 
 * @param[out] ptxAlloc - the address of storage in which to return the 
 *    address of the MSGQ channel object constructor (dnxMsgQNew).
 * 
 * @return Always returns zero.
 */
int dnxMsgQInit(int (**ptxAlloc)(char * url, iDnxChannel ** icpp))
{
   // Could use this routine to loop through the global channel map
   // and create all message queues found therein (or error out if
   // any of them are already in use...)

   *ptxAlloc = dnxMsgQNew;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the MSGQ transport sub-system. 
 */
void dnxMsgQDeInit(void)
{
   // Could use this routine to remove all of our own message queues
   // from the system IPC space.
}

/*--------------------------------------------------------------------------*/

