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

/** Test harness for the dnxJobList.c module.
 *
 * @file jltest.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include "dnxJobList.h"

#define JL_SIZE		5

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
