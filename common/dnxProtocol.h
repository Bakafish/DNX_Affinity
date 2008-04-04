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

/** Types and definitions for DNX messaging protocol.
 * 
 * @file dnxProtocol.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXPROTOCOL_H_
#define _DNXPROTOCOL_H_

#include <time.h>

#include "dnxChannel.h"

/** Defines the type of a DNX object in a network message. */
typedef enum DnxObjType
{
   DNX_OBJ_SCHEDULER = 0,
   DNX_OBJ_DISPATCHER,
   DNX_OBJ_WORKER,
   DNX_OBJ_COLLECTOR,
   DNX_OBJ_REAPER,
   DNX_OBJ_JOB,
   DNX_OBJ_MANAGER,
   DNX_OBJ_MAX
} DnxObjType;

/** Defines the type of a DNX worker node network request. */
typedef enum DnxReqType
{
   DNX_REQ_REGISTER = 0, 
   DNX_REQ_DEREGISTER, 
   DNX_REQ_ACK, 
   DNX_REQ_NAK 
} DnxReqType;

/** Defines the state of a DNX job. */
typedef enum DnxJobState
{
   DNX_JOB_NULL = 0, 
   DNX_JOB_PENDING, 
   DNX_JOB_INPROGRESS, 
   DNX_JOB_COMPLETE, 
   DNX_JOB_EXPIRED 
} DnxJobState;

/** The maximum number of bytes in a DNX message address buffer. */
#define DNX_MAX_ADDRESS 64

/** A DNX transaction id. */
typedef struct DnxXID
{
   DnxObjType objType;
   unsigned long objSerial;
   unsigned long objSlot;
} DnxXID;

typedef struct DnxNodeRequest
{
   DnxXID xid;                      // Worker node transaction id
   DnxReqType reqType;              // Request type
   unsigned int jobCap;             // Job capacity
   unsigned int ttl;                // Request Time-To-Live (in seconds)
   time_t expires;                  // Job expiration time (not transmitted)
   char address[DNX_MAX_ADDRESS];   // Source address
} DnxNodeRequest;

typedef struct DnxJob
{
   DnxXID xid;                      // Job transaction id
   DnxJobState state;               // Job state
   int priority;                    // Execution Priority
   int timeout;                     // Max job execution time
   char * cmd;                      // Contains command plus arguments
   char address[DNX_MAX_ADDRESS];   // Source address
} DnxJob;

typedef struct DnxResult
{
   DnxXID xid;                      // Job transaction id
   DnxJobState state;               // Job state
   unsigned int delta;              // Job execution time delta
   int resCode;                     // Job result code
   char * resData;                  // Job result data
   char address[DNX_MAX_ADDRESS];   // Source address
} DnxResult;

typedef struct DnxMgmtRequest
{
   DnxXID xid;                      // Manager transaction id
   char * action;                   // Request: SHUTDOWN, RELOAD, STATUS
   char address[DNX_MAX_ADDRESS];   // Source address
} DnxMgmtRequest;

typedef struct DnxMgmtReply
{
   DnxXID xid;                      // Client transaction id
   DnxReqType status;               // Request status: ACK or NAK
   char * reply;                    // Reply data (only valid for STATUS request)
   char address[DNX_MAX_ADDRESS];   // Source address
} DnxMgmtReply;

int dnxRegister(DnxChannel * channel, DnxNodeRequest * pReg, char * address);
int dnxDeRegister(DnxChannel * channel, DnxNodeRequest * pReg, char * address);
int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, 
      char * address, int timeout);
int dnxWantJob(DnxChannel * channel, DnxNodeRequest * pReq, char * address);
int dnxGetJob(DnxChannel * channel, DnxJob * pJob, char * address, int timeout);
int dnxPutJob(DnxChannel * channel, DnxJob * pJob, char * address);
int dnxGetResult(DnxChannel * channel, DnxResult * pResult, char * address, 
      int timeout);
int dnxPutResult(DnxChannel * channel, DnxResult * pResult, char * address);
int dnxGetMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest, 
      char * address, int timeout);

int dnxMakeXID(DnxXID * pxid, DnxObjType xType, unsigned long xSerial, 
      unsigned long xSlot);

#endif   /* _DNXPROTOCOL_H_ */

