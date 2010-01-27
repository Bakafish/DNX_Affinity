#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "dnxDebug.h"
#include "dnxNode.h"

DnxNode* gTopNode;


///Create a new node and add it to the end of the list
DnxNode* dnxNodeListCreateNode(char *address, char *hostname)
{
    DnxNode *pDnxNode = NULL;
    unsigned long long int temp_flag = 0ULL;
    // This is racy as hell, should have had a list level mutex
    
    if(gTopNode != NULL) {
        DnxNode* pTopDnxNode = gTopNode;
        DNX_PT_MUTEX_LOCK(&pTopDnxNode->mutex);
        // once we have a lock on the head, see if the object has been added
        // while we were waiting
        pDnxNode = dnxNodeListFindNode(address);
        if(!pDnxNode) {
            // Make a new node
            pDnxNode = (DnxNode*) xcalloc (1,sizeof(DnxNode));
            DNX_PT_MUTEX_INIT(&pDnxNode->mutex);
            pDnxNode->address = xstrdup(address);
            pDnxNode->hostname = xstrdup(hostname);
dnxDebug(4, "dnxNodeListCreateNode: [%s,%s] flags Before:(%llu)", pDnxNode->address, pDnxNode->hostname, pDnxNode->flags);
            temp_flag = dnxGetAffinity(pDnxNode->hostname);
dnxDebug(4, "dnxNodeListCreateNode: [%s,%s] flags Return:(%llu)", pDnxNode->address, pDnxNode->hostname, temp_flag);
            pDnxNode->flags = temp_flag;
dnxDebug(4, "dnxNodeListCreateNode: [%s,%s] flags After:(%llu)", pDnxNode->address, pDnxNode->hostname, pDnxNode->flags);
            
            // Push it behind the head
            pDnxNode->prev = pTopDnxNode;
            pDnxNode->next = pTopDnxNode->next;
            pTopDnxNode->next = pDnxNode;
        } 
        
        // unlock the head
        DNX_PT_MUTEX_UNLOCK(&pTopDnxNode->mutex);
    } else {
        // We are creating the top node
        pDnxNode = (DnxNode*) xcalloc (1,sizeof(DnxNode));
        DNX_PT_MUTEX_INIT(&pDnxNode->mutex);
        pDnxNode->address = xstrdup(address);
        pDnxNode->hostname = xstrdup(hostname);
        temp_flag = dnxGetAffinity(pDnxNode->hostname);
        pDnxNode->flags = temp_flag;
        dnxDebug(4, "dnxNodeListCreateNode: [%s,%s] flags:(%llu) (%llu)", 
            pDnxNode->address, pDnxNode->hostname, 
            pDnxNode->flags,
            temp_flag 
            );

        pDnxNode->prev = NULL;
        pDnxNode->next = NULL;
    }
    
    return pDnxNode;
}
//     if(!pDnxNode)
//     {
// 
//         dnxLog("dnxNodeListCreateNode: Creating a node for %s\n",address);
//         pDnxNode = (DnxNode*) xcalloc (1,sizeof(DnxNode));
//         DNX_PT_MUTEX_INIT(&pDnxNode->mutex);
//         DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);
//         pDnxNode->address = xstrdup(address);
//         pDnxNode->hostname = xstrdup(hostname);
//         pDnxNode->flags = dnxGetAffinity(hostname);
//         DnxNode* end = dnxNodeListEnd();
// 
//         if(end)
//         {
//             DNX_PT_MUTEX_LOCK(&end->mutex);
//             end->next = pDnxNode;
//             DNX_PT_MUTEX_UNLOCK(&end->mutex);
//         }
// 
//         pDnxNode->prev = end;
//         pDnxNode->next = NULL;
//         DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);
// 
//     }
//     // We don't have to initialize the remaining values since 
//     // we used calloc they are already set to 0
// 
//     return pDnxNode;
// }

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
    xfree(pDnxNode->hostname);
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
    gTopNode = dnxNodeListCreateNode("127.0.0.1", "localhost");
}

