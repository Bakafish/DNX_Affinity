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

/** Main source file for DNX management control client.
 * 
 * @file dnxstats.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_STATS_IMPL
 */

/*!@defgroup DNX_STATS_IMPL DNX Management Client Implementation 
 */

#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxError.h"
#include "dnxDebug.h"

#if HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef VERSION
# define VERSION "<unknown>"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Print a usage string and exit. 
 * 
 * @param[in] base - the base program name.
 */
static void usage(char * base)
{
   fprintf(stderr, 
         "Usage: %s [options]\n"
         "Where [options] are:\n"
         "  -s <host>    specify target host name (default: localhost).\n"
         "  -p <port>    specify target port number (default: 12480).\n"
         "  -c <cmdstr>  send <cmdstr> to server.\n"
         "  -v           print version and exit.\n"
         "  -h           print this help and exit.\n\n", base);
   exit(-1);
}

/** The main program entry point for the dnx management client.
 * 
 * @param[in] argc - the number of elements in the @p argv array.
 * @param[in] argv - a null-terminated array of command-line arguments.
 * 
 * @return Zero on success, or a non-zero error code that is returned to the
 * shell. Any non-zero codes should be values between 1 and 127.
 */
int main(int argc, char ** argv)
{
   extern char * optarg;
   extern int optind, opterr, optopt;

   int ch, ret;
   char * cp, * prog, * cmdstr;
   char * hoststr, * portstr;

   // get program base name
   prog = (char *)((cp = strrchr(argv[0], '/')) != 0 ? (cp + 1) : argv[0]);

   // parse arguments
   hoststr = "localhost";
   portstr = "12480";
   opterr = 0;
   cmdstr = 0;
   while ((ch = getopt(argc, argv, "hvc:s:p:")) != -1)
   {
      switch (ch)
      {
         case 's':
            hoststr = optarg;
            break;

         case 'p':
            portstr = optarg;
            break;

         case 'c': 
            cmdstr = optarg; 
            break;

         case 'v':
            printf("\n  %s version %s\n  Bug reports: %s.\n\n", 
                  prog, VERSION, PACKAGE_BUGREPORT);
            exit(0);

         case 'h': 
         default :
            usage(prog);
      }
   }

   // ensure we've been given a command
   if (!cmdstr)
   {
      fprintf(stderr, "%s: No command string specified.\n", prog);
      usage(prog);
   }

   // init comm sub-system; send command; wait for response
   if ((ret = dnxChanMapInit(0)) != 0)
      fprintf(stderr, "%s: Error initializing channel map: %s.\n", 
            prog, dnxErrorString(ret));
   else
   {
      char url[1024];

      snprintf(url, sizeof url, "udp://%s:%s", hoststr, portstr);

      if ((ret = dnxChanMapAdd("MgmtClient", url)) != 0)
         fprintf(stderr, "%s: Error adding channel (%s): %s.\n", 
               prog, url, dnxErrorString(ret));
      else
      {
         DnxChannel * channel;

         if ((ret = dnxConnect("MgmtClient", 1, &channel)) != 0)
            fprintf(stderr, "%s: Error connecting to server (%s): %s.\n", 
                  prog, url, dnxErrorString(ret));
         else
         {
            DnxMgmtRequest req;

            memset(&req, 0, sizeof req);
            dnxMakeXID(&req.xid, DNX_OBJ_MANAGER, 0, 0);
            req.action = cmdstr;

            if ((ret = dnxSendMgmtRequest(channel, &req, 0)) != 0)
               fprintf(stderr, "%s: Error sending request: %s.\n", 
                     prog, dnxErrorString(ret));
            else
            {
               DnxMgmtReply rsp;

               if ((ret = dnxWaitForMgmtReply(channel, &rsp, 0, 10)) != 0)
                  fprintf(stderr, "%s: Error receiving response: %s.\n", 
                        prog, dnxErrorString(ret));
               else
               {
                  if (rsp.status == DNX_REQ_ACK)
                     printf("%s\n", rsp.reply);
                  else
                     fprintf(stderr, "%s: Request failed on server.\n", prog);
      
                  xfree(rsp.reply);
               }
            }
            dnxDisconnect(channel);
         }
         dnxChanMapDelete("MgmtClient");
      }
      dnxChanMapRelease();
   }

   xheapchk();

   return ret? -1: 0;
}

/*--------------------------------------------------------------------------*/

