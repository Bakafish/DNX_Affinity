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

/** Definitions and prototypes for the DNX Registrar thread.
 *
 * The purpose of this thread is to manage Worker Node registrations.
 * When a Worker Node wants to receive ervice check jobs from the
 * Scheduler Node, it must first register itself with the Scheduler
 * Node by sending a TCP-based registration message to it.
 * 
 * The Registrar thread manages this registration process on behalf
 * of the Scheduler.
 *
 * @file dnxRegistrar.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXREGISTRAR_H_
#define _DNXREGISTRAR_H_

#include "dnxQueue.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"

/** An abstraction data type for the DNX registrar object. */
typedef struct { int unused; } DnxRegistrar;

/** Return an available node "request for work" object pointer.
 * 
 * @param[in] reg - the registrar from which a node request should be returned.
 * @param[out] ppNode - the address of storage in which to return the located
 *    request node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxGetNodeRequest(DnxRegistrar * reg, DnxNodeRequest ** ppNode);

/** Create a new registrar object.
 * 
 * @param[in] queuesz - the size of the queue to create in this registrar.
 * @param[in] dispchan - a pointer to the dispatcher channel.
 * @param[out] preg - the address of storage in which to return the newly
 *    created registrar.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxRegistrarCreate(unsigned queuesz, DnxChannel * dispchan, 
      DnxRegistrar ** preg);

/** Destroy a previously created registrar object.
 * 
 * Signals the registrar thread, waits for it to stop, and frees allocated 
 * resources.
 * 
 * @param[in] reg - the registrar to be destroyed.
 */
void dnxRegistrarDestroy(DnxRegistrar * reg);

/** Create an Affinity linked list item.
*
* Adds an affinity struct and returns the new item.
*
* @param[in] p - the affinity list to add item to.
* @param[in] name - the name of the affinity group or host.
* @param[in] flag - the binary bitmask representation for the affinity group.
*
* @return Affinity object on success, NULL on failure.
*/
DnxAffinityList * addDnxAffinity(DnxAffinityList *p, char * name, unsigned long long flag);

#endif   /* _DNXREGISTRAR_H_ */

