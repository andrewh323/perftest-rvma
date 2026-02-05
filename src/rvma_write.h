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

RVMA_Buffer_Entry* rvmaPostBuffer(void *buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, uint64_t virtualAddress, RVMA_Mailbox *mailbox, int64_t epochThreshold, epoch_type epochType, int bufferType);

RVMA_Status postRecvPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType);

RVMA_Status rvmaSend(void *buf, int64_t size, uint64_t vaddr, RVMA_Mailbox *mailbox);

RVMA_Status rvmaRecv(uint64_t vaddr, RVMA_Mailbox *mailbox, uint64_t *recv_timestamp);

#endif //ELEC498_RVMA_H
