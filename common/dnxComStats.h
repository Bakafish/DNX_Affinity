#ifndef DNXCOMSTATS_H_INCLUDED
#define DNXCOMSTATS_H_INCLUDED
#include <pthread.h>
#include "dnxTypes.h"


/****************************************************
*   dnxComStats.h (C)2008 Intellectual Reserve      *
*   This file defines the DnxComStat aka DCS object *
*   The purpose of DCS is to providing an           *
*   insturmentation for the tracking of stats for   *
*   things in common between client and server      *
*   such as packets in / out etc.                   *
*   Both client and server should include this file *
*   And then instantiate a global DCS object gTopDCS*
*   for maximum benefit.                            *
****************************************************/


typedef struct DCS
{
    struct DCS * next;
    struct DCS * prev;
    unsigned packets_out;
    unsigned packets_in;
    unsigned packets_failed;
    pthread_mutex_t mutex;
    char * address;
} DCS;

///Create a new DCS and add it to the end of the list
DCS* dnxComStatCreateDCS(char* address);

///Increment and return a value
unsigned dnxComStatIncrement(char * address, int member);

///Remove a DCS
DCS* dnxComStatRemoveDCS(DCS* pDCS);

///Destroy all DCSs attached to pDCS and take pDCS out too
void dnxComStatDestroy();

///Destroy all DCSs and recreate the top DCS.
void dnxComStatReset();

///Return a pointer to the end DCS in the list
DCS* dnxComStatEnd();

///Find a DCS by it's IP address
DCS* dnxComStatFindDCS(char* address);

///Clear stats on a particular DCS
void dnxComStatClear(char * address);

#endif // DNXCOMSTATS_H_INCLUDED
