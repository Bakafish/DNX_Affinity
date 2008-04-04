//	jltest.c
//
//	Test harness for the dnxJobList.c module.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include "dnxJobList.h"


//
//	Constants
//

#define JL_SIZE		5


//
//	Structures
//


//
//	Globals
//


//
//	Prototypes
//


//----------------------------------------------------------------------------

int main (int argc, char **argv)
{
	DnxJobList *pJobList;
	int ret;

	// Initialize the job list
	if ((ret = dnxJobListInit(&pJobList, JL_SIZE)) != DNX_OK)
	{
		fprintf(stderr, "%s: Failed to initialize DNX Job List: %d\n", argv[0], ret);
		exit(1);
	}

	// Test add several jobs past limit

	// Test add a few and dispatch a few

	// Test collecting some

	// Now add some more

	// Release the job list
	if ((ret = dnxJobListWhack(&pJobList)) != DNX_OK)
	{
		fprintf(stderr, "%s: Failed to release DNX Job List: %d\n", argv[0], ret);
		exit(2);
	}

	return 0;
}

//----------------------------------------------------------------------------
