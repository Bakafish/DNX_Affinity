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

// dnxUdp.h
//
// Defintions and prototypes for UDP transport layer.
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


#ifndef _DNXUDP_H_
#define _DNXUDP_H_

// Obtain definition of dnxChannel
#include "dnxChannel.h"


//
// Constants
//


//
// Structures
//


//
// Globals
//


//
// Prototypes
//

int dnxUdpInit (void);
int dnxUdpDeInit (void);
int dnxUdpNew (dnxChannel **channel, char *url);
int dnxUdpDelete (dnxChannel *channel);
int dnxUdpOpen (dnxChannel *channel, dnxChanMode mode);
int dnxUdpClose (dnxChannel *channel);
int dnxUdpRead (dnxChannel *channel, char *buf, int *size, int timeout, char *src);
int dnxUdpWrite (dnxChannel *channel, char *buf, int size, int timeout, char *dst);

#endif   /* _DNXUDP_H_ */

