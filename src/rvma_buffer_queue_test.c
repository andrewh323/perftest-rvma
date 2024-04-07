/***
 * General Description:
 * This file contains the testing functions for RVMA buffer queue structure and associated initialization and freeing functions
 *
 * Authors: Nicholas Chivaran, Samantha Hawco
 *
 * Reviewers: Ethan Shama, Nathan Kowal
 *
 ***/

#include "rvma_buffer_queue_test.h"

RVMA_testCounter* bq_TestCounter;
int bq_test_queueCapExpected = 256;
int bq_test_queueCapLarge = 15360;
void ** bq_test_buffer;
void ** bq_test_buffer_2;
int64_t bq_test_buffer_size;
int64_t bq_test_buffer_size_2;
void ** bq_test_notificationPtr;
void ** bq_test_notificationLenPtr;
int64_t bq_test_epochThreshold = 1;
epoch_type bq_test_epochType = EPOCH_BYTES;
RVMA_Buffer_Queue* bq_test_queue;
RVMA_Buffer_Entry* bq_test_entry;
RVMA_Buffer_Entry* bq_test_entry_2;
    
void initRVMABufferQueueTest(){
    // insert initialization of testing variables here
    bq_TestCounter = initTestCounter();
    setTestbench(bq_TestCounter, "Buffer_Queue");

    // create [8 rows x 64 col]buffer
    bq_test_buffer = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        bq_test_buffer[i] = (void *)malloc(64 * sizeof(void));
    }
    memset(bq_test_buffer, 0, sizeof(bq_test_buffer));
    bq_test_buffer_size = sizeof(bq_test_buffer);

    bq_test_buffer_2 = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        bq_test_buffer_2[i] = (void *)malloc(64 * sizeof(void));
    }
    memset(bq_test_buffer_2, 0, sizeof(bq_test_buffer_2));
    bq_test_buffer_size_2 = sizeof(bq_test_buffer_2);

    // create [8 rows x 1 col] notification buffer
    bq_test_notificationPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        bq_test_notificationPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(bq_test_notificationPtr, 0, sizeof(bq_test_notificationPtr));
    
    // create [8 rows x 1 col] notification len buffer
    bq_test_notificationLenPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        bq_test_notificationLenPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(bq_test_notificationLenPtr, 0, sizeof(bq_test_notificationLenPtr));


    bq_test_queue = createBufferQueue(bq_test_queueCapExpected);
    bq_test_entry = createBufferEntry(bq_test_buffer, bq_test_buffer_size, bq_test_notificationPtr, 
                                bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType);
    bq_test_entry_2 = createBufferEntry(bq_test_buffer_2, bq_test_buffer_size_2, bq_test_notificationPtr, 
                                bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType);

}

// access to local counter for output printing
RVMA_testCounter* getBufferQueueTestCounter(){
    return bq_TestCounter;
}


void test_createBufferQueueNoCap(){
    RVMA_Buffer_Queue* bq_temp_queue = createBufferQueue(0);
    bool status = (bq_temp_queue == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of buffer queue with capactiy of 0
        printTestFailed(bq_TestCounter,"TC-BQ1");
        testFailed(bq_TestCounter);
        freeBufferQueue(bq_temp_queue);
    }
    testFinished(bq_TestCounter);
}

void test_createBufferQueueOneCap(){
    RVMA_Buffer_Queue* bq_temp_queue = createBufferQueue(1);
    bool status = (bq_temp_queue == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of buffer queue with capacity of 1
        printTestFailed(bq_TestCounter,"TC-BQ2");
        testFailed(bq_TestCounter);
        freeBufferQueue(bq_temp_queue);
    }
    testFinished(bq_TestCounter);
}

void test_createBufferQueueCap(){
    RVMA_Buffer_Queue* bq_temp_queue =createBufferQueue(bq_test_queueCapExpected);
    bool status = (bq_temp_queue == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of buffer queue with capacity of 256
        printTestFailed(bq_TestCounter,"TC-BQ3");
        testFailed(bq_TestCounter);
        freeBufferQueue(bq_temp_queue);
    }
    testFinished(bq_TestCounter);
}

void test_createBufferQueueLargeCap(){
    RVMA_Buffer_Queue* bq_temp_queue =createBufferQueue(bq_test_queueCapLarge);
    bool status = (bq_temp_queue == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of buffer queue with larger than expected capacity
        printTestFailed(bq_TestCounter,"TC-BQ4");
        testFailed(bq_TestCounter);
        freeBufferQueue(bq_temp_queue);
    }
    testFinished(bq_TestCounter);
}

void test_createBufferEntry(){
    RVMA_Buffer_Entry *entry;
    entry = createBufferEntry(bq_test_buffer, bq_test_buffer_size, bq_test_notificationPtr, 
                                bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType);
    bool status = (entry == NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ5");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->realBuffAddr != bq_test_buffer) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ6: bufferAddr saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->realBuffSize != bq_test_buffer_size) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ7: BuffSize saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->epochCount != 0) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ8: epochCount saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->epochThreshold != bq_test_epochThreshold) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ9: epochThreshold saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->epochType != bq_test_epochType) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ10: epochType saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->notifBuffPtrAddr != bq_test_notificationPtr) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ11: notifBuffPtrAddr saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->notifLenPtrAddr != bq_test_notificationLenPtr) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ12: notifLenPtr saved incorrectly.");
        testFailed(bq_TestCounter);
    }
    
    testFinished(bq_TestCounter);
}

