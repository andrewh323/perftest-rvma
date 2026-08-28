//
// Created by Ethan Shama on 2024-01-23.
//

#ifndef ELEC498_RVMA_H
#define ELEC498_RVMA_H

#include "rvma_common.h"
#include "rvma_mailbox_hashmap.h"
#include "rvma_buffer_queue.h"

#include <infiniband/verbs.h>

typedef struct {
    Mailbox_HashMap *hashMapPtr;
    __key_t key;
} RVMA_Win;

double get_cpu_ghz();

RVMA_Win* rvmaInitWindowMailboxKey(uint64_t virtualAddress, __key_t key);

RVMA_Win* rvmaInitWindowMailbox(uint64_t virtualAddress);

RVMA_Win* rvmaInitWindow();

RVMA_Status rvmaAddMailboxtoWindow(RVMA_Win* window, uint64_t virtualAddress, __key_t key);

RVMA_Status rvmaSetKey(RVMA_Win* win, __key_t key);

RVMA_Status rvmaCloseWin(RVMA_Win*);

int64_t rvmaWinGetEpoch(RVMA_Win*);

RVMA_Buffer_Entry* rvmaPostBuffer(void *buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, uint64_t virtualAddress, RVMA_Mailbox *mailbox, int64_t epochThreshold, epoch_type epochType);

// Default per-buffer cap for postSendPool/postRecvPool callers that hold
// whole reassembled messages (e.g. stream sockets) rather than single
// wire-sized fragments.
#define RVMA_DEFAULT_MAX_BUF_SIZE (1024*1024)

RVMA_Status postSendPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType,
    size_t max_buf_size);

RVMA_Status postRecvPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType,
    size_t max_buf_size);

RVMA_Status rvmaSend(void *buf, int64_t size, uint64_t vaddr, RVMA_Mailbox *mailbox);

RVMA_Status rvmaRecv(uint64_t vaddr, void *buf, size_t len, int flags, RVMA_Mailbox *mailbox);

#endif //ELEC498_RVMA_H
