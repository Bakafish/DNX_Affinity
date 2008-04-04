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

/** Displays process group membership data after fork().
 *
 * @file pgrp.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>


static void sig_main (int sig)
{
   syslog(LOG_INFO, "Main: Caught signal %d", sig);
   printf("Main: Caught signal %d\n", sig);
   fflush(stdout);
   exit(1);
}

static void sig_child (int sig)
{
   syslog(LOG_INFO, "Child: Caught signal %d", sig);
   printf("Child: Caught signal %d\n", sig);
   fflush(stdout);
   exit(1);
}

int main (int argc, char **argv)
{
   int pid;
   int status;

   signal(SIGHUP, sig_main);
   signal(SIGINT, sig_main);
   signal(SIGQUIT, sig_main);
   signal(SIGTERM, sig_main);

   // Identify ourself and parentage
   printf("Main: Pid=%d, PGrp=%d, PPid=%d\n", getpid(), getpgrp(), getppid());
   fflush(stdout);

   // Fork
   if ((pid = fork()) < 0)
   {
      fprintf(stderr, "Main: fork failed: %s\n", strerror(errno));
      exit(1);
   }
   else if (pid > 0)
   {
      // Parent
      wait(&status);
      exit(0);
   }

   // Child
   signal(SIGHUP, sig_child);
   signal(SIGINT, sig_child);
   signal(SIGQUIT, sig_child);
   signal(SIGTERM, sig_child);
   printf("Child: Pid=%d, PGrp=%d, PPid=%d\n", getpid(), getpgrp(), getppid());
   fflush(stdout);
   sleep(10);

   _exit(0);
}