void test_createBufferEntryEpochOps(){
    RVMA_Buffer_Entry *entry;
    entry = createBufferEntry(bq_test_buffer, bq_test_buffer_size, bq_test_notificationPtr, 
                                bq_test_notificationLenPtr, bq_test_epochThreshold, EPOCH_OPS);
    bool status = (entry == NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ13");
        testFailed(bq_TestCounter);
    }
    testFinished(bq_TestCounter);

    status = (entry->epochType !=  EPOCH_OPS) ? false : true;
    if (!status){ 
        printTestFailed(bq_TestCounter,"TC-BQ14: epochType saved incorrectly.");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryNullBuff(){
    bool status = (createBufferEntry(NULL, bq_test_buffer_size, bq_test_notificationPtr, 
                    bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ15");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryZeroSize(){
    bool status = (createBufferEntry(bq_test_buffer, 0 , bq_test_notificationPtr, 
                    bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ16");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryNegSize(){
    bool status = (createBufferEntry(bq_test_buffer, -1 , bq_test_notificationPtr, 
                    bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ17");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryNullNotifPtr(){
    bool status = (createBufferEntry(bq_test_buffer, bq_test_buffer_size , NULL, 
                    bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ18");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryNullNotifLenPtr(){
    bool status = (createBufferEntry(bq_test_buffer, bq_test_buffer_size , bq_test_notificationPtr, 
                    NULL, bq_test_epochThreshold, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ19");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_createBufferEntryZeroThreshold(){
    bool status = (createBufferEntry(bq_test_buffer, bq_test_buffer_size , bq_test_notificationPtr, 
                    bq_test_notificationLenPtr, 0, bq_test_epochType) != NULL) ? false : true;
    if (!status){ // failed test case due to failure of creating buffer entry
        printTestFailed(bq_TestCounter,"TC-BQ20");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_isFull(){
    enqueue(bq_test_queue, bq_test_entry);
    bool status = (isFull(bq_test_queue) == RVMA_ERROR) ? false : true;
    if (!status){ // failed test case due to failure checking fullness of buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ21");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_isFullNullQueue(){
    bool status = (isFull(NULL) != RVMA_ERROR) ? false : true;
    if (!status){ // failed test case due to failure checking fullness of buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ22");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_isEmpty(){
    bool status = (isEmpty(bq_test_queue) == RVMA_ERROR) ? false : true;
    if (!status){ // failed test case due to failure checking fullness of buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ23");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter); 
}

void test_isEmptyNullQueue(){
    bool status = (isFull(NULL) != RVMA_ERROR) ? false : true;
    if (!status){ // failed test case due to failure checking fullness of buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ24");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueue(){
    bool status = (enqueue(bq_test_queue, bq_test_entry) == RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ25");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueueNullQueue(){
    bool status = (enqueue(NULL, bq_test_entry) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ26");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueueNullEntry(){
    bool status = (enqueue(bq_test_queue, NULL) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ27");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueueRetiredBuffer(){
    bool status = (enqueueRetiredBuffer(bq_test_queue, bq_test_entry) == RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ28");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueueRetiredBufferNullQueue(){
    bool status = (enqueueRetiredBuffer(NULL, bq_test_entry) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ29");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_enqueueRetiredBufferNullEntry(){
    bool status = (enqueueRetiredBuffer(bq_test_queue, NULL) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to add entry to buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ30");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_dequeue(){
    bool status = (dequeue(bq_test_queue, bq_test_entry) == RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to remove entry from buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ31");
        testFailed(bq_TestCounter);
    }else{
        enqueue(bq_test_queue, bq_test_entry); //add entry back for consistancy (always want to have at least one element)
    }

    testFinished(bq_TestCounter);
}

void test_dequeueNullQueue(){
    bool status = (dequeue(NULL, bq_test_entry) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to remove entry from buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ32");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_dequeueNullEntry(){
    bool status = (dequeue(bq_test_queue, NULL) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to remove entry from buffer queue
        printTestFailed(bq_TestCounter,"TC-BQ33");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_dequeueNonexistentEntry(){
    bool status = (dequeue(bq_test_queue, bq_test_entry_2) != RVMA_FAILURE) ? false : true;
    if (!status){ // failed test case due to failure to catch error of nonexisitant entry
        printTestFailed(bq_TestCounter,"TC-BQ35: tried to remove nonexistant entry");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}

void test_freeBufferQueue(){
    RVMA_Buffer_Queue* bq_temp_queue = createBufferQueue(bq_test_queueCapExpected);
    RVMA_Buffer_Entry *entry = createBufferEntry(bq_test_buffer, bq_test_buffer_size, bq_test_notificationPtr, 
                                bq_test_notificationLenPtr, bq_test_epochThreshold, bq_test_epochType);
    enqueue(bq_temp_queue, entry);

    RVMA_Status result = freeBufferQueue(bq_temp_queue);
    bool status = (result != RVMA_SUCCESS) ? false : true;
    if (!status){ // failed test because queue not freed
        printTestFailed(bq_TestCounter,"TC-BQ36");
        testFailed(bq_TestCounter);
    }else{
        bq_temp_queue = NULL;
    }

    testFinished(bq_TestCounter);
}

void test_freeBufferQueueNull(){
    bool status = (freeBufferQueue(NULL) != RVMA_ERROR) ? false : true;
    if (!status){ // failed test because queue not freed
        printTestFailed(bq_TestCounter,"TC-BQ37");
        testFailed(bq_TestCounter);
    }

    testFinished(bq_TestCounter);
}