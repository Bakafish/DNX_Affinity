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

/** Implements the System V Message Queue IPC Tranport Layer.
 *
 * @file dnxMsgQ.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxMsgQ.h"

#include "dnxError.h"
#include "dnxDebug.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
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

//----------------------------------------------------------------------------

/** Initialize the MSGQ channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxMsgQInit(void)
{
   // Could use this routine to loop through the global channel map
   // and create all message queues found therein (or error out if
   // any of them are already in use...)

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Clean up global resources allocated by the MSGQ channel sub-system.
 * 
 * @return Always returns zero.
 */
int dnxMsgQDeInit(void)
{
   // Could use this routine to remove all of our own message queues
   // from the system IPC space.

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Create a new MSGQ channel.
 * 
 * @param[out] channel - the address of storage for returning the new channel.
 * @param[in] url - the URL bind address to associate with the new channel.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxMsgQNew(DnxChannel ** channel, char * url)
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

   // Get the message queue ID
   errno = 0;
   if ((port = strtol(cp, &lastchar, 0)) < 1 || errno == ERANGE || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // No private keys are allowed
   if ((key_t)port == IPC_PRIVATE)
      return DNX_ERR_BADURL;

   // Allocate a new channel structure
   if ((*channel = (DnxChannel *)xmalloc(sizeof(DnxChannel))) == NULL)
      return DNX_ERR_MEMORY;  // Memory allocation error
   memset(*channel, 0, sizeof(DnxChannel));

   // Save host name and port
   (*channel)->type = DNX_CHAN_MSGQ;
   (*channel)->name = NULL;
   (*channel)->host = NULL;
   (*channel)->port = (int)port; // This is really the Message Queue ID
   (*channel)->state = DNX_CHAN_CLOSED;

   // Set I/O methods
   (*channel)->dnxOpen  = dnxMsgQOpen;
   (*channel)->dnxClose = dnxMsgQClose;
   (*channel)->dnxRead  = dnxMsgQRead;
   (*channel)->dnxWrite = dnxMsgQWrite;
   (*channel)->txDelete = dnxMsgQDelete;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a MSGQ channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxMsgQDelete(DnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_MSGQ);

   // Make sure this channel is closed
   if (channel->state == DNX_CHAN_OPEN)
      dnxMsgQClose(channel);

   // Release channel memory
   memset(channel, 0, sizeof(DnxChannel));
   xfree(channel);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Open a MSGQ channel.
 * 
 * Possible modes are active (1) or passive (0). 
 * 
 * @param[in] channel - the channel to be opened.
 * @param[in] mode - the mode in which @p channel should be opened.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxMsgQOpen(DnxChannel * channel, DnxChanMode mode)
{
   int qid;

   assert(channel && channel->type == DNX_CHAN_MSGQ && channel->port > 0);

   // Make sure this channel isn't already open
   if (channel->state != DNX_CHAN_CLOSED)
      return DNX_ERR_ALREADY;

   // Attempt to create/open the message queue
   if ((qid = msgget((key_t)(channel->port), (IPC_CREAT | 0x660))) == (key_t)(-1))
      return DNX_ERR_OPEN;

   // Mark the channel as open
   channel->chan  = qid;
   channel->state = DNX_CHAN_OPEN;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Close a MSGQ channel.
 * 
 * @param[in] channel - the channel to be closed.
 * 
 * @return Always returns zero.
 */
int dnxMsgQClose(DnxChannel * channel)
{
   assert(channel && channel->type == DNX_CHAN_MSGQ);

   // Make sure this channel isn't already closed
   assert(channel->state == DNX_CHAN_OPEN);
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_ALREADY;

   // This is really a NOP, since we don't "close" our handle to the message queue.
   //
   // However, the message queue should be deleted when no longer in use by
   // any process; but that will have to be implemented in dnxMsgQDeinit().

   // Mark the channel as closed
   channel->state = DNX_CHAN_CLOSED;
   channel->chan  = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Read data from a MSGQ channel.
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
int dnxMsgQRead(DnxChannel * channel, char * buf, int * size, 
      int timeout, char * src)
{
   dnxMsgBuf msg;

   assert(channel && channel->type == DNX_CHAN_MSGQ && buf && size && *size > 0);

   // Make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // Prepare the message for transfer
   msg.mtext = buf;

   // Wait for a message, truncate if larger than the specified buffer size
   if ((*size = (int)msgrcv(channel->chan, &msg, (size_t)*size, 0L, MSG_NOERROR)) == -1)
      return DNX_ERR_RECEIVE;
   
   /** @todo Implement timeout logic. */

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Write data to a MSGQ channel.
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
int dnxMsgQWrite(DnxChannel * channel, char * buf, int size, 
      int timeout, char * dst)
{
   dnxMsgBuf msg;

   assert(channel && channel->type == DNX_CHAN_MSGQ && buf);

   // Validate that the message size is within bounds
   if (size < 1 || size > DNX_MAX_MSG)
      return DNX_ERR_SIZE;

   // Make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // Prepare the message for transfer
   msg.mtype = (long)DNX_MSGQ_STANDARD;
   msg.mtext = buf;

   // Send the message
   if (msgsnd(channel->chan, &msg, (size_t)size, 0) == -1)
      return DNX_ERR_SEND;
   
   /** @todo Implement timeout logic. */

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

