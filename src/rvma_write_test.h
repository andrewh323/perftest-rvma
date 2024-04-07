//
// Created by Nicholas Chivaran on 2024-02-10.
//

#ifndef ELEC498_RVMA_TEST_H
#define ELEC498_RVMA_TEST_H

#include <stdbool.h>
#include <string.h>

#include "rvma_write.h"
#include "rvma_test_common.h"

void initRVMAWriteTest();

RVMA_testCounter* getWriteTestCounter();

void test_RVMAInitWindowMailboxKey();

void test_RVMAInitWindowMailbox();

void test_RVMAInitWindow();

void test_RVMASetKeyNullWindow();

void test_RVMASetKeyPresetKey();

void test_RVMASetKeyNewKey();

void test_RVMAAddMailboxtoWindowIncorrectKey();

void test_RVMAAddMailboxtoWindowCorrectKey();

void test_RVMACloseWin();

void test_RVMAPostBufferInvalidBuffer();

void test_RVMAPostBufferInvalidBufferSize();

void test_RVMAPostBufferInvalidNotificationPtr();

void test_RVMAPostBufferInvalidNotificationLenPtr();

void test_RVMAPostBufferInvalidVirtualAddress();

void test_RVMAPostBufferInvalidWindow();

void test_RVMAPostBufferCorrect();

#endif //ELEC498_RVMA_TEST_H
