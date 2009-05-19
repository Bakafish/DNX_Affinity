#include <assert.h>

#include "dnxProtocol.h"
#include "dnxXml.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "common/dnxTransport.h"
#include <arpa/inet.h>

//----------------------------------------------------------------------------

/** Dispatch a job to a client node (server).
 *
 * @param[in] channel - the channel on which to send @p pJob.
 * @param[in] pJob - the job request to be sent on @p channel.
 * @param[in] address - the address to which @p pJob should be sent. This
 *    parameter is optional, and may be specified as NULL, in which case the
 *    channel address will be used.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxSendJob(DnxChannel * channel, DnxJob * pJob, char * address)
{
   DnxXmlBuf xbuf;

   assert(channel && pJob && pJob->cmd && *pJob->cmd);

   // create the XML message
   dnxXmlOpen (&xbuf, "Job");
   dnxXmlAdd  (&xbuf, "XID",      DNX_XML_XID,  &pJob->xid);
   dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_XID,  &pJob->xid); // old format - for bc
   dnxXmlAdd  (&xbuf, "State",    DNX_XML_INT,  &pJob->state);
   dnxXmlAdd  (&xbuf, "Priority", DNX_XML_INT,  &pJob->priority);
   dnxXmlAdd  (&xbuf, "Timeout",  DNX_XML_INT,  &pJob->timeout);
   dnxXmlAdd  (&xbuf, "Command",  DNX_XML_STR,   pJob->cmd);
   dnxXmlClose(&xbuf);

   dnxDebug(3, "dnxSendJob: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------
/** Wait for a node request (server).
 *
 * @param[in] channel - the channel from which to receive the node request.
 * @param[out] pReg - the address of storage into which the request should
 *    be read from @p channel.
 * @param[out] address - the address of storage in which to return the address
 *    of the sender. This parameter is optional and may be passed as NULL. If
 *    non-NULL, it should be large enough to store sockaddr_* data.
 * @param[in] timeout - the maximum number of seconds the caller is willing to
 *    wait before accepting a timeout error.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, char * address, int timeout)
{
   DnxXmlBuf xbuf;
   int ret;
   int test;

   assert(channel && pReg);

   if(pReg->ttl != 0) {
      // We are reusing an object and need to free up the old pointers
      xfree(pReg->addr);
      xfree(pReg->hn);
      pReg->addr = NULL;
      pReg->hn = NULL;
   }
   
   memset(pReg, 0, sizeof *pReg);

   // await a message from the specified channel
   xbuf.size = sizeof xbuf.buf - 1;
   if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK) {
      return ret;
   }
   
   if (address != NULL) {
        int maxlen = INET6_ADDRSTRLEN + 1;
        inet_ntop(AF_INET, &(((struct sockaddr_in *)address)->sin_addr), pReg->addr, maxlen); 
//      pReg->addr = ntop((struct sockaddr *)address); //Do this now save time in logging later
   }
   
   // decode the XML message:
   xbuf.buf[xbuf.size] = 0;
   dnxDebug(3, "dnxWaitForNodeRequest: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // verify this is a "NodeRequest" message
   if ((ret = dnxXmlCmpStr(&xbuf, "Request", "NodeRequest")) != DNX_OK)
   {
      test = dnxXmlCmpStr(&xbuf, "Request", "JobAck");
      dnxDebug(4, "dnxWaitForNodeRequest: Request (%i) != NodeRequest", test);
      return ret;
   }
   // decode the worker node's XID (support older GUID format as well)
   if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pReg->xid)) != DNX_OK
         && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pReg->xid)) != DNX_OK)
      return ret;

   // decode request type
   if ((ret = dnxXmlGet(&xbuf, "ReqType", DNX_XML_INT, &pReg->reqType)) != DNX_OK)
      return ret;

   // decode job capacity (support strange mixture of JobCap and Capacity)
   if ((ret = dnxXmlGet(&xbuf, "JobCap", DNX_XML_INT, &pReg->jobCap)) != DNX_OK
         && (ret = dnxXmlGet(&xbuf, "Capacity", DNX_XML_INT, &pReg->jobCap)) != DNX_OK)
      return ret;
    
   // decode the hostname
   if ((ret = dnxXmlGet(&xbuf, "Hostname", DNX_XML_STR, &pReg->hn)) != DNX_OK)
      return ret;
        
   // decode job expiration (Time-To-Live in seconds)
   return dnxXmlGet(&xbuf, "TTL", DNX_XML_INT, &pReg->ttl);
}
//----------------------------------------------------------------------------

/** Collect job results from a client (server).
 *
 * @param[in] channel - the channel from which to receive the job result.
 * @param[out] pResult - the address of storage into which the job result
 *    should be read from @p channel.
 * @param[out] address - the address of storage in which to return the address
 *    of the sender. This parameter is optional and may be passed as NULL. If
 *    non-NULL, it should be large enough to store sockaddr_* data.
 * @param[in] timeout - the maximum number of seconds the caller is willing to
 *    wait before accepting a timeout error.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxWaitForResult(DnxChannel * channel, DnxResult * pResult, char * address, int timeout)
{
   DnxXmlBuf xbuf;
   int ret;

   assert(channel && pResult);

   memset(pResult, 0, sizeof *pResult);

   // await a message from the specified channel
   xbuf.size = sizeof xbuf.buf - 1;
   if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
      return ret;

   // decode the XML message
   xbuf.buf[xbuf.size] = 0;
   dnxDebug(3, "dnxWaitForResult: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // verify this is a "Result" message
   if ((ret = dnxXmlCmpStr(&xbuf, "Request", "Result")) == DNX_OK)
   {

       // decode the result's XID (support older GUID format as well)
       if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pResult->xid)) != DNX_OK
             && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pResult->xid)) != DNX_OK)
          return ret;

       // decode the result's state
       if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &pResult->state)) != DNX_OK)
          return ret;

       // decode the result's execution time delta
       if ((ret = dnxXmlGet(&xbuf, "Delta", DNX_XML_UINT, &pResult->delta)) != DNX_OK)
          return ret;

       // decode the result's result code
       if ((ret = dnxXmlGet(&xbuf, "ResultCode", DNX_XML_INT, &pResult->resCode)) != DNX_OK)
          return ret;

       // decode the result's result data
       return dnxXmlGet(&xbuf, "ResultData", DNX_XML_STR, &pResult->resData);
   }

   //<SM 10/08 JobAck Mod>
   //Record the job ack
   if((ret = dnxXmlCmpStr(&xbuf, "Request", "JobAck")) == DNX_OK)
   {
        DnxXID xid;
        int current = 0;

        dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &xid);

        char * addr = ntop((struct sockaddr *)address);
        dnxDebug(3,"Received JobAck for Job XID %s from Node: %s",xid,addr);
        xfree(addr);

        dnxJobListMarkAck(&xid);

        //Call this function again and this time wait on the result (resotring original functionality)
        ret = dnxWaitForResult(channel, pResult, address, timeout);

        return ret;
   }
   //<SM 10/08 JobAck Mod End>
}



//----------------------------------------------------------------------------
