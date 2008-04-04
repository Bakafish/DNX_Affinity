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

/** Test harness for facility codes.
 *
 * @file ftest.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

typedef struct _FacilityCodes_ {
	char *str;
	int val;
} FacilityCodes;

static FacilityCodes facCode[] = {
	{ "LOG_LOCAL0",	LOG_LOCAL0 },
	{ "LOG_LOCAL1",	LOG_LOCAL1 },
	{ "LOG_LOCAL2",	LOG_LOCAL2 },
	{ "LOG_LOCAL3",	LOG_LOCAL3 },
	{ "LOG_LOCAL4",	LOG_LOCAL4 },
	{ "LOG_LOCAL5",	LOG_LOCAL5 },
	{ "LOG_LOCAL6",	LOG_LOCAL6 },
	{ "LOG_LOCAL7",	LOG_LOCAL7 },
	{ NULL, -1 }
};

static int verifyFacility (char *szFacility, int *nFacility)
{
	FacilityCodes *p;

	for (p = facCode; p->str && strcmp(szFacility, p->str); p++);

	return (*nFacility = p->val);
}

int main (int argc, char **argv)
{
	int i, ret, facility;

	for (i=1; i < argc; i++)
	{
		facility = 0;
		ret = verifyFacility(argv[i], &facility);
		printf("[%d]: %s is %x (%d)\n", i, argv[i], facility, ret);
	}

	return 0;
}
