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

/** DNX Communications Channel definition.
 *
 * @file dnxChannel.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXCHANNEL_H_
#define _DNXCHANNEL_H_

#define DNX_MAX_URL			1023
#define DNX_MAX_MSG			1024

typedef enum _dnxChanType_ { DNX_CHAN_UNKNOWN = 0, DNX_CHAN_TCP, DNX_CHAN_UDP, DNX_CHAN_UNIX, DNX_CHAN_MSGQ } dnxChanType;

typedef enum _dnxChanMode_ { DNX_CHAN_PASSIVE = 0, DNX_CHAN_ACTIVE } dnxChanMode;

typedef enum _dnxChanState_ { DNX_CHAN_CLOSED = 0, DNX_CHAN_OPEN } dnxChanState;

typedef struct _dnxChannel_ {
	int chan;			// Can be: INET socket, UNIX socket or IPC Message Queue ID
						// (Can implement as a union in the future, if needed, to support
						// other communications mechanisms.)  Better yet, implement as an
						// opaque (void) structure pointer to allow the transport protocols
						// to manipulate this with complete encapsulation and privacy.
	dnxChanType	type; 	// Channel type
	char *name;			// Channel name, as specified to dnxConnect
	char *host;			// Host for TCP/UDP channels; NULL for Message Queues
	int port;			// Port for TCP and UDP channels; ID for Message Queues
	dnxChanState state;	// Channel state
	int debug;			// Channel comm debug flag
	int (*dnxOpen) (struct _dnxChannel_ *channel, dnxChanMode mode);
	int (*dnxClose)(struct _dnxChannel_ *channel);
	int (*dnxRead) (struct _dnxChannel_ *channel, char *buf, int *size, int timeout, char *src);
	int (*dnxWrite)(struct _dnxChannel_ *channel, char *buf, int size, int timeout, char *dst);
	int (*txDelete) (struct _dnxChannel_ *channel);	// Release a channel using this transport
} dnxChannel;

#endif   /* _DNXCHANNEL_H_ */
