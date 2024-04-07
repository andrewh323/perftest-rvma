/***
 * General Description:
 * This file contains RVMA buffer queue structure functions and associated initialization and freeing functions
 *
 * Authors: Ethan Shama, Nathan Kowal
 *
 * Reviewers: Nicholas Chivaran, Samantha Hawco
 *
 ***/

#include "rvma_write.h"

RVMA_Buffer_Queue* createBufferQueue(int capacity) {
    RVMA_Buffer_Queue *queue_ptr;
    queue_ptr = (RVMA_Buffer_Queue *) malloc(sizeof(RVMA_Buffer_Queue));
    if(!queue_ptr) {
        print_error("createBufferQueue: Buffer Queue couldn't be allocated");
        return NULL;
    }

    queue_ptr->capacity = capacity;
    queue_ptr->start = 0;
    queue_ptr->size = 0;
    queue_ptr->end = capacity - 1;
    queue_ptr->pBufferEntry = (RVMA_Buffer_Entry**) malloc(capacity * sizeof(RVMA_Buffer_Entry*));
    if(!queue_ptr->pBufferEntry) {
        print_error("createBufferQueue: Buffer Entry list couldn't be allocated");
        free(queue_ptr);
        return NULL;
    }
    memset(queue_ptr->pBufferEntry, 0, capacity * sizeof(RVMA_Buffer_Entry*));

    return queue_ptr;
}

RVMA_Buffer_Entry* createBufferEntry(void **buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, int64_t epochThreshold, epoch_type epochType){
    if(buffer == NULL){
        print_error("createBufferEntry: Buffer is null");
        return  NULL;
    }
    if(size <= 0){
        print_error("createBufferEntry: size is 0 or less");
        return  NULL;
    }
    if(notificationPtr == NULL){
        print_error("createBufferEntry: notificationPtr is null");
        return  NULL;
    }
    if(notificationLenPtr == NULL){
        print_error("createBufferEntry: notificationLenPtr is null");
        return  NULL;
    }
    if(epochThreshold <= 0){
        print_error("createBufferEntry: epochThreshold is 0 or less");
        return  NULL;
    }
    if(epochType != EPOCH_BYTES && epochType != EPOCH_OPS){
        print_error("createBufferEntry: epochType is not a valid entry");
        return  NULL;
    }

    RVMA_Buffer_Entry* entry;
    entry = (RVMA_Buffer_Entry*)malloc(sizeof(RVMA_Buffer_Entry));
    if(entry == NULL){
        print_error("createBufferEntry: Buffer entry couldn't be allocated");
        return  NULL;
    }

    entry->realBuffAddr = buffer;
    entry->realBuffSize = size;
    entry->epochCount = 0;
    entry->epochThreshold = epochThreshold;
    entry->epochType = epochType;
    entry->notifBuffPtrAddr = notificationPtr;
    entry->notifLenPtrAddr = notificationLenPtr;

    return entry;
}

RVMA_Status isFull(RVMA_Buffer_Queue* queue){
    if(queue == NULL){
        print_error("isFull: queue is null");
        return RVMA_ERROR;
    }
    if(queue->size == queue->capacity) return RVMA_TRUE;
    else return RVMA_FALSE;
}

RVMA_Status isEmpty(RVMA_Buffer_Queue* queue){
    if(queue == NULL){
        print_error("isEmpty: queue is null");
        return RVMA_ERROR;
    }
    if(queue->size == 0) return RVMA_TRUE;
    else return RVMA_FALSE;
}

RVMA_Status enqueue(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry){
    if(queue == NULL){
        print_error("enqueue: queue is null");
        return RVMA_FAILURE;
    }
    if(entry == NULL){
        print_error("enqueue: entry is null");
        return RVMA_FAILURE;
    }

    if(isFull(queue) == RVMA_TRUE) return RVMA_QUEUE_FULL;

    queue->end = (queue->end + 1) % queue->capacity;
    queue->pBufferEntry[queue->end] = entry;
    entry = NULL;

    queue->size = queue->size + 1;

    return RVMA_SUCCESS;
}

RVMA_Status enqueueRetiredBuffer(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry){
    if(queue == NULL){
        print_error("enqueue: queue is null");
        return RVMA_FAILURE;
    }
    if(entry == NULL){
        print_error("enqueue: entry is null");
        return RVMA_FAILURE;
    }
    
    RVMA_Buffer_Entry** temp = realloc(queue->pBufferEntry, (queue->capacity+1) * sizeof(RVMA_Buffer_Entry*));
    if (temp == NULL) {
        print_error("enqueueRetiredBuffer: Failed to allocate more memory for retired queue.");
        return RVMA_FAILURE;
    } else {
        queue->pBufferEntry = temp;
    }

    queue->capacity = queue->capacity+1;
    queue->end = queue->end + 1;
    queue->pBufferEntry[queue->end] = entry;
    entry = NULL;
    queue->size = queue->size + 1;

    return RVMA_SUCCESS;
}

RVMA_Status dequeue(RVMA_Buffer_Queue* queue, RVMA_Buffer_Entry* entry){
    if(queue == NULL){
        print_error("dequeue: queue is null");
        return RVMA_FAILURE;
    }
    if(entry == NULL){
        print_error("dequeue: entry is null");
        return RVMA_FAILURE;
    }

    if(isEmpty(queue) == RVMA_TRUE) return RVMA_FAILURE;

    int found = 0;
    for(int i = 0; i < queue->capacity; i++){
        int idx = queue->start + i % queue->capacity;

        if(entry == queue->pBufferEntry[idx]){
            queue->pBufferEntry[idx] = NULL;
            found = 1;
            break;
        }
    }

    if(!found){
        return RVMA_FAILURE;
    }

    queue->size = queue->size - 1;

    return RVMA_SUCCESS;
}

RVMA_Status freeBufferEntry(RVMA_Buffer_Entry *entry)
{
    if(entry == NULL)
    {
        print_error("freeBufferEntry: entry is null");
        return RVMA_ERROR;
    }


    if (entry->realBuffAddr){
        if(munlock(entry->realBuffAddr, entry->realBuffSize * sizeof(entry->realBuffAddr)) == -1)
            print_error("rvmaPostBuffer: buffer memory couldn't be unpinned");
        free(entry->realBuffAddr);
        entry->realBuffAddr = NULL;
    }

    free(entry);
    entry = NULL;

    return RVMA_SUCCESS;
}

RVMA_Status freeBufferQueue(RVMA_Buffer_Queue* queue)
{
    if(queue == NULL)
    {
        print_error("freeBufferQueue: queue is null");
        return RVMA_ERROR;
    }

    if(queue->capacity != 0 || queue->size != 0){
        int flag = 1;
        int count = 0;
        int idx = queue->start;
        while(flag)
        {
            if(queue->pBufferEntry[idx])
            {
                freeBufferEntry(queue->pBufferEntry[idx]);
                queue->pBufferEntry[idx] = NULL;
                count++;
            }

            idx = (idx + 1) % queue->capacity;
            if(count == queue->size)
            {
                flag = 0;
            }
        }
    }

    free(queue->pBufferEntry);
    queue->pBufferEntry = NULL;
    free(queue);
    queue = NULL;

    return RVMA_SUCCESS;
}
