//
// Created by Nathan Kowal on 2024-01-25.
//

#ifndef ELEC498_RVMA_BUFFER_QUEUE_H
#define ELEC498_RVMA_BUFFER_QUEUE_H

#include "rvma_common.h"

typedef enum {
    EPOCH_BYTES,
    EPOCH_OPS
} epoch_type;

typedef struct {
    void *realBuff;
    void **realBuffAddr;
    int64_t realBuffSize;
    int64_t epochCount;
    int64_t epochThreshold;
    epoch_type epochType;
    void **notifBuffPtrAddr;
    void **notifLenPtrAddr;
    struct ibv_mr *mr;
} RVMA_Buffer_Entry;

typedef struct {
    int start, end, size;
    int capacity;
    RVMA_Buffer_Entry** pBufferEntry;
} RVMA_Buffer_Queue;

RVMA_Buffer_Queue* createBufferQueue(int capacity);

RVMA_Buffer_Entry* createBufferEntry(void *buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, int64_t epochThreshold, epoch_type epochType);

RVMA_Status isFull(RVMA_Buffer_Queue* queue);

RVMA_Status isEmpty(RVMA_Buffer_Queue* queue);

RVMA_Status enqueue(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry);

RVMA_Status enqueueRetiredBuffer(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry);

RVMA_Buffer_Entry* dequeue(RVMA_Buffer_Queue* queue);

RVMA_Status removeEntry(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry);

RVMA_Status freeBufferEntry(RVMA_Buffer_Entry *entry);

RVMA_Status freeBufferQueue(RVMA_Buffer_Queue* queue);

#endif //ELEC498_RVMA_BUFFER_QUEUE_H
