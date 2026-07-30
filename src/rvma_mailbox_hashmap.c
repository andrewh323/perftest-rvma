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
    RVMA_Mailbox *mb = (RVMA_Mailbox*) malloc(sizeof(RVMA_Mailbox));

    if(!mb) return NULL;

    mb->sendBufferQueue = createBufferQueue(QUEUE_CAPACITY);
    mb->inflightSendQueue = createBufferQueue(QUEUE_CAPACITY);
    mb->recvBufferQueue = createBufferQueue(QUEUE_CAPACITY);
    mb->completedRecvQueue = createBufferQueue(QUEUE_CAPACITY);

    if (!mb->sendBufferQueue   || !mb->inflightSendQueue ||
        !mb->recvBufferQueue   || !mb->completedRecvQueue) {
        print_error("setupMailbox: failed to allocate buffer queues");
        freeMailbox(&mb);  // freeMailbox must already NULL-check each field
        return NULL;
    }
    
    mb->max_outstanding_sends = 1000;
    mb->outstanding_sends = 0;
    mb->max_recvs = 512;
    mb->posted_recvs = 0;
    mb->sendCount = 0;
    mb->recvCount = 0;
    mb->vaddr = vaddr;
    mb->key = hashFunction(mb->vaddr, hashmapCapacity);

    return mb;
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

        if ((*mailboxPtr)->sendBufferQueue) {
            freeBufferQueue(((*mailboxPtr)->sendBufferQueue));
        }
        if ((*mailboxPtr)->recvBufferQueue) {
            freeBufferQueue(((*mailboxPtr)->recvBufferQueue));
        }

        if ((*mailboxPtr)->qp) ibv_destroy_qp((*mailboxPtr)->qp);

        if ((*mailboxPtr)->recv_mr) ibv_dereg_mr((*mailboxPtr)->recv_mr);
        if ((*mailboxPtr)->send_mr) ibv_dereg_mr((*mailboxPtr)->send_mr);

        if ((*mailboxPtr)->send_cq) ibv_destroy_cq((*mailboxPtr)->send_cq);
        if ((*mailboxPtr)->recv_cq) ibv_destroy_cq((*mailboxPtr)->recv_cq);

        if ((*mailboxPtr)->recv_pool) free((*mailboxPtr)->recv_pool);
        if ((*mailboxPtr)->send_pool) free((*mailboxPtr)->send_pool);

        if ((*mailboxPtr)->pd) ibv_dealloc_pd((*mailboxPtr)->pd);

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
    if (hashmapPtr && *hashmapPtr) {
        freeAllMailbox(hashmapPtr);
    }
    return RVMA_SUCCESS;
}

int hashFunction(uint64_t vaddr, int capacity) {
    uint64_t largePrime = 11400714819323198485ULL;
    uint64_t hash = vaddr * largePrime;
    return (int) (hash % capacity);
}

RVMA_Status newMailboxIntoHashmap(Mailbox_HashMap* hashMap, uint64_t vaddr){
    if (hashMap->numOfElements >= hashMap->capacity) {
        errno = ENOSPC;
        return RVMA_ERROR;
    }

    int start = hashFunction(vaddr, hashMap->capacity);

    for (int i = 0; i < hashMap->capacity; i++) {
        int slot = (start + i) % hashMap->capacity;
        
        if (hashMap->hashmap[slot] == NULL) { // Found a free slot
            RVMA_Mailbox* mb = setupMailbox(vaddr, hashMap->capacity);
            if (!mb) return RVMA_ERROR;
            mb->key = slot; // Record slot itself
            hashMap->hashmap[slot] = mb;
            hashMap->numOfElements++;
            return RVMA_SUCCESS;
        }

        if (hashMap->hashmap[slot]->vaddr == vaddr) {
            // Collision
            return RVMA_ERROR;
        }
    }

    errno = ENOSPC;
    return RVMA_ERROR;
}

RVMA_Mailbox* searchHashmap(Mailbox_HashMap* hashMap, uint64_t vaddr) {

    if(hashMap == NULL) {
        print_error("searchHashmap: hashmap is null");
        return NULL;
    }
    if(vaddr == NULL) {
        print_error("searchHashmap: key is null");
        return NULL;
    }

    // Getting the bucket index for the given key
    int start = hashFunction(vaddr, hashMap->capacity);

    for (int i = 0; i < hashMap->capacity; i++) {
        int slot = (start + i) % hashMap->capacity;
        if (hashMap->hashmap[slot] == NULL) return NULL;
        if (hashMap->hashmap[slot]->vaddr == vaddr) return hashMap->hashmap[slot];
    }
    // If no key found in the hashMap equal to the given vaddr
    print_error("searchHashmap: No mailbox with that vaddr found");
    return NULL;
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
    mailboxPtr->send_cq = ibv_create_cq(mailboxPtr->cm_id->verbs, 16, NULL, NULL, 0);
    if (!mailboxPtr->send_cq) {
        perror("ibv_create_cq failed");
        return -1;
    }
    mailboxPtr->recv_cq = ibv_create_cq(mailboxPtr->cm_id->verbs, 16, NULL, NULL, 0);
    if (!mailboxPtr->recv_cq) {
        perror("ibv_create_cq failed");
        return -1;
    }


    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = mailboxPtr->send_cq,
        .recv_cq = mailboxPtr->recv_cq,
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