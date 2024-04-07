/***
 * General Description:
 * This file contains the testing functions for RVMA Mailbox (hashmap) and associated initialization and freeing functions
 *
 * Authors: Nicholas Chivaran, Samantha Hawco
 *
 * Reviewers: Ethan Shama, Nathan Kowal
 *
 ***/

#include "rvma_mailbox_hashmap_test.h"

// Global rvma_write testing variables
int mh_temp = 1;
int mh_temp2 = 2;
void* mh_test_vAddress = &mh_temp;
void* mh_test_vAddress2 = &mh_temp2;
int mh_test_hashmapCapacity = 256;
void ** mh_test_buffer;
int64_t mh_test_buffer_size;
void ** mh_test_notificationPtr;
void ** mh_test_notificationLenPtr;
int64_t mh_test_epochThreshold = 1;
epoch_type mh_test_epochType = EPOCH_BYTES;


RVMA_testCounter* mh_TestCounter;

// initialization of testing variables
void initRVMAMailboxHashmapTest(){
    mh_TestCounter = initTestCounter();
    setTestbench(mh_TestCounter, "Mailbox Hashmap");

    // create [8 rows x 64 col]buffer
    mh_test_buffer = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        mh_test_buffer[i] = (void *)malloc(64 * sizeof(void));
    }
    memset(mh_test_buffer, 0, sizeof(mh_test_buffer));

    mh_test_buffer_size = sizeof(mh_test_buffer);

    // create [8 rows x 1 col] notification buffer
    mh_test_notificationPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        mh_test_notificationPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(mh_test_notificationPtr, 0, sizeof(mh_test_notificationPtr));

    // create [8 rows x 1 col] notification len buffer
    mh_test_notificationLenPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        mh_test_notificationLenPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(mh_test_notificationLenPtr, 0, sizeof(mh_test_notificationLenPtr));
}

// access to local counter for output printing
RVMA_testCounter* getMailboxHashmapTestCounter(){
    return mh_TestCounter;
}

// RVMA_Mailbox_Hashmap Test Case 1 (TC-MH1)
void test_setupMailbox(){
    bool status = (setupMailbox(mh_test_vAddress, mh_test_hashmapCapacity) == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of mailboxPtr, creation of BQ, or creation of retired BQ
        printTestFailed(mh_TestCounter,"TC-MH1");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 2 (TC-MH2)
void test_initMailboxHashmap(){
    bool status = (initMailboxHashmap() == NULL) ? false : true;
    if (!status){ // failed test case due to failure in malloc of hashmap ptr or hashmap
        printTestFailed(mh_TestCounter,"TC-MH2");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 3 (TC-MH3)
void test_freeMailbox(){
    RVMA_Mailbox* test_mailbox = setupMailbox(mh_test_vAddress, mh_test_hashmapCapacity);
    RVMA_Mailbox** test_mailboxPtr = &test_mailbox;
    bool status = (freeMailbox(test_mailboxPtr) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH3");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 4 (TC-MH4)
void test_hashFunctionSameHash(){
    int res1 = hashFunction(mh_test_vAddress, mh_test_hashmapCapacity);
    int res2 = hashFunction(mh_test_vAddress, mh_test_hashmapCapacity);
    bool status = (res1 == res2) ? true : false;
    if (!status){ // failed test case due to inconsistent hash function
        printTestFailed(mh_TestCounter,"TC-MH4");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 5 (TC-MH5)
void test_hashFunctionDifferentHash(){
    int res1 = hashFunction(mh_test_vAddress, mh_test_hashmapCapacity);
    int res2 = hashFunction(mh_test_vAddress2, mh_test_hashmapCapacity);
    bool status = (res1 != res2) ? true : false;
    if (!status){ // failed test case due to inconsistent hash function
        printTestFailed(mh_TestCounter,"TC-MH5");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 6 (TC-MH6)
void test_newMailboxIntoHashmapNewMailbox(){
    Mailbox_HashMap* test_hashmapPtr = initMailboxHashmap();
    bool status = (newMailboxIntoHashmap(test_hashmapPtr, mh_test_vAddress) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH6");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 7 (TC-MH7)
void test_newMailboxIntoHashmapPresetMailbox(){
    Mailbox_HashMap* test_hashmapPtr = initMailboxHashmap();
    newMailboxIntoHashmap(test_hashmapPtr, mh_test_vAddress);
    bool status = (newMailboxIntoHashmap(test_hashmapPtr, mh_test_vAddress) == RVMA_ERROR) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH7");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 8 (TC-MH8)
void test_freeAllMailbox(){
    Mailbox_HashMap* test_hashmap = initMailboxHashmap();
    newMailboxIntoHashmap(test_hashmap, mh_test_vAddress);
    Mailbox_HashMap ** test_hashmapPtr = &test_hashmap;
    bool status = (freeAllMailbox(test_hashmapPtr) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH8");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 9 (TC-MH9)
void test_freeHashmap(){
    Mailbox_HashMap* test_hashmap = initMailboxHashmap();
    newMailboxIntoHashmap(test_hashmap, mh_test_vAddress);
    Mailbox_HashMap ** test_hashmapPtr = &test_hashmap;
    bool status = (freeHashmap(test_hashmapPtr) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH9");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 10 (TC-MH10)
void test_searchHashmapNullHashmap(){
    bool status = (searchHashmap(NULL, mh_test_vAddress) == NULL) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH10");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 11 (TC-MH11)
void test_searchHashmapNullKey(){
    Mailbox_HashMap* test_hashmapPtr = initMailboxHashmap();
    bool status = (searchHashmap(test_hashmapPtr, NULL) == NULL) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH11");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 12 (TC-MH12)
void test_searchHashmapNoMailbox(){
    Mailbox_HashMap* test_hashmapPtr = initMailboxHashmap();
    bool status = (searchHashmap(test_hashmapPtr, mh_test_vAddress) == NULL) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH12");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}

// RVMA_Mailbox_Hashmap Test Case 13 (TC-MH13)
void test_searchHashmapCorrect(){
    Mailbox_HashMap* test_hashmapPtr = initMailboxHashmap();
    newMailboxIntoHashmap(test_hashmapPtr, mh_test_vAddress);
    bool status = (searchHashmap(test_hashmapPtr, mh_test_vAddress) != NULL) ? true : false;
    if (!status){ // failed test case
        printTestFailed(mh_TestCounter,"TC-MH13");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);

}

// RVMA_Mailbox_Hashmap Test Case 14 (TC-MH14)
void test_retireBuffer(){
    RVMA_Mailbox* test_mailboxPtr = setupMailbox(mh_test_vAddress, mh_test_hashmapCapacity);
    RVMA_Buffer_Entry* test_bufferEntryPtr = createBufferEntry(mh_test_buffer, mh_test_buffer_size,
                                                               mh_test_notificationPtr, mh_test_notificationLenPtr,
                                                               mh_test_epochThreshold, mh_test_epochType);
    enqueue(test_mailboxPtr->bufferQueue, test_bufferEntryPtr);
    bool status = (retireBuffer(test_mailboxPtr, test_bufferEntryPtr) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case due to failure during move from BQ to retired BQ
        printTestFailed(mh_TestCounter,"TC-MH14");
        testFailed(mh_TestCounter);
    }
    testFinished(mh_TestCounter);
}