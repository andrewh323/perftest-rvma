/***
 * General Description:
 * This file contains the unit tests for the RVMA functions
 *
 * Authors: Nicholas Chivaran, Samantha Hawco
 *
 * Reviewers: Ethan Shama, Nathan Kowal
 *
 ***/

#include "rvma_write_test.h"

// Global rvma_write testing variables
int w_temp = 1;
void* w_test_vAddress = &w_temp;
__key_t w_test_key = 1;
__key_t w_test_key2 = 2;
void ** w_test_buffer;
int64_t w_test_buffer_size;
void ** w_test_notificationPtr;
void ** w_test_notificationLenPtr;
int64_t w_test_epochThreshold = 1;
epoch_type w_test_epochType = EPOCH_BYTES;

// temp variable for tracking whether a failed test occured (used for output handling)
RVMA_testCounter* w_TestCounter;


// initialization of testing variables
void initRVMAWriteTest(){

    w_TestCounter = initTestCounter();
    setTestbench(w_TestCounter, "Write");

    // create [8 rows x 64 col]buffer
    w_test_buffer = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        w_test_buffer[i] = (void *)malloc(64 * sizeof(void));
    }
    memset(w_test_buffer, 0, sizeof(w_test_buffer));

    w_test_buffer_size = sizeof(w_test_buffer);

    // create [8 rows x 1 col] notification buffer
    w_test_notificationPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        w_test_notificationPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(w_test_notificationPtr, 0, sizeof(w_test_notificationPtr));

    // create [8 rows x 1 col] notification len buffer
    w_test_notificationLenPtr = (void **)malloc(8 * sizeof(void *));
    for (int i = 0; i < 8; i++) {
        w_test_notificationLenPtr[i] = (void *)malloc(1 * sizeof(void));
    }
    memset(w_test_notificationLenPtr, 0, sizeof(w_test_notificationLenPtr));
}

// access to local counter for output printing
RVMA_testCounter* getWriteTestCounter(){
    return w_TestCounter;
}


