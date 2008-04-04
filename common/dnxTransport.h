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

/** Types and definitions for DNX transport layer.
 * 
 * @file dnxTransport.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXTRANSPORT_H_
#define _DNXTRANSPORT_H_

#include "dnxChannel.h"

#define DNX_MAX_CHAN_MAP   1000

typedef struct DnxChanMap_ 
{
   char * name;         // Channel name, as read from configuration file
   char * url;          // Channel connection specification
   DnxChanType type;    // Channel type: which transport to use
   int (*txAlloc)(DnxChannel ** channel, char * url);  // Transport's channel factory
} DnxChanMap;

int dnxChanMapInit(char * fileName);
int dnxChanMapRelease(void);
int dnxChanMapAdd(char * name, char * url);
int dnxChanMapUrlParse(DnxChanMap * chanMap, char * url);
int dnxChanMapDelete(char * name);
int dnxChanMapFindSlot(DnxChanMap ** chanMap);
int dnxChanMapFindName(char * name, DnxChanMap ** chanMap);
int dnxConnect(char * name, DnxChannel ** channel, DnxChanMode mode);
int dnxDisconnect(DnxChannel * channel);
int dnxGet(DnxChannel * channel, char * buf, int * size, int timeout, char * src);
int dnxPut(DnxChannel * channel, char * buf, int size, int timeout, char * dst);
int dnxChannelDebug(DnxChannel * channel, int doDebug);

#endif   /* _DNXTRANSPORT_H_ */

