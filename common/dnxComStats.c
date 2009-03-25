#include "dnxTypes.h"
#include "dnxComStats.h"
#include "dnxDebug.h"

DCS * gTopDCS = NULL;

///Create a new DCS and add it to the end of the list
DCS* dnxComStatCreateDCS(char* address)
{

    pthread_mutex_t mutex;
    DNX_PT_MUTEX_INIT(&mutex);
    DNX_PT_MUTEX_LOCK(&mutex);

    DCS* pDCS = dnxComStatFindDCS(address);

    if(!pDCS)
    {
        pDCS = (DCS*) xcalloc (1,sizeof(DCS));
        DNX_PT_MUTEX_INIT(&pDCS->mutex);
        DNX_PT_MUTEX_LOCK(&pDCS->mutex);
        xfree(pDCS->address);
        pDCS->address = xstrdup(address);
        DCS* end = dnxComStatEnd();
        if(end)
            end->next = pDCS;

        pDCS->prev = end;
        pDCS->next = NULL;
        DNX_PT_MUTEX_UNLOCK(&pDCS->mutex);
    }
    DNX_PT_MUTEX_UNLOCK(&mutex);
    //We don't have to initialize the remaining values since we used calloc they are already set to 0
    dnxDebug(2,"New DCS  was created at %s by thread %i",address, pthread_self());
    return pDCS;
}

///Find a DCS by it's IP address
DCS* dnxComStatFindDCS(char* address)
{
    //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
    assert(address);
    assert(isalnum(*address));

    pthread_mutex_t mutex;
    DNX_PT_MUTEX_INIT(&mutex);
    DNX_PT_MUTEX_LOCK(&mutex);

    dnxDebug(3,"Attempting to find DCS for %s\n",address);

    DCS* pDCS = gTopDCS;

    while(pDCS && strcmp(pDCS->address,address) != 0)
    {
        pDCS = pDCS->next;
    }
    if(pDCS)
        dnxDebug(3,"Found DCS %s for thread %i", pDCS->address, pthread_self());
    else
        dnxDebug(2,"Warning:  Could not find DCS %s for thread %i", address, pthread_self());

    DNX_PT_MUTEX_UNLOCK(&mutex);
    return pDCS;

}

unsigned dnxComStatIncrement(char * address, int member)
{
    assert(address);

    pthread_mutex_t mutex;
    DNX_PT_MUTEX_INIT(&mutex);
    DNX_PT_MUTEX_LOCK(&mutex);

    DCS * pDCS = dnxComStatFindDCS(address);
    unsigned ret = 0;
    if(pDCS)
    {
        DNX_PT_MUTEX_LOCK(&pDCS->mutex);
        if(pDCS != gTopDCS)
            DNX_PT_MUTEX_LOCK(&gTopDCS->mutex);

        long ret =0;
        dnxDebug(3,"Incrementing stat %i on DCS %s for thread %i",member, pDCS->address,pthread_self());
        switch(member)
        {
            case PACKETS_IN :
                gTopDCS->packets_in++;
                ret = pDCS->packets_in++;
            break;

            case PACKETS_OUT :
                gTopDCS->packets_out++;
                ret = pDCS->packets_out++;
            break;

            case PACKETS_FAILED :
                gTopDCS->packets_failed++;
                ret = pDCS->packets_failed++;
        }

        if(pDCS != gTopDCS)
                    DNX_PT_MUTEX_UNLOCK(&gTopDCS->mutex);

        DNX_PT_MUTEX_UNLOCK(&pDCS->mutex);
    }else{
        dnxDebug(3,"Warning:  Tried to increment stat %i for non-existent DCS ADDRESS: %s proceeding to create DCS",member,address);
        pDCS = dnxComStatCreateDCS(address);
        assert(pDCS);
        ////assert(dnxComStatFindDCS(address));
        dnxDebug(2,"Created DCS at %s",pDCS->address);
    }

    DNX_PT_MUTEX_UNLOCK(&mutex);
    return ret;
}

///Remove a DCS
DCS* dnxComStatRemoveDCS(DCS* pDCS)
{
    if(pDCS)
    {
        dnxDebug(3,"Deleting DCS at %s\n",pDCS->address);
    }else{
        dnxDebug(3,"Cannot delete non-existent DCS!\n");
        return pDCS;
    }

    pthread_mutex_t mutex;
    DNX_PT_MUTEX_INIT(&mutex);
    DNX_PT_MUTEX_LOCK(&mutex);
    DNX_PT_MUTEX_LOCK(&pDCS->mutex);

    //Store pointers to the Previous and Next DCSs
    DCS *pNext = pDCS->next;
    DCS *pPrev = pDCS->prev;

    //Point the pointers to each other (Shake hands guys)
    //This unlinks the DCS from the DCSlist
    if(pPrev)
        pPrev->next = pNext;

    if(pNext)
        pNext->prev = pPrev;

    //Bye, Bye I'm leaving
    DNX_PT_MUTEX_UNLOCK(&pDCS->mutex);  //Hopefully by now nothing can touch this, however if the object is still being reffered to by something we could be causing a segfault
    DNX_PT_MUTEX_DESTROY(&pDCS->mutex);
    xfree(pDCS->address);
    xfree(pDCS);

    DNX_PT_MUTEX_UNLOCK(&mutex);

    return pNext;
}

///Destroy all DCSs attached to gTopDCS and take gTopDCS out too
void dnxComStatDestroy()
{
    DCS * pDCS = gTopDCS;
    while(pDCS)
    {
        pDCS = dnxComStatRemoveDCS(pDCS);
    }
    gTopDCS = NULL;
}

///Reset all DCS values
void dnxComStatReset()
{
    dnxDebug(3,"dnxComStatReset Called, reseting all DCS(s) stats!");
    DCS * pDCS = gTopDCS;
    while(pDCS)
    {
        DNX_PT_MUTEX_LOCK(&pDCS->mutex);
            pDCS->packets_in = 0;
            pDCS->packets_out = 0;
            pDCS->packets_failed = 0;
        DNX_PT_MUTEX_UNLOCK(&pDCS->mutex);
        pDCS = pDCS->next;
    }
}

///Remove a particular DCS
void dnxComStatClear(char * address)
{
    DCS * pDCS = dnxComStatFindDCS(address);
    if(pDCS)
        dnxComStatRemoveDCS(pDCS);
}

///Return a pointer to the end DCS in the list
DCS* dnxComStatEnd()
{
    DCS* pDCS = gTopDCS;

    while(pDCS && pDCS->next)
    {
        pDCS = pDCS->next;
    }
    return pDCS;
}


