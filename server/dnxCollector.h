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

/** Definitions and prototypes for the DNX Collector thread.
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
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXCOLLECTOR_H_
#define _DNXCOLLECTOR_H_

#include "dnxJobList.h"
#include "dnxTransport.h"

/** Abstract data type for the DNX job results collector. */
typedef struct { int unused; } DnxCollector;

/** Return a reference to the collector channel object.
 * 
 * @param[in] coll - the collector whose dispatch channel should be returned.
 * 
 * @return A pointer to the collector channel object.
 */
DnxChannel * dnxCollectorGetChannel(DnxCollector * coll);

/** Create a new collector object.
 * 
 * @param[in] chname - the name of the collect channel.
 * @param[in] collurl - the collect channel URL.
 * @param[in] joblist - a pointer to the global job list object.
 * @param[out] pcoll - the address of storage for the return of the new
 *    collector object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCollectorCreate(char * chname, char * collurl, DnxJobList * joblist, 
      DnxCollector ** pcoll);

/** Destroy an existing collector object.
 * 
 * @param[in] coll - a pointer to the collector object to be destroyed.
 */
void dnxCollectorDestroy(DnxCollector * coll);

#endif   /* _DNXCOLLECTOR_H_ */

