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

// dnxProtocol.c
//
// This module contains all of the communications methods for
// the Distributed Nagios eXecutive.
//
// Exports:
//
//    - dnxRegisterDispatcher
//    - dnxDeregisterDispatcher
//    - dnxGetJob
//    - dnxPutJob
//    - dnxGetResult
//    - dnxPutResult
//
// Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written:   2006-06-19
// Last Modified:   2007-09-26
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "dnxError.h"
#include "dnxTransport.h"
#include "dnxXml.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"


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


//----------------------------------------------------------------------------
// CLIENT: Use to register with Registrar

int dnxRegister (dnxChannel *channel, DnxNodeRequest *pReg, char *address)
{
   DnxXmlBuf xbuf;

   // Validate parameters
   if (!channel || !pReg)
      return DNX_ERR_INVALID;

   // Create the XML message
   dnxXmlOpen (&xbuf, "Register");
   dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_GUID, &(pReg->guid));
   dnxXmlAdd  (&xbuf, "ReqType",  DNX_XML_INT,  &(pReg->reqType));
   dnxXmlAdd  (&xbuf, "Capacity", DNX_XML_UINT, &(pReg->jobCap));
   dnxXmlAdd  (&xbuf, "TTL",      DNX_XML_UINT, &(pReg->ttl));
   dnxXmlClose(&xbuf);

   // Send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
// CLIENT: Use to deregister with Registrar

int dnxDeRegister (dnxChannel *channel, DnxNodeRequest *pReg, char *address)
{
   DnxXmlBuf xbuf;

   // Validate parameters
   if (!channel || !pReg)
      return DNX_ERR_INVALID;

   // Create the XML message
   dnxXmlOpen (&xbuf, "DeRegister");
   dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_GUID, &(pReg->guid));
   dnxXmlAdd  (&xbuf, "ReqType",  DNX_XML_INT,  &(pReg->reqType));
   dnxXmlAdd  (&xbuf, "Capacity", DNX_XML_UINT, &(pReg->jobCap));
   dnxXmlAdd  (&xbuf, "TTL",      DNX_XML_UINT, &(pReg->ttl));
   dnxXmlClose(&xbuf);

   // Send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
// SERVER: Used by Registrar to wait for a node request

