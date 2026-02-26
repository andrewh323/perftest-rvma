/***
 * General Description:
 * This file contains RVMA Mailbox (hashmap) functions and associated initialization and freeing functions
 *
 * Authors: Ethan Shama, Nathan Kowal
 *
 * Reviewers: Nicholas Chivaran, Samantha Hawco
 *
 ***/

#include "rvma_mailbox_hashmap.h"
#include <rdma/rdma_cma.h>


RVMA_Mailbox* setupMailbox(uint64_t vaddr, int hashmapCapacity){
    RVMA_Mailbox *mailboxPtr;
    mailboxPtr = (RVMA_Mailbox*) malloc(sizeof(RVMA_Mailbox));

    if(!mailboxPtr) return NULL;

    RVMA_Buffer_Queue *bufferQueue;
    bufferQueue = createBufferQueue(QUEUE_CAPACITY);
    if(!bufferQueue) {
        print_error("setupMailbox: Buffer Queue failed to be created");
        free(mailboxPtr);
        return NULL;
    }

    RVMA_Buffer_Queue *retiredBufferQueue;
    retiredBufferQueue = createBufferQueue(1);
    if(!retiredBufferQueue) {
        print_error("setupMailbox: Retired Buffer Queue failed to be created");
        free(mailboxPtr);
        free(bufferQueue);
        return NULL;
    }

    mailboxPtr->pd = NULL;
    mailboxPtr->cq = NULL;
    mailboxPtr->qp = NULL;
    mailboxPtr->bufferQueue = bufferQueue;
    mailboxPtr->retiredBufferQueue = retiredBufferQueue;
    mailboxPtr->vaddr = vaddr;
    mailboxPtr->key = hashFunction(mailboxPtr->vaddr, hashmapCapacity);

    return mailboxPtr;
}

Mailbox_HashMap* initMailboxHashmap(){
    Mailbox_HashMap* hashmapPtr;
    hashmapPtr = (Mailbox_HashMap*)malloc(sizeof(Mailbox_HashMap));
    if(!hashmapPtr) {
        print_error("initMailboxHashmap: hashmap failed to be allocated");
        return NULL;
    }

    hashmapPtr->capacity = HASHMAP_CAPACITY;
    hashmapPtr->numOfElements = 0;

    hashmapPtr->hashmap = (RVMA_Mailbox**)malloc(sizeof(RVMA_Mailbox*) * hashmapPtr->capacity);
    if(!hashmapPtr->hashmap) {
        print_error("initMailboxHashmap: mailboxs failed to be allocated");
        free(hashmapPtr);
        return NULL;
    }
    else{
        memset(hashmapPtr->hashmap, 0, hashmapPtr->capacity * sizeof(RVMA_Mailbox*));
    }

    return hashmapPtr;
}

RVMA_Status freeMailbox(RVMA_Mailbox** mailboxPtr){
    if (mailboxPtr && *mailboxPtr) {
        // Here you should also properly free your bufferQueues, which inside them free the possibly allocated buffers
        if ((*mailboxPtr)->bufferQueue) {
            freeBufferQueue(((*mailboxPtr)->bufferQueue));
        }
        if ((*mailboxPtr)->retiredBufferQueue) {
            freeBufferQueue(((*mailboxPtr)->retiredBufferQueue));
        }
        free(*mailboxPtr);
        *mailboxPtr = NULL;
    }
    return RVMA_SUCCESS;
}

RVMA_Status freeAllMailbox(Mailbox_HashMap** hashmapPtr){

    for(int i = 0; i < (*hashmapPtr)->capacity; i++){
        if((*hashmapPtr)->hashmap[i]) {
            if ((*hashmapPtr)->hashmap[i]->key == i) {
                freeMailbox(&((*hashmapPtr)->hashmap[i]));
            } else {
                (*hashmapPtr)->hashmap[i] = NULL;
            }
        }
    }

    free((*hashmapPtr)->hashmap);
    free(*hashmapPtr);
    *hashmapPtr = NULL;

    return RVMA_SUCCESS;
}

RVMA_Status freeHashmap(Mailbox_HashMap** hashmapPtr){

    if(hashmapPtr && *hashmapPtr){
        freeAllMailbox(hashmapPtr);
        free(*hashmapPtr);
        *hashmapPtr = NULL;
    }

    return RVMA_SUCCESS;
}

