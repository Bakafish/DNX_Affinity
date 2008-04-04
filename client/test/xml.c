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

/** Test harness for dnxXml.c.
 *
 * @file xml.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxXml.h"

static char *szProg;

int xmlPut (DnxXmlBuf *xbuf, DnxJob *pJob);
int xmlGet (DnxXmlBuf *xbuf, DnxJob *pJob);
int xmlDump (char *prefix, DnxXmlBuf *xbuf);
int jobDump (char *prefix, DnxJob *pJob);


//----------------------------------------------------------------------------

int main (int argc, char **argv)
{
   DnxXmlBuf xbuf;
   DnxJob job;
   char *cp;
   int ret;

   // Set program base name
   szProg = (char *)((cp = strrchr(argv[0], '/')) ? (cp+1) : argv[0]);

   // Initialize Job structure
   memset(&job, 0, sizeof(job));
   dnxMakeGuid(&(job.guid), DNX_OBJ_JOB, 12345L, 3);
   job.state    = DNX_JOB_PENDING;
   job.priority = 7;
   job.cmd      = "check_spam.pl <wak>test</wak> ahora por favor";
   jobDump("Initialized Job", &job);

   // Create an XML buffer
   if ((ret = xmlPut(&xbuf, &job)) == DNX_OK)
   {
      // Examine the XML buffer
      xmlDump("After xmlPut", &xbuf);

      // Clear the job structure and see if we get the same data back from the XML buffer
      printf("Clearing the job structure.\n");
      memset(&job, 0, sizeof(job));
      jobDump("Cleared Job", &job);

      // Reconstitute the job structure from the xml buffer
      ret = xmlGet(&xbuf, &job);

      // Examine the XML buffer
      jobDump("After xmlGet", &job);

      // Cleanup
      if (job.cmd) free(job.cmd);
   }
   else
      fprintf(stderr, "Error from xmlPut: %d\n", ret);

   return ret;
}

//----------------------------------------------------------------------------
// Test adding to the XML buffer

int xmlPut (DnxXmlBuf *xbuf, DnxJob *pJob)
{
   
   int ret;
   
   // Initialize an XML buffer
   if ((ret = dnxXmlOpen(xbuf, "Job")) != DNX_OK)
   {
      fprintf(stderr, "%s: dnxXmlOpen failed: %d\n", szProg, ret);
      exit (ret);
   }

   printf("xndXmlOpen('Job'): Size=%d (%d), Buf=\"%s\"\n", xbuf->size, strlen(xbuf->buf), xbuf->buf);

   dnxXmlAdd  (xbuf, "GUID",     DNX_XML_GUID, &(pJob->guid));
   dnxXmlAdd  (xbuf, "State",    DNX_XML_INT,  &(pJob->state));
   dnxXmlAdd  (xbuf, "Priority", DNX_XML_INT,  &(pJob->priority));
   dnxXmlAdd  (xbuf, "Command",  DNX_XML_STR,    pJob->cmd);
   dnxXmlClose(xbuf);

   return ret;
}

//----------------------------------------------------------------------------
// Test retrieving from the XML buffer

int xmlGet (DnxXmlBuf *xbuf, DnxJob *pJob)
{
   char *msg = NULL;
   int ret;

   // Validate parameters
   if (!xbuf || !pJob)
      return DNX_ERR_INVALID;

   // Verify this is a "Job" message
   if ((ret = dnxXmlGet(xbuf, "Request", DNX_XML_STR, &msg)) != DNX_OK)
      return ret;
   if (strcmp(msg, "Job"))
   {
      ret = DNX_ERR_SYNTAX;
      goto abend;
   }

   // Decode the job's GUID
   if ((ret = dnxXmlGet(xbuf, "GUID", DNX_XML_GUID, &(pJob->guid))) != DNX_OK)
      goto abend;

   // Decode the job's state
   if ((ret = dnxXmlGet(xbuf, "State", DNX_XML_INT, &(pJob->state))) != DNX_OK)
      goto abend;

   // Decode the job's priority
   if ((ret = dnxXmlGet(xbuf, "Priority", DNX_XML_INT, &(pJob->priority))) != DNX_OK)
      goto abend;

   // Decode the job's command
   ret = dnxXmlGet(xbuf, "Command", DNX_XML_STR, &(pJob->cmd));

abend:

   // Check for abend condition
   if (ret != DNX_OK)
   {
      if (msg) free(msg);
   }

   return ret;
}

//----------------------------------------------------------------------------

int xmlDump (char *prefix, DnxXmlBuf *xbuf)
{
   // Validate parameters
   if (!prefix || !xbuf)
      return DNX_ERR_INVALID;

   printf("%s: Size=%d (%d), Buf=\"%s\"\n", prefix, xbuf->size, strlen(xbuf->buf), xbuf->buf);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int jobDump (char *prefix, DnxJob *pJob)
{
   // Validate parameters
   if (!prefix || !pJob)
      return DNX_ERR_INVALID;

   printf("%s: Job Structure:\n", prefix);
   printf("\tGUID : %u-%lu-%u\n", pJob->guid.objType, pJob->guid.objSerial, pJob->guid.objSlot);
   printf("\tState: %d\n", pJob->state);
   printf("\tPrior: %d\n", pJob->priority);
   printf("\tCmd  : %s\n", (char *)((pJob->cmd) ? pJob->cmd : "NULL"));

   return DNX_OK;
}

