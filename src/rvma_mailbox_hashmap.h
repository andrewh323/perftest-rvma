//
// Created by Ethan Shama on 2024-01-26.
//

#ifndef ELEC498_RVMA_MAILBOX_HASHMAP_H
#define ELEC498_RVMA_MAILBOX_HASHMAP_H

#include "rvma_common.h"
#include "rvma_buffer_queue.h"
#include <arpa/inet.h>

#define HASHMAP_CAPACITY 50

typedef struct {
    int key;
    void *virtualAddress;

    struct ibv_mr *mr; // Memory region
    struct ibv_pd *pd; // Protection domain
    struct ibv_cq *cq; // Completion queue
    struct ibv_qp *qp; // Queue pair
    struct rdma_cm_id *cm_id; // RDMA connection manager
    struct rdma_event_channel *ec; // Event channel
    struct ibv_context *ctx; // Device context
    int port_num; // Port number for datagrams
    int type; // Socket type (SOCK_STREAM or SOCK_DGRAM)
    uint64_t cycles; // Clock cycles for tsc timer;

    RVMA_Buffer_Queue *bufferQueue;
    RVMA_Buffer_Queue *retiredBufferQueue;
} RVMA_Mailbox;

typedef struct  {
    int numOfElements, capacity;
    RVMA_Mailbox** hashmap;
} Mailbox_HashMap;

RVMA_Mailbox* setupMailbox(void *virtualAddress, int hashmapCapacity);

RVMA_Status freeMailbox(RVMA_Mailbox** mailboxPtr);

RVMA_Status freeAllMailbox(Mailbox_HashMap** hashmapPtr);

Mailbox_HashMap* initMailboxHashmap();

RVMA_Status freeHashmap(Mailbox_HashMap** hashmapPtr);

int hashFunction(void *virtualAddress, int capacity);

RVMA_Status newMailboxIntoHashmap(Mailbox_HashMap* hashMap, void *virtualAddress);

RVMA_Status retireBuffer(RVMA_Mailbox* RVMA_Mailbox, RVMA_Buffer_Entry* entry);

RVMA_Mailbox* searchHashmap(Mailbox_HashMap* hashMap, void* key);

int establishMailboxConnection(RVMA_Mailbox *mailboxPtr, struct sockaddr_in *remote_addr);

#endif //ELEC498_RVMA_MAILBOX_HASHMAP_H
