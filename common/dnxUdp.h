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
 * This file is temporary till we get loadable transport libraries. Once 
 * that is finished, then dnxTSPI.h will act as a proper header file for all 
 * loadable transports.
 * 
 * @file dnxUdp.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXUDP_H_
#define _DNXUDP_H_

#include "dnxTSPI.h"

// UDP transport sub-system initialization/shutdown.
extern int dnxUdpInit(int (**ptxAlloc)(char * url, iDnxChannel ** icpp));
extern void dnxUdpDeInit(void);

#endif   /* _DNXUDP_H_ */