///Return a pointer to the end node in the list
// DnxNode* dnxNodeListEnd()
// {
//     DnxNode* pTopDnxNode = gTopNode;
//     DnxNode* pDnxNode = NULL;
//     if(pTopDnxNode)
//         pDnxNode = pTopDnxNode;
//     else
//         return pDnxNode;
// 
//     while(pDnxNode && pDnxNode->next != NULL)
//     {
//         pDnxNode = pDnxNode->next;
//     }
//     return pDnxNode;
// }

///Return a pointer to the beginning node in the list
// DnxNode* dnxNodeListBegin()
// {
//     DnxNode* pBottomNode = gTopNode;
//     DnxNode* pDnxNode = NULL;
// 
//     if(pBottomNode->prev)
//     {
//         pDnxNode = pBottomNode->prev;
//     } else {
//         return pBottomNode;
//     }
// 
//     while(pDnxNode->prev != NULL)
//     {
//         pDnxNode = pDnxNode->prev;
//     }
//     return pDnxNode;
// }

///Find a node by it's IP address
DnxNode* dnxNodeListFindNode(char* address)
{
    //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
    assert(address);
    assert(isalnum(*address));

    //dnxDebug(3,"Attempting to find node at %s\n",address);
    if(!gTopNode)
        return NULL;
        
    DnxNode* pDnxNode = gTopNode;

    while(pDnxNode && strcmp(pDnxNode->address,address) != 0) {
        pDnxNode = pDnxNode->next;
    }

    return pDnxNode;
}

///Count the nodes in the list
int dnxNodeListCountNodes()
{
    int count = 0;
    DnxNode* pDnxNode = gTopNode;
    if(pDnxNode) {
        do {
             count++;
             dnxLog("Counting node at %s\n",pDnxNode->address);
        } while(pDnxNode = pDnxNode->next);
    }
    return count;
}


/** Function to increment member values
*   @param address  - The IP address of the node you want
*   @param  member - The name of the member you want to increment
*/
unsigned dnxNodeListIncrementNodeMember(char* address, int member)
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

    } else {
        dnxDebug(1,"dnxNodeListIncrementNodeMember: Tried to increment stat %i for non-existent node ADDRESS: %s proceeding to create node",member,address);
        dnxLog("dnxNodeListIncrementNodeMember: Tried to increment stat %i for non-existent node ADDRESS: %s proceeding to create node",member,address);
//         dnxNodeListCreateNode(address);
//         retval = dnxNodeListIncrementNodeMember(address,member);
    }

    return(retval);
}

/** Function to set member values
*   @param address  - The IP address of the node you want
*   @param  hostname  - the value you want to set member to
*/
// unsigned long long dnxNodeListSetNodeAffinity(char* address, char* hostname)
// {
//     //If the IP address is NULL or corrupted it can cause nastiness later on, lets catch it here.
//     assert(address && isalnum(*address));
// 
// //     DnxNode* pDnxNode = dnxNodeListFindNode(address);
//     unsigned long long local_flag = dnxGetAffinity(hostname);
//     
//     if(pDnxNode) {
//         if(pDnxNode->flags == 0) {
//             DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);
//             pDnxNode->hostname = xstrdup(hostname);
//             pDnxNode->flags = local_flag;//dnxGetAffinity(hostname); 
//             DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);
//             dnxDebug(2, "dnxNodeListSetNodeAffinity: Address: [%s], Hostname: [%s], Flags: [%llu]",
//                 pDnxNode->address, pDnxNode->hostname, pDnxNode->flags);
//         }
//     } else {
//         dnxDebug(2, "dnxNodeListSetNodeAffinity: No exsisting node:: Address: [%s], Hostname: [%s]",
//             address, hostname);
// //         pDnxNode = dnxNodeListCreateNode(address);
//         DNX_PT_MUTEX_LOCK(&pDnxNode->mutex);
//         pDnxNode->hostname = xstrdup(hostname);
//         pDnxNode->flags = local_flag;//dnxGetAffinity(hostname);
//         DNX_PT_MUTEX_UNLOCK(&pDnxNode->mutex);
//         dnxDebug(2, "dnxNodeListSetNodeAffinity: Created Address: [%s], Hostname: [%s], Flags: [%llu]",
//             pDnxNode->address, pDnxNode->hostname, pDnxNode->flags);
//     }
//     return(pDnxNode->flags);
// }