int hashFunction(uint64_t vaddr, int capacity) {
    uint64_t largePrime = 11400714819323198485ULL;
    uint64_t hash = vaddr * largePrime;
    return (int) (hash % capacity);
}

RVMA_Status newMailboxIntoHashmap(Mailbox_HashMap* hashMap, uint64_t vaddr){
    int hashNum = hashFunction(vaddr, hashMap->capacity);
    RVMA_Mailbox* mailboxPtr;
    mailboxPtr = setupMailbox(vaddr, hashMap->capacity);

    if (hashMap->hashmap[hashNum] != NULL) {
        freeMailbox(&mailboxPtr);
        print_error("newMailboxIntoHashmap: Virtual address hashed to same hash number, Virtual address rejected....");
        return RVMA_ERROR;
    }
    else {
        hashMap->hashmap[hashNum] = mailboxPtr;
        hashMap->numOfElements = hashMap->numOfElements + 1;
        return RVMA_SUCCESS;
    }
}

RVMA_Mailbox* searchHashmap(Mailbox_HashMap* hashMap, uint64_t key){

    if(hashMap == NULL) {
        print_error("searchHashmap: hashmap is null");
        return NULL;
    }
    if(key == NULL) {
        print_error("searchHashmap: key is null");
        return NULL;
    }

    // Getting the bucket index for the given key
    int hashNum = hashFunction(key, hashMap->capacity);

    // Head of the linked list present at bucket index
    RVMA_Mailbox* mailboxPtr = hashMap->hashmap[hashNum];
    if (mailboxPtr && mailboxPtr->vaddr == key) {
        return mailboxPtr;
    }
    else{
        // If no key found in the hashMap equal to the given key
        print_error("searchHashmap: No Key in Hashmap matches provided key");
        return NULL;
    }
}

RVMA_Status retireBuffer(RVMA_Mailbox* RVMA_Mailbox, RVMA_Buffer_Entry* entry){

    dequeue(RVMA_Mailbox->bufferQueue);

    return enqueueRetiredBuffer(RVMA_Mailbox->retiredBufferQueue, entry);
}

int establishMailboxConnection(RVMA_Mailbox *mailboxPtr, struct sockaddr_in *remote_addr) {
    struct rdma_cm_event *event;

    // Resolve address
    if (rdma_resolve_addr(mailboxPtr->cm_id, NULL, (struct sockaddr *)remote_addr, 2000)) {
        perror("rdma_resolve_addr");
        return -1;
    }
    if (rdma_get_cm_event(mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        fprintf(stderr, "rdma_resolve_addr failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    // Resolve route
    if (rdma_resolve_route(mailboxPtr->cm_id, 2000)) {
        perror("rdma_resolve_route");
        return -1;
    }
    if (rdma_get_cm_event(mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        fprintf(stderr, "rdma_resolve_route failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    rdma_ack_cm_event(event);

    // Create protection domain
    if (!mailboxPtr->pd) {
        mailboxPtr->pd = ibv_alloc_pd(mailboxPtr->cm_id->verbs);
        if (!mailboxPtr->pd) {
            perror("ibv_alloc_pd failed");
            return -1;
        }
    }

    // Define completion queue
    mailboxPtr->cq = ibv_create_cq(mailboxPtr->cm_id->verbs, 16, NULL, NULL, 0);
    if (!mailboxPtr->cq) {
        perror("ibv_create_cq failed");
        return -1;
    }

    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = mailboxPtr->cq,
        .recv_cq = mailboxPtr->cq,
        .qp_type = IBV_QPT_RC, // Reliable connection
        .sq_sig_all = 1,
        .cap = {
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    if(rdma_create_qp(mailboxPtr->cm_id, mailboxPtr->pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }

    mailboxPtr->qp = mailboxPtr->cm_id->qp;

    // Connect
    if (rdma_connect(mailboxPtr->cm_id, NULL)) {
        perror("rdma_connect");
        return -1;
    }
    if (rdma_get_cm_event(mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ESTABLISHED) {
        fprintf(stderr, "rdma_connect failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    rdma_ack_cm_event(event);

    printf("Mailbox connected successfully!\n");

    return 0;
}