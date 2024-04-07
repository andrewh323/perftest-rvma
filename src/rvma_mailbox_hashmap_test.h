//
// Created by Nicholas Chivaran on 2024-02-12.
//

#ifndef ELEC498_RVMA_MAILBOX_HASHMAP_TEST_H
#define ELEC498_RVMA_MAILBOX_HASHMAP_TEST_H

#include "rvma_mailbox_hashmap.h"
#include "rvma_test_common.h"

void initRVMAMailboxHashmapTest();

RVMA_testCounter* getMailboxHashmapTestCounter();

void test_setupMailbox();

void test_initMailboxHashmap();

void test_freeMailbox();

void test_hashFunctionSameHash();

void test_hashFunctionDifferentHash();

void test_newMailboxIntoHashmapNewMailbox();

void test_newMailboxIntoHashmapPresetMailbox();

void test_freeAllMailbox();

void test_freeHashmap();

void test_searchHashmapNullHashmap();

void test_searchHashmapNullKey();

void test_searchHashmapNoMailbox();

void test_searchHashmapCorrect();

void test_retireBuffer();

#endif //ELEC498_RVMA_MAILBOX_HASHMAP_TEST_H
