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

RVMA_Win* rvmaInitWindowMailboxKey(void *virtualAddress, __key_t key);

RVMA_Win* rvmaInitWindowMailbox(void *virtualAddress);

RVMA_Win* rvmaInitWindow();

RVMA_Status rvmaAddMailboxtoWindow(RVMA_Win* window, void *virtualAddress, __key_t key);

RVMA_Status rvmaSetKey(RVMA_Win* win, __key_t key);

RVMA_Status rvmaCloseWin(RVMA_Win*);

int64_t rvmaWinGetEpoch(RVMA_Win*);

RVMA_Status rvmaPostBuffer(void **buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, void *virtualAddress, RVMA_Win* window, int64_t epochThreshold, epoch_type epochType);

int rvmaPutHybrid(struct ibv_qp* qp, int index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);

RVMA_Status rvmaSend(void *buf, int64_t size, void *vaddr, RVMA_Win *window);

RVMA_Status rvmaRecv(void *vaddr, RVMA_Win *window);

RVMA_Status eventCompleted(struct ibv_wc *wc, RVMA_Win *win, void* virtualAddress);

RVMA_Status rvmaCheckBufferQueue(RVMA_Buffer_Queue *bufferQueue, TestType type, int msgSize);

#endif //ELEC498_RVMA_H
