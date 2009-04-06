/***************************************************************************************
*@file  dnxNode.h
*@author Steven D.Morrey (smorrey@ldschurch.org)
*@c (c)2008 Intellectual Reserve, Inc.
*
*   The purpose of this file is to define a worker node instrumentation class.
***************************************************************************************/
#include "dnxTypes.h"

#ifndef DNXNODE
#define DNXNODE

enum
{
    JOBS_DISPATCHED,
    JOBS_HANDLED,
    JOBS_REJECTED_OOM,
    JOBS_REJECTED_NO_NODES,
    JOBS_REQ_RECV,
    JOBS_REQ_EXP,
    HOSTNAME,
    AFFINITY_FLAGS

};

/** DnxNodes are more than just simple structs for keeping track of IP addresses
* They are a linked list of worker nodes tied to relevant metrics
*/
typedef struct DnxNode
{

    struct DnxNode* next; //!< Next Node
    struct DnxNode* prev; //!< Previous Node
    char* address;  //!< IP address or URL of worker
    char* hostname; //!< Hostname defined in dnxClient.cfg
    unsigned long long flags; //!< Affinity flags assigned during init
    unsigned jobs_dispatched; //!< How many jobs have been sent to worker
    unsigned jobs_handled;   //!< How many jobs have been handled
    unsigned jobs_rejected_oom;  //!< How many jobs have been rejected due to memory
    unsigned jobs_rejected_no_nodes;  //!< How many jobs have been rejected due to no available nodes
    unsigned jobs_req_recv;  //!< How many job requests have been recieved from worker
    unsigned jobs_req_exp;  //!< How many job requests have expired
    pthread_mutex_t mutex;  //!< Thread locking control structure
} DnxNode;


/** Node Creation Function for DnxNodes
*   Create a new node and add it to the end of the list
*   @param address - IP address of the worker node
*   @param pTopDnxNode - Pointer to the node you want to add to
*   @return pDnxNode - Pointer to the newly created node
*
*   NOTE:  If a node already exists with that IP address, then
*   we return the existing node instead of creating a new node
*/
DnxNode* dnxNodeListCreateNode(char* address);

/** Node Destruction Function
*   This function can be used to remove the entire list
*/
void dnxNodeListDestroy();


/** Removal function for DnxNodes
*   Remove and delete a node.
*   Since all nodes are linked together in a list, this function will also heal the list
*   by pointing prev at next and vice versa
*   @param pDnxNode - A pointer to the node you want to remove
*   @return - A pointer to the next node in the list
*/
DnxNode* dnxNodeListRemoveNode(DnxNode* pDnxNode);

/** Removal function for DnxNodes
*   Remove and delete all nodes.
*   Then recreate gTopNode
*/
void dnxNodeListReset();

/** Return a pointer to the end node
*/
DnxNode* dnxNodeListEnd();

/** Return a pointer to the beginning node
*/
DnxNode* dnxNodeListBegin();

/** Find a node by it's IP address
*   @param address - The IP address of the node you want to find
*   @return - A pointer to the node if found, if not found it returns NULL
*/
DnxNode* dnxNodeListFindNode(char* address);

/** Count the nodes
*/
int dnxNodeListCountNodes();


/** Count a given member value from all nodes
*   Internal use function, use dnxNodeListCountX functions instead
*/
unsigned dnxNodeListCountValuesFromAllNodes(int member);

/** Return a member value from a single node
*   Internal use function, use dnxNodeListCountX functions instead
*/
unsigned dnxNodeListGetMemberValue(DnxNode* pDnxNode, int member);

/** Place holder function to determine if we want values from all nodes or just one
*   Internal use function, use dnxNodeListCountX functions instead
*/
unsigned dnxNodeListCount(char* address, int member);

unsigned dnxNodeListIncrementNodeMember(char* address,int member);

unsigned dnxNodeListSetNode(char* address, int member, void* value);

void dnxStatsRequestListener(void *vptr_args);

#endif

