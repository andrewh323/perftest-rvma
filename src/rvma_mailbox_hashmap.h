//
// Created by Ethan Shama on 2024-01-26.
//

#ifndef ELEC498_RVMA_MAILBOX_HASHMAP_H
#define ELEC498_RVMA_MAILBOX_HASHMAP_H

#include "rvma_common.h"
#include "rvma_buffer_queue.h"
#include <arpa/inet.h>

#define HASHMAP_CAPACITY 50
#define RVMA_MAX_BATCH 16

typedef struct {
    int key;
    int type;
    int sendCount;
    int recvCount;
    int max_outstanding_sends;
    int outstanding_sends;
    int posted_recvs;
    int max_recvs;

    void *send_pool;
    void *recv_pool;
    
    uint64_t vaddr;
    struct ibv_pd *pd; // Protection domain
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_qp *qp; // Queue pair
    struct ibv_mr *send_mr;
    struct ibv_mr *recv_mr;
    struct rdma_cm_id *cm_id; // RDMA connection manager
    struct rdma_event_channel *ec; // Event channel
    struct ibv_context *ctx; // Device context

    RVMA_Buffer_Queue *sendBufferQueue;
    RVMA_Buffer_Queue *inflightSendQueue;
    RVMA_Buffer_Queue *recvBufferQueue;
    RVMA_Buffer_Queue *completedRecvQueue;
    RVMA_Buffer_Queue *retiredBufferQueue;
} RVMA_Mailbox;

typedef struct  {
    int numOfElements, capacity;
    RVMA_Mailbox** hashmap;
} Mailbox_HashMap;

RVMA_Mailbox* setupMailbox(uint64_t vaddr, int hashmapCapacity);

RVMA_Status freeMailbox(RVMA_Mailbox** mailboxPtr);

RVMA_Status freeAllMailbox(Mailbox_HashMap** hashmapPtr);

Mailbox_HashMap* initMailboxHashmap();

RVMA_Status freeHashmap(Mailbox_HashMap** hashmapPtr);

int hashFunction(uint64_t vaddr, int capacity);

RVMA_Status newMailboxIntoHashmap(Mailbox_HashMap* hashMap, uint64_t vaddr);

RVMA_Status retireBuffer(RVMA_Mailbox* RVMA_Mailbox, RVMA_Buffer_Entry* entry);

RVMA_Mailbox* searchHashmap(Mailbox_HashMap* hashMap, uint64_t key);

int establishMailboxConnection(RVMA_Mailbox *mailboxPtr, struct sockaddr_in *remote_addr);

#endif //ELEC498_RVMA_MAILBOX_HASHMAP_H
