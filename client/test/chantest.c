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

// chantest.c
//
// Tests channel functions
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxTransport.h"
#include "dnxXml.h"

DnxChannel * pDispatch;  // Dispatch communications channel
DnxChannel * pCollect;   // Collector communications channel

static int initComm (void);
static int nukeComm (void);

/*--------------------------------------------------------------------------*/

int main (int argc, char **argv)
{
   pDispatch = pCollect = NULL;

   initComm();

   printf("Comms initialized...\n");

   nukeComm();

   printf("Comms nuked...\n");

   exit(0);
}

/*--------------------------------------------------------------------------*/

static int initComm (void)
{
   int ret;

#ifdef DEBUG
   syslog(LOG_DEBUG, "DnxNebMain: Creating Dispatch and Collector channels");
#endif

   // Initialize the DNX comm stack
   if ((ret = dnxChanMapInit(NULL)) != DNX_OK)
   {
      printf("initComm: dnxChanMapInit failed: %d\n", ret);
      return ret;
   }

   // Create Dispatcher channel
   if ((ret = dnxChanMapAdd("Dispatch", "udp://0.0.0.0:12480")) != DNX_OK)
   {
      printf("initComm: dnxChanMapInit(Dispatch) failed: %d\n", ret);
      return ret;
   }

   // Create Collector channel
   if ((ret = dnxChanMapAdd("Collect", "udp://0.0.0.0:12481")) != DNX_OK)
   {
      printf("initComm: dnxChanMapInit(Collect) failed: %d\n", ret);
      return ret;
   }
   
   // Attempt to open the Dispatcher channel
   if ((ret = dnxConnect("Dispatch", 0, &pDispatch)) != DNX_OK)
   {
      printf("initComm: dnxConnect(Dispatch) failed: %d\n", ret);
      return ret;
   }
   
   // Attempt to open the Collector channel
   if ((ret = dnxConnect("Collect", 0, &pCollect)) != DNX_OK)
   {
      printf("initComm: dnxConnect(Collect) failed: %d\n", ret);
      return ret;
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

static int nukeComm (void)
{
   int ret;

   // Close the Collector channel
   if ((ret = dnxDisconnect(pCollect)) != DNX_OK)
      printf("nukeComm: Failed to disconnect Collector channel: %d\n", ret);

   // Close the Dispatcher channel
   if ((ret = dnxDisconnect(pDispatch)) != DNX_OK)
      printf("nukeComm: Failed to disconnect Dispatcher channel: %d\n", ret);

   // Release the DNX comm stack
   if ((ret = dnxChanMapRelease()) != DNX_OK)
      printf("nukeComm: Failed to release DNX comm stack: %d\n", ret);

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

