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

/** Types and definitions for TCP transport layer.
 * 
 * @file dnxTcp.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXTCP_H_
#define _DNXTCP_H_

#include "dnxChannel.h"

#define DNX_TCP_LISTEN  5

int dnxTcpInit(void);
int dnxTcpDeInit(void);
int dnxTcpNew(DnxChannel ** channel, char * url);
int dnxTcpDelete(DnxChannel * channel);
int dnxTcpOpen(DnxChannel * channel, DnxChanMode mode);
int dnxTcpClose(DnxChannel * channel);
int dnxTcpRead(DnxChannel * channel, char * buf, int * size, int timeout, char * src);
int dnxTcpWrite(DnxChannel * channel, char * buf, int size, int timeout, char * dst);

#endif   /* _DNXTCP_H_ */

