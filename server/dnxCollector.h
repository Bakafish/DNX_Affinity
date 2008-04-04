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

/** Implements the DNX Collector thread.
 *
 * The purpose of this thread is to collect service check
 * completion results from the worker nodes.  When a service
 * check result is collected, this thread dequeues the service
 * check from the Jobs queue and posts the result to the existing
 * Nagios service_result_buffer.
 *
 * @file dnxCollector.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXCOLLECTOR_H_
#define _DNXCOLLECTOR_H_

void *dnxCollector (void *data);

#endif   /* _DNXCOLLECTOR_H_ */