int dnxWaitForNodeRequest (dnxChannel *channel, DnxNodeRequest *pReg, char *address, int timeout)
{
   DnxXmlBuf xbuf;
   char *msg = NULL;
   int ret;

   // Validate parameters
   if (!channel || !pReg)
      return DNX_ERR_INVALID;

   // Initialize the node request structure
   memset(pReg, 0, sizeof(DnxNodeRequest));

   // Await a message from the specified channel
   xbuf.size = DNX_MAX_MSG;
   if ((ret = dnxGet(channel, xbuf.buf, &(xbuf.size), timeout, address)) != DNX_OK)
      return ret;

   // Decode the XML message:
   xbuf.buf[xbuf.size] = '\0';
   dnxDebug(2, "dnxWaitForNodeRequest: XML Msg(%d)=%s", xbuf.size, xbuf.buf);

   // Verify this is a "Job" message
   if ((ret = dnxXmlGet(&xbuf, "Request", DNX_XML_STR, &msg)) != DNX_OK)
      return ret;
   if (strcmp(msg, "NodeRequest"))
   {
      dnxSyslog(LOG_ERR, "dnxWaitForNodeRequest: Unrecognized Request=%s", msg);
      ret = DNX_ERR_SYNTAX;
      goto abend;
   }

   // Decode the worker node's GUID
   if ((ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_GUID, &(pReg->guid))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxWaitForNodeRequest: Invalid GUID: %d", ret);
      goto abend;
   }

   // Decode request type
   if ((ret = dnxXmlGet(&xbuf, "ReqType", DNX_XML_INT, &(pReg->reqType))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxWaitForNodeRequest: Invalid ReqType: %d", ret);
      goto abend;
   }

   // Decode job capacity
   if ((ret = dnxXmlGet(&xbuf, "JobCap", DNX_XML_INT, &(pReg->jobCap))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxWaitForNodeRequest: Invalid JobCap: %d", ret);
      goto abend;
   }

   // Decode job expiration (Time-To-Live in seconds)
   if ((ret = dnxXmlGet(&xbuf, "TTL", DNX_XML_INT, &(pReg->ttl))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxWaitForNodeRequest: Invalid TTL: %d", ret);
      goto abend;
   }

abend:

   // Check for abend condition
   if (ret != DNX_OK)
   {
      if (msg) free(msg);
   }

   return ret;
}

//----------------------------------------------------------------------------
// CLIENT: Issued to Registrar to request a job

int dnxWantJob (dnxChannel *channel, DnxNodeRequest *pReg, char *address)
{
   DnxXmlBuf xbuf;

   // Validate parameters
   if (!channel || !pReg)
      return DNX_ERR_INVALID;

   // Create the XML message
   dnxXmlOpen (&xbuf, "NodeRequest");
   dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_GUID, &(pReg->guid));
   dnxXmlAdd  (&xbuf, "ReqType",  DNX_XML_INT,  &(pReg->reqType));
   dnxXmlAdd  (&xbuf, "Capacity", DNX_XML_UINT, &(pReg->jobCap));
   dnxXmlAdd  (&xbuf, "TTL",      DNX_XML_UINT, &(pReg->ttl));
   dnxXmlClose(&xbuf);

   dnxDebug(2, "dnxWantJob: XML Msg(%d)=%s", xbuf.size, xbuf.buf);

   // Send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
// CLIENT: Used to wait for a job from the Dispatcher

int dnxGetJob (dnxChannel *channel, DnxJob *pJob, char *address, int timeout)
{
   DnxXmlBuf xbuf;
   char *msg = NULL;
   int ret;

   // Validate parameters
   if (!channel || !pJob)
      return DNX_ERR_INVALID;

   // Initialize the job structure
   memset(pJob, 0, sizeof(DnxJob));

   // Await a message from the specified channel
   xbuf.size = DNX_MAX_MSG;
   if ((ret = dnxGet(channel, xbuf.buf, &(xbuf.size), timeout, address)) != DNX_OK)
      return ret;

   // Decode the XML message:
   xbuf.buf[xbuf.size] = '\0';

   // Verify this is a "Job" message
   if ((ret = dnxXmlGet(&xbuf, "Request", DNX_XML_STR, &msg)) != DNX_OK)
      return ret;
   if (strcmp(msg, "Job"))
   {
      ret = DNX_ERR_SYNTAX;
      goto abend;
   }

   // Decode the job's GUID
   if ((ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_GUID, &(pJob->guid))) != DNX_OK)
      goto abend;

   // Decode the job's state
   if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &(pJob->state))) != DNX_OK)
      goto abend;

   // Decode the job's priority
   if ((ret = dnxXmlGet(&xbuf, "Priority", DNX_XML_INT, &(pJob->priority))) != DNX_OK)
      goto abend;

   // Decode the job's timeout
   if ((ret = dnxXmlGet(&xbuf, "Timeout", DNX_XML_INT, &(pJob->timeout))) != DNX_OK)
      goto abend;

   // Decode the job's command
   ret = dnxXmlGet(&xbuf, "Command", DNX_XML_STR, &(pJob->cmd));

abend:

   // Check for abend condition
   if (ret != DNX_OK)
   {
      if (msg) free(msg);
      if (pJob->cmd) free(pJob->cmd);
   }

   return ret;
}

//----------------------------------------------------------------------------
// SERVER: Used by Dispatcher to send a job to a client

int dnxPutJob (dnxChannel *channel, DnxJob *pJob, char *address)
{
   DnxXmlBuf xbuf;

   // Validate parameters
   if (!channel || !pJob || !(pJob->cmd))
      return DNX_ERR_INVALID;

   // Create the XML message
   dnxXmlOpen (&xbuf, "Job");
   dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_GUID, &(pJob->guid));
   dnxXmlAdd  (&xbuf, "State",    DNX_XML_INT,  &(pJob->state));
   dnxXmlAdd  (&xbuf, "Priority", DNX_XML_INT,  &(pJob->priority));
   dnxXmlAdd  (&xbuf, "Timeout",  DNX_XML_INT,  &(pJob->timeout));
   dnxXmlAdd  (&xbuf, "Command",  DNX_XML_STR,    pJob->cmd);
   dnxXmlClose(&xbuf);

   // Send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
// SERVER: Used by Collector to receive a result from a client

