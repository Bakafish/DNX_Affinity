#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "dnxDebug.h"
#include "dnxNode.h"

DnxNode* gTopNode;


///Create a new node and add it to the end of the list
DnxNode* dnxNodeListCreateNode(char* address)
{

    DnxNode* pDnxNode = dnxNodeListFindNode(address);

    if(!pDnxNode)
    {

        dnxLog("Creating a node for %s\n",address);
        pDnxNode = (DnxNode*) xcalloc (1,sizeof(DnxNode));
        pDnxNode->address = xstrdup(address);
        DnxNode* end = dnxNodeListEnd();
        DNX_PT_MUTEX_INIT(&pDnxNode->mutex);
        DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);

        if(end)
        {
            DNX_PT_MUTEX_LOCK(&end->mutex);
            end->next = pDnxNode;
            DNX_PT_MUTEX_UNLOCK(&end->mutex);
        }

        pDnxNode->prev = end;
        pDnxNode->next = NULL;
        DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);

    }
    //We don't have to initialize the remaining values since we used calloc they are already set to 0

    return pDnxNode;
}

///Remove a Node
DnxNode* dnxNodeListRemoveNode(DnxNode* pDnxNode)
{
    if(pDnxNode)
    {
        dnxLog("Deleting node at %s\n",pDnxNode->address);
    }else{
        dnxLog("Cannot delete non-existent node!\n");
        return pDnxNode;
    }

    DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);

    DnxNode *pNext = NULL;
    DnxNode *pPrev = NULL;

    //Store pointers to the Previous and Next nodes
    pNext = pDnxNode->next;
    pPrev = pDnxNode->prev;

    //Take the key and lock them up
    if(pPrev)
        DNX_PT_MUTEX_LOCK(&pPrev->mutex);
    if(pNext)
        DNX_PT_MUTEX_LOCK(&pNext->mutex);

    //Point the pointers to eachother (Shake hands guys)
    //This unlinks the node from the nodelist
    if(pPrev)
    {
        pPrev->next = pNext;
        DNX_PT_MUTEX_UNLOCK(&pPrev->mutex);
    }
    if(pNext)
    {
        pNext->prev = pPrev;
        DNX_PT_MUTEX_UNLOCK(&pNext->mutex);
    }


    //Bye, Bye I'm leaving
    xfree(pDnxNode->address);
    DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);
    xfree(pDnxNode);

    return pNext;
}

///Destroy all nodes attached to pDnxNode and take pDnxNode out too
void dnxNodeListDestroy()
{
    DnxNode* pDnxNode = gTopNode;
    while(pDnxNode)
    {
        pDnxNode = dnxNodeListRemoveNode(pDnxNode);
    }
    gTopNode = NULL;
}

///Destroy all nodes and recreate the top node.
void dnxNodeListReset()
{
    dnxLog("dnxNodeListReset Called, reseting all node(s) stats!");
    dnxNodeListDestroy();
    gTopNode = dnxNodeListCreateNode("127.0.0.1");
}
///Return a pointer to the end node in the list
DnxNode* dnxNodeListEnd()
{
    DnxNode* pTopDnxNode = gTopNode;
    DnxNode* pDnxNode = NULL;
    if(pTopDnxNode)
        pDnxNode = pTopDnxNode;
    else
        return pDnxNode;

    while(pDnxNode && pDnxNode->next != NULL)
    {
        pDnxNode = pDnxNode->next;
    }
    return pDnxNode;
}

///Return a pointer to the beginning node in the list
DnxNode* dnxNodeListBegin()
{
    DnxNode* pBottomNode = gTopNode;
    DnxNode* pDnxNode = NULL;

    if(pBottomNode->prev)
    {
        pDnxNode = pBottomNode->prev;
    }else{
            return pBottomNode;
    }

    while(pDnxNode->prev != NULL)
    {
        pDnxNode = pDnxNode->prev;
    }
    return pDnxNode;
}

///Find a node by it's IP address
DnxNode* dnxNodeListFindNode(char* address)
{
    //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
    assert(address);
    assert(isalnum(*address));

    //dnxDebug(3,"Attempting to find node at %s\n",address);

    DnxNode* pDnxNode = gTopNode;

    while(pDnxNode && strcmp(pDnxNode->address,address) != 0)
    {
        pDnxNode = pDnxNode->next;
    }
    return pDnxNode;
}

///Count the nodes in the list
int dnxNodeListCountNodes()
{
    int count = 0;
    DnxNode* pDnxNode = gTopNode;
    if(pDnxNode)
        while(pDnxNode = pDnxNode->next)
        {
             count++;
             dnxLog("Counting node at %s\n",pDnxNode->address);
        }

    return count;
}


/** Function to increment member values
*   @param address  - The IP address of the node you want
*   @param  member - The name of the member you want to increment
*/
unsigned dnxNodeListIncrementNodeMember(char* address,int member)
{
    //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
    assert(address && isalnum(*address));

    DnxNode* pDnxNode = dnxNodeListFindNode(address);

    int retval = 0;

    if(pDnxNode)
    {
        DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);
        switch(member)
        {
            case JOBS_DISPATCHED :
                gTopNode->jobs_dispatched++;
                retval = pDnxNode->jobs_dispatched++;
            break;

            case JOBS_HANDLED :
                gTopNode->jobs_handled++;
                retval = pDnxNode->jobs_handled++;
            break;

            case JOBS_REQ_RECV : gTopNode->jobs_req_recv++;
                retval = pDnxNode->jobs_req_recv++;
            break;

            case JOBS_REQ_EXP :
                gTopNode->jobs_req_exp++;
                retval = pDnxNode->jobs_req_exp++;
            break;
            
            dnxLog("Error:  Tried to increment stats for non-existent stat %i",member);
            default : retval = -1;
        }
        DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);

    }else{
        dnxDebug(3,"Warning:  Tried to increment stat %i for non-existent node ADDRESS: %s proceeding to create node",member,address);
        dnxNodeListCreateNode(address);
        retval = dnxNodeListIncrementNodeMember(address,member);
    }

    return(retval);
}

/** Function to set member values
*   @param address  - The IP address of the node you want
*   @param  hostname  - the value you want to set member to
*/
unsigned dnxNodeListSetNodeAffinity(char* address, char* hostname)
{
    //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
    assert(address && isalnum(*address));

    DnxNode* pDnxNode = dnxNodeListFindNode(address);

    int retval = 0;

    if(pDnxNode)
    {
        DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);
        if(hostname != NULL){
            dnxDebug(2, "dnxNodeListSetNodeAffinity: [%s] IP address [%s]",
                hostname, address);
//                 (unsigned)((ip_addr >> 24) & 0xff),
//                 (unsigned)((ip_addr >> 16) & 0xff),
//                 (unsigned)((ip_addr >>  8) & 0xff),
//                 (unsigned)( ip_addr        & 0xff));
        
        
//            pReq->affinity = dnxGetAffinity(*hostname);
        }
        DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);

    }else{
        dnxDebug(3,"Warning:  Tried to increment stat %i for non-existent node ADDRESS: %s proceeding to create node",member,address);
        dnxNodeListCreateNode(address);
        retval = dnxNodeListIncrementNodeMember(address,member);
    }

    return(retval);
}
