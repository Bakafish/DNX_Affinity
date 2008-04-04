
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
