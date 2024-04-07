/***
 * General Description:
 * This file tests and logs all the the units tests
 *
 * Authors: Nicholas Chivaran, Samantha Hawco
 *
 * Reviewers: Ethan Shama, Nathan Kowal
 *
 ***/

#include "rvma_buffer_queue_test.h"
#include "rvma_mailbox_hashmap_test.h"
#include "rvma_write_test.h"

int main(int argc, char *argv[]) {

    // global testing values used for tracking total number of test cases run/failed across all 3 RVMA tests
    int global_total = 0;
    int global_failed = 0;
    RVMA_testCounter* tbCtr;

    //set testing flag
    set_testingFlag(true);

    //RVMA Buffer Queue Tests
    initRVMABufferQueueTest();

    //TEST CASES
    test_createBufferQueueCap();
    test_createBufferQueueNoCap();
    test_createBufferQueueOneCap();
    test_createBufferQueueLargeCap();

    test_createBufferEntry();
    test_createBufferEntryEpochOps();
    test_createBufferEntryNullBuff();
    test_createBufferEntryZeroSize();
    test_createBufferEntryNegSize();
    test_createBufferEntryNullNotifPtr();
    test_createBufferEntryNullNotifLenPtr();
    test_createBufferEntryZeroThreshold();

    test_isFull();
    test_isFullNullQueue();
    test_isEmpty();
    test_isEmptyNullQueue();

    test_enqueue();
    test_enqueueNullQueue();
    test_enqueueNullEntry();
    test_enqueueRetiredBuffer();
    test_enqueueRetiredBufferNullQueue();
    test_enqueueRetiredBufferNullEntry();

    test_dequeue();
    test_dequeueNullQueue();
    test_dequeueNullEntry();
    test_dequeueNonexistentEntry();

    test_freeBufferQueue();
    test_freeBufferQueueNull();

    // Output
    tbCtr = getBufferQueueTestCounter();
    printTestConclusion(tbCtr);
    global_total += tbCtr->totalCount;
    global_failed += tbCtr->totalFailed;

    //RVMA Mailbox Hashmap Tests
    initRVMAMailboxHashmapTest();

    //TEST CASES
    test_setupMailbox();
    test_initMailboxHashmap();
    test_freeMailbox();
    test_hashFunctionSameHash();
    test_hashFunctionDifferentHash();
    test_newMailboxIntoHashmapNewMailbox();
    test_newMailboxIntoHashmapPresetMailbox();
    test_freeAllMailbox();
    test_freeHashmap();
    test_searchHashmapNullHashmap();
    test_searchHashmapNullKey();
    test_searchHashmapNoMailbox();
    test_searchHashmapCorrect();
    test_retireBuffer();

    // Output
    tbCtr = getMailboxHashmapTestCounter();
    printTestConclusion(tbCtr);
    global_total += tbCtr->totalCount;
    global_failed += tbCtr->totalFailed;

    //RVMA Write Tests
    initRVMAWriteTest();

    //TEST CASES
    test_RVMAInitWindowMailboxKey();
    test_RVMAInitWindowMailbox();
    test_RVMAInitWindow();
    test_RVMASetKeyNullWindow();
    test_RVMASetKeyPresetKey();
    test_RVMASetKeyNewKey();
    test_RVMAAddMailboxtoWindowIncorrectKey();
    test_RVMAAddMailboxtoWindowCorrectKey();
    test_RVMACloseWin();
    test_RVMAPostBufferInvalidBuffer();
    test_RVMAPostBufferInvalidBufferSize();
    test_RVMAPostBufferInvalidNotificationPtr();
    test_RVMAPostBufferInvalidNotificationLenPtr();
    test_RVMAPostBufferInvalidVirtualAddress();
    test_RVMAPostBufferInvalidWindow();
    test_RVMAPostBufferCorrect();

    // Output
    tbCtr = getWriteTestCounter();
    printTestConclusion(tbCtr);
    global_total += tbCtr->totalCount;
    global_failed += tbCtr->totalFailed;

    // Overall testing results

    printf("%d/%d test cases PASSED for all RVMA Tests\n",
           (global_total - global_failed), global_total);

    return 0;
}