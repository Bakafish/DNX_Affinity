//	dnxTransport.h
//
//	Function prototypes for DNx transport layer.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-06-19
//	Last Modified:   2007-02-08
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#ifndef _DNXTRANSPORT_H_
#define _DNXTRANSPORT_H_

// Obtain definitions of dnxChanType, dnxChanMap and dnxChannel
#include "dnxChannel.h"


//
//	Constants
//

#define DNX_MAX_CHAN_MAP	1000


//
//	Structures
//

typedef struct _dnxChanMap_ {
	char *name;			// Channel name, as read from configuration file
	char *url;			// Channel connection specification
	dnxChanType	type; 	// Channel type: which transport to use
	int (*txAlloc) (dnxChannel **channel, char *url);	// Transport's channel factory
} dnxChanMap;


//
//	Globals
//


//
//	Prototypes
//

int dnxChanMapInit (char *fileName);
int dnxChanMapRelease (void);
int dnxChanMapAdd (char *name, char *url);
int dnxChanMapUrlParse (dnxChanMap *chanMap, char *url);
int dnxChanMapDelete (char *name);
int dnxChanMapFindSlot (dnxChanMap **chanMap);
int dnxChanMapFindName (char *name, dnxChanMap **chanMap);
int dnxConnect (char *name, dnxChannel **channel, dnxChanMode mode);
int dnxDisconnect (dnxChannel *channel);
int dnxGet (dnxChannel *channel, char *buf, int *size, int timeout, char *src);
int dnxPut (dnxChannel *channel, char *buf, int size, int timeout, char *dst);
int dnxChannelDebug (dnxChannel *channel, int doDebug);

#endif