// RVMA_Write Test Case 1 (TC-W1)
void test_RVMAInitWindowMailboxKey(){ //TC-W1
    bool status = (rvmaInitWindowMailboxKey(w_test_vAddress, w_test_key) == NULL) ? false : true;
    if (!status){ // failed test case due to failure in hashmap creation or adding a new mailbox to window
        printTestFailed(w_TestCounter,"TC-W1");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 2 (TC-W2)
void test_RVMAInitWindowMailbox(){
    bool status = (rvmaInitWindowMailbox(w_test_vAddress) == NULL) ? false : true;
    if (!status){ // failed test case due to failure in hashmap creation or adding a new mailbox to window
        printTestFailed(w_TestCounter,"TC-W2");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 3 (TC-W3)
void test_RVMAInitWindow(){ //TC-W3
    bool status = (rvmaInitWindow() == NULL) ? false : true;
    if (!status){ // failed test case due to failure in hashmap creation or adding a new mailbox to window
        printTestFailed(w_TestCounter,"TC-W3");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 4 (TC-W4)
void test_RVMASetKeyNullWindow(){
    bool status = (rvmaSetKey(NULL, w_test_key2) == RVMA_ERROR) ? true : false;
    if (!status){ // failed test case due to overwriting of window key
        printTestFailed(w_TestCounter,"TC-W4");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 5 (TC-W5)
void test_RVMASetKeyPresetKey(){
    RVMA_Win* test_windowPtr = rvmaInitWindowMailboxKey(w_test_vAddress, w_test_key);
    bool status = (rvmaSetKey(test_windowPtr, w_test_key2) == RVMA_FAILURE) ? true : false;
    if (!status){ // failed test case due to overwriting of window key
        printTestFailed(w_TestCounter,"TC-W5");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 6 (TC-W6)
void test_RVMASetKeyNewKey(){ //TC-W5
    RVMA_Win* test_windowPtr = rvmaInitWindowMailbox(w_test_vAddress);
    bool status = (rvmaSetKey(test_windowPtr, w_test_key2) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case due to not window key
        printTestFailed(w_TestCounter,"TC-W6");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 7 (TC-W7)
void test_RVMAAddMailboxtoWindowIncorrectKey(){ //TC-W6
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    rvmaSetKey(test_windowPtr, w_test_key);
    bool status = (rvmaAddMailboxtoWindow(test_windowPtr, w_test_vAddress, w_test_key2) == RVMA_FAILURE) ? true : false;
    if (!status){ // failed test case due to adding mailbox with incorrect key (improper access)
        printTestFailed(w_TestCounter,"TC-W7");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 8 (TC-W8)
void test_RVMAAddMailboxtoWindowCorrectKey(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    rvmaSetKey(test_windowPtr, w_test_key);
    bool status = (rvmaAddMailboxtoWindow(test_windowPtr, w_test_vAddress, w_test_key) == RVMA_SUCCESS) ? true : false;
    if (!status){ // failed test case due to failed insert of new mailbox into window
        printTestFailed(w_TestCounter,"TC-W8");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 9 (TC-W9)
void test_RVMACloseWin(){
    RVMA_Win* windowPtr = rvmaInitWindow();
    bool status = (rvmaCloseWin(windowPtr) == RVMA_SUCCESS) ? true: false; // close window
    if (!status){ // failed test case due to some error with closing
        printTestFailed(w_TestCounter,"TC-W9");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 10 (TC-W10)
void test_RVMAPostBufferInvalidBuffer(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(NULL, w_test_buffer_size, w_test_notificationPtr, w_test_notificationLenPtr,
                                 w_test_vAddress, test_windowPtr, w_test_epochThreshold,
                                 w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with NULL buffer
        printTestFailed(w_TestCounter,"TC-W10");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 11 (TC-W11)
void test_RVMAPostBufferInvalidBufferSize(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(w_test_buffer, -1, w_test_notificationPtr, w_test_notificationLenPtr,
                                  w_test_vAddress, test_windowPtr, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with buffer size < 0
        printTestFailed(w_TestCounter,"TC-W11");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 12 (TC-W12)
void test_RVMAPostBufferInvalidNotificationPtr(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(w_test_buffer, w_test_buffer_size, NULL, w_test_notificationLenPtr,
                                  w_test_vAddress, test_windowPtr, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with NULL notification pointer
        printTestFailed(w_TestCounter,"TC-W12");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 13 (TC-W13)
void test_RVMAPostBufferInvalidNotificationLenPtr(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(w_test_buffer, w_test_buffer_size, w_test_notificationPtr, NULL,
                                  w_test_vAddress, test_windowPtr, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with NULL notification length pointer
        printTestFailed(w_TestCounter,"TC-W13");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 14 (TC-W14)
void test_RVMAPostBufferInvalidVirtualAddress(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(w_test_buffer, w_test_buffer_size, w_test_notificationPtr, w_test_notificationLenPtr,
                                  NULL, test_windowPtr, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with NULL virtual address
        printTestFailed(w_TestCounter,"TC-W14");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 15 (TC-W15)
void test_RVMAPostBufferInvalidWindow(){
    RVMA_Win* test_windowPtr = rvmaInitWindow();
    bool status = (rvmaPostBuffer(w_test_buffer, w_test_buffer_size, w_test_notificationPtr, w_test_notificationLenPtr,
                                  w_test_vAddress, NULL, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_ERROR) ? true: false;

    if (!status){ // failed test case due to operation with NULL window
        printTestFailed(w_TestCounter,"TC-W15");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}

// RVMA_Write Test Case 16 (TC-W16)
void test_RVMAPostBufferCorrect(){
    RVMA_Win* test_windowPtr = rvmaInitWindowMailboxKey(w_test_vAddress, w_test_key);
    bool status = (rvmaPostBuffer(w_test_buffer, w_test_buffer_size, w_test_notificationPtr, w_test_notificationLenPtr,
                                  w_test_vAddress, test_windowPtr, w_test_epochThreshold,
                                  w_test_epochType) == RVMA_SUCCESS) ? true: false;

    if (!status){ // failed test case due to failure to find hashmap, create hashmap entry, pin/unpin memory, or add to BQ
        printTestFailed(w_TestCounter,"TC-W16");
        testFailed(w_TestCounter);
    }
    testFinished(w_TestCounter);
}
