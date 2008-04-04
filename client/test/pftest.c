//	pftest.c
//
//	Tests process group membership after pfopen() call.
//

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
