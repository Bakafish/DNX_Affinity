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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>


int main (int argc, char **argv)
{
   struct sockaddr_in inaddr;
   char msg[512], resp[512];
   int sd;
   int rc;


   // Create UDP socket
   if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
   {
      fprintf(stderr, "%s: Failed to open UDP socket: %s\n", argv[0], strerror(errno));
      exit(1);
   }

   // Setup the server socket address structure
    inaddr.sin_family = AF_INET;
   inaddr.sin_addr.s_addr = htonl(0x7f000001);
    inaddr.sin_port = htons(30400);

   // Set the default destination address
   if (connect(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
   {
      close(sd);
      fprintf(stderr, "%s: Failed to set default dest address: %s\n", argv[0], strerror(errno));
      exit(2);
   }

   // Setup test message
   strcpy(msg, "The Quick Brown Fox Jumped Over the Lazy Dog's Back");

   // Send test packet to test server
   if ((rc = write(sd, msg, strlen(msg))) != strlen(msg))
   {
      close(sd);
      fprintf(stderr, "%s: Failed to write test packet: %d bytes written (%d)\n", argv[0], rc, errno);
      exit(3);
   }

   printf("Sent test packet to server.  Awaiting response...\n");
   fflush(stdout);

   // Await a response
   if ((rc = read(sd, resp, sizeof(resp))) < 0)
      fprintf(stderr, "%s: Failed to read response packet: %s\n", argv[0], strerror(errno));
   else if (rc == 0)
      fprintf(stderr, "%s: Failed to read response packet: Received EOF (zero bytes) from socket\n", argv[0]);
   else
   {
      if (rc < sizeof(resp))
         resp[rc] = '\0';
      else
         resp[sizeof(resp)-1] = '\0';

      printf("Received response: %s\n", resp);
   }

   printf("\nsizeof(sockaddr_in6) = %d\n", sizeof(struct sockaddr_in6));
   fflush(stdout);

   // Close socket and cleanup
   close(sd);

   return 0;
}

/*--------------------------------------------------------------------------*/

