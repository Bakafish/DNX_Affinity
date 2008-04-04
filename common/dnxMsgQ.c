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

// dnxMsgQ.c
//
// Implements the System V Message Queue IPC Tranport Layer
//
// Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written:   2006-06-19
// Last Modified:   2007-02-08
//
// License:
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxMsgQ.h"

//
// Constants
//

#define DNX_MSGQ_STANDARD  1  // Message type


//
// Structures
//

typedef struct _dnxMsgBuf_ {
   long mtype;    /* message type, must be > 0 */
   char *mtext;   /* message data */
} dnxMsgBuf;


//
// Globals
//


//
// Prototypes
//


//----------------------------------------------------------------------------

int dnxMsgQInit (void)
{
   // Could use this routine to loop through the global channel map
   // and create all message queues found therein (or error out if
   // any of them are already in use...)

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxMsgQDeInit (void)
{
   // Could use this routine to remove all of our own message queues
   // from the system IPC space.

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxMsgQNew (dnxChannel **channel, char *url)
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

   // Get the message queue ID
   errno = 0;
   if ((port = strtol(cp, &lastchar, 0)) < 1 || errno == ERANGE || (*lastchar && *lastchar != '/'))
      return DNX_ERR_BADURL;

   // No private keys are allowed
   if ((key_t)port == IPC_PRIVATE)
      return DNX_ERR_BADURL;

   // Allocate a new channel structure
   if ((*channel = (dnxChannel *)malloc(sizeof(dnxChannel))) == NULL)
      return DNX_ERR_MEMORY;  // Memory allocation error
   memset(*channel, 0, sizeof(dnxChannel));

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

int dnxMsgQDelete (dnxChannel *channel)
{
   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_MSGQ)
      return DNX_ERR_INVALID;

   // Make sure this channel is closed
   if (channel->state == DNX_CHAN_OPEN)
      dnxMsgQClose(channel);

   // Release channel memory
   memset(channel, 0, sizeof(dnxChannel));
   free(channel);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxMsgQOpen (dnxChannel *channel, dnxChanMode mode)
{
   int qid;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_MSGQ || channel->port < 1)
      return DNX_ERR_INVALID;

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

int dnxMsgQClose (dnxChannel *channel)
{
   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_MSGQ)
      return DNX_ERR_INVALID;

   // Make sure this channel isn't already closed
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

int dnxMsgQRead (dnxChannel *channel, char *buf, int *size, int timeout, char *src)
{
   dnxMsgBuf msg;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_MSGQ || !buf || *size < 1)
      return DNX_ERR_INVALID;

   // Make sure this channel is open
   if (channel->state != DNX_CHAN_OPEN)
      return DNX_ERR_OPEN;

   // Prepare the message for transfer
   msg.mtext = buf;

   // Wait for a message, truncate if larger than the specified buffer size
   if ((*size = (int)msgrcv(channel->chan, &msg, (size_t)*size, 0L, MSG_NOERROR)) == -1)
      return DNX_ERR_RECEIVE;
   
   // TODO: Implement timeout logic

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxMsgQWrite (dnxChannel *channel, char *buf, int size, int timeout, char *dst)
{
   dnxMsgBuf msg;

   // Validate parameters
   if (!channel || channel->type != DNX_CHAN_MSGQ || !buf)
      return DNX_ERR_INVALID;

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
   
   // TODO: Implement timeout logic

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