int dnxGetResult (dnxChannel *channel, DnxResult *pResult, char *address, int timeout)
{
   DnxXmlBuf xbuf;
   char *msg = NULL;
   int ret;

   // Validate parameters
   if (!channel || !pResult)
      return DNX_ERR_INVALID;

   // Initialize the result structure
   memset(pResult, 0, sizeof(DnxResult));

   // Await a message from the specified channel
   xbuf.size = DNX_MAX_MSG;
   if ((ret = dnxGet(channel, xbuf.buf, &(xbuf.size), timeout, address)) != DNX_OK)
      return ret;

   // Decode the XML message:
   xbuf.buf[xbuf.size] = '\0';
   dnxDebug(2, "dnxGetResult: XML Msg(%d)=%s", xbuf.size, xbuf.buf);

   // Verify this is a "Job" message
   if ((ret = dnxXmlGet(&xbuf, "Request", DNX_XML_STR, &msg)) != DNX_OK)
      return ret;
   if (strcmp(msg, "Result"))
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Unrecognized Request=%s", msg);
      ret = DNX_ERR_SYNTAX;
      goto abend;
   }

   // Decode the result's GUID
   if ((ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_GUID, &(pResult->guid))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Invalid GUID: %d", ret);
      goto abend;
   }

   // Decode the result's state
   if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &(pResult->state))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Invalid State: %d", ret);
      goto abend;
   }

   // Decode the result's execution time delta
   if ((ret = dnxXmlGet(&xbuf, "Delta", DNX_XML_UINT, &(pResult->delta))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Invalid Delta: %d", ret);
      goto abend;
   }

   // Decode the result's result code
   if ((ret = dnxXmlGet(&xbuf, "ResultCode", DNX_XML_INT, &(pResult->resCode))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Invalid ResultCode: %d", ret);
      goto abend;
   }

   // Decode the result's result data
   if ((ret = dnxXmlGet(&xbuf, "ResultData", DNX_XML_STR, &(pResult->resData))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetResult: Invalid ResultData: %d", ret);
   }

abend:

   // Check for abend condition
   if (ret != DNX_OK)
   {
      if (msg) free(msg);
      if (pResult->resData) free(pResult->resData);
   }

   return ret;
}

//----------------------------------------------------------------------------
// CLIENT: Used to report a result to the Collector

int dnxPutResult (dnxChannel *channel, DnxResult *pResult, char *address)
{
   DnxXmlBuf xbuf;

   // Validate parameters
   if (!channel || !pResult)
      return DNX_ERR_INVALID;

   // Create the XML message
   dnxXmlOpen (&xbuf, "Result");
   dnxXmlAdd  (&xbuf, "GUID",       DNX_XML_GUID, &(pResult->guid));
   dnxXmlAdd  (&xbuf, "State",      DNX_XML_INT,  &(pResult->state));
   dnxXmlAdd  (&xbuf, "Delta",      DNX_XML_UINT, &(pResult->delta));
   dnxXmlAdd  (&xbuf, "ResultCode", DNX_XML_INT,  &(pResult->resCode));
   if (pResult->resData && pResult->resData[0])
      dnxXmlAdd  (&xbuf, "ResultData", DNX_XML_STR, pResult->resData);
   else
      dnxXmlAdd  (&xbuf, "ResultData", DNX_XML_STR, "(DNX: No output!)");
   dnxXmlClose(&xbuf);

   // Send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
// MANAGER: User to issue requests to client agent

int dnxGetMgmtRequest (dnxChannel *channel, DnxMgmtRequest *pRequest, char *address, int timeout)
{
   DnxXmlBuf xbuf;
   char *msg = NULL;
   int ret;

   // Validate parameters
   if (!channel || !pRequest)
      return DNX_ERR_INVALID;

   // Clear MgmtRequest structure
   memset(pRequest, 0, sizeof(DnxMgmtRequest));

   // Await a message from the specified channel
   xbuf.size = DNX_MAX_MSG;
   if ((ret = dnxGet(channel, xbuf.buf, &(xbuf.size), timeout, address)) != DNX_OK)
   {
      if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_ERR, "dnxGetMgmtRequest: Failed to retrieve message from channel: %d", ret);
      return ret;
   }

   // Decode the XML message:
   xbuf.buf[xbuf.size] = '\0';

   // Verify this is a "Job" message
   if ((ret = dnxXmlGet(&xbuf, "Request", DNX_XML_STR, &msg)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetMgmtRequest: Failed to decode Request: %d", ret);
      return ret;
   }
   if (strcmp(msg, "MgmtRequest"))
   {
      ret = DNX_ERR_SYNTAX;
      dnxSyslog(LOG_ERR, "dnxGetMgmtRequest: Invalid Request: %-20.20s", msg);
      goto abend;
   }

   // Decode the Manager's GUID
   if ((ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_GUID, &(pRequest->guid))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetMgmtRequest: Failed to decode GUID: %d", ret);
      goto abend;
   }

   // Decode the management request
   if ((ret = dnxXmlGet(&xbuf, "Action", DNX_XML_STR, &(pRequest->action))) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxGetMgmtRequest: Failed to decode Action: %d", ret);
      goto abend;
   }

abend:

   // Check for abend condition
   if (ret != DNX_OK)
   {
      if (msg) free(msg);
      if (pRequest->action) free(pRequest->action);
   }

   return ret;
}

/*--------------------------------------------------------------------------*/

