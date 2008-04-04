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

/** Types and definitions for UDP transport layer.
 * 
 * @file dnxUdp.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXUDP_H_
#define _DNXUDP_H_

#include "dnxChannel.h"

int dnxUdpInit(void);
int dnxUdpDeInit(void);
int dnxUdpNew(DnxChannel ** channel, char * url);
int dnxUdpDelete(DnxChannel * channel);
int dnxUdpOpen(DnxChannel * channel, DnxChanMode mode);
int dnxUdpClose(DnxChannel * channel);
int dnxUdpRead(DnxChannel * channel, char * buf, int * size, int timeout, char * src);
int dnxUdpWrite(DnxChannel * channel, char * buf, int size, int timeout, char * dst);

#endif   /* _DNXUDP_H_ */

