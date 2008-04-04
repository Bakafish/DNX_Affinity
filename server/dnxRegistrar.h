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

// dnxRegistrar.h
//
// This file implements the DNX Registrar thread.
//
// The purpose of this thread is to manage Worker Node registrations.
// When a Worker Node wants to receive ervice check jobs from the
// Scheduler Node, it must first register itself with the Scheduler
// Node by sending a TCP-based registration message to it.
//
// The Registrar thread manages this registration process on behalf
// of the Scheduler.
//
// Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written: 2006-07-11  R.W.Ingraham
// Last Modified: 2007-02-08
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

#ifndef _DNXREGISTRAR_H_
#define _DNXREGISTRAR_H_


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

void *dnxRegistrar (void *data);
int dnxGetNodeRequest (DnxGlobalData *gData, DnxNodeRequest **ppNode);
int dnxProcessNodeRequest (DnxGlobalData *gData);
int dnxRegisterNode (DnxGlobalData *gData, DnxNodeRequest *pMsg);
int dnxDeregisterNode (DnxGlobalData *gData, DnxNodeRequest *pMsg);
int dnxDeregisterAllNodes (DnxGlobalData *gData);

#endif   /* _DNXREGISTRAR_H_ */

