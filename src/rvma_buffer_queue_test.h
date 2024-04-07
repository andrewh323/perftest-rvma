//
// Created by Nicholas Chivaran on 2024-02-12.
//

#ifndef ELEC498_RVMA_BUFFER_QUEUE_TEST_H
#define ELEC498_RVMA_BUFFER_QUEUE_TEST_H

#include "rvma_buffer_queue.h"
#include "rvma_test_common.h"

void initRVMABufferQueueTest();

RVMA_testCounter* getBufferQueueTestCounter();

//createBufferQueue Tests

void test_createBufferQueueCap();

void test_createBufferQueueNoCap();

void test_createBufferQueueOneCap();

void test_createBufferQueueLargeCap();

//createBufferEntry Tests

void test_createBufferEntry();

void test_createBufferEntryEpochOps();

void test_createBufferEntryNullBuff();

void test_createBufferEntryZeroSize();

void test_createBufferEntryNegSize();

void test_createBufferEntryNullNotifPtr();

void test_createBufferEntryNullNotifLenPtr();

void test_createBufferEntryZeroThreshold();

//isFull Tests

void test_isFull();

void test_isFullNullQueue();

void test_isEmpty();

void test_isEmptyNullQueue();

//enqueue tests

void test_enqueue();

void test_enqueueNullQueue();

void test_enqueueNullEntry();

void test_enqueueRetiredBuffer();

void test_enqueueRetiredBufferNullQueue();

void test_enqueueRetiredBufferNullEntry();

//dequeue tests

void test_dequeue();

void test_dequeueNullQueue();

void test_dequeueNullEntry();

void test_dequeueNonexistentEntry();

//free tests
void test_freeBufferQueue();

void test_freeBufferQueueNull();

#endif //ELEC498_RVMA_BUFFER_QUEUE_TEST_H
