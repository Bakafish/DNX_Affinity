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

/** Tests process group membership after pfopen() call.
 *
 * @file pftest.c
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
#include <errno.h>

#include "pfopen.h"


int main (int argc, char **argv)
{
	PFILE *pf;
	int i;
	char data[1024];

	// Identify ourself and parentage
	printf("Before PFOPEN: Pid=%d, PGrp=%d, PPid=%d\n", getpid(), getpgrp(), getppid());
	fflush(stdout);

	// pfopen
	if ((pf = pfopen("pgrp", "r")) == NULL)
	{
		fprintf(stderr, "Main: pfopen failed: %s\n", strerror(errno));
		exit(1);
	}

	// Read child output
	printf("Child Output:\n");
	fflush(stdout);
	i = 0;
	while (i < 2 && fgets(data, sizeof(data), PF_OUT(pf)) != NULL)
	{
		printf("%s", data);
		i++;
	}
	printf("*** END of child output ***\n");
	fflush(stdout);

	// Terminate child process
	printf("Sending group term signal to child process group\n");
	fflush(stdout);
	pfkill(pf, SIGTERM);
	
	// Close pipe to child process
	printf("Closing connection to child process\n");
	fflush(stdout);
	pfclose(pf);

	printf("SUCCESS\n");
	fflush(stdout);

	exit(0);
}
