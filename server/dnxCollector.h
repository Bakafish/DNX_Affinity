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

//	dnxCollector.h
//
//	This file implements the DNX Collector thread.
//
//	The purpose of this thread is to collect service check
//	completion results from the worker nodes.  When a service
//	check result is collected, this thread dequeues the service
//	check from the Jobs queue and posts the result to the existing
//	Nagios service_result_buffer.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-07-11
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
//	Written: 2006-07-11	R.W.Ingraham
//

#ifndef _DNXCOLLECTOR_H_
#define _DNXCOLLECTOR_H_

//
//	Constants
//


//
//	Structures
//


//
//	Globals
//


//
//	Prototypes
//

void *dnxCollector (void *data);

#endif   /* _DNXCOLLECTOR_H_ */

