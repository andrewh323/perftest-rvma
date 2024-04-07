//
// Created by Nicholas Chivaran on 2024-02-16.
//

#ifndef ELEC498_RVMA_TEST_COMMON_H
#define ELEC498_RVMA_TEST_COMMON_H

#include <stdbool.h>
#include <string.h>

#include "rvma_common.h"

typedef struct {
    int totalCount;
    int totalFailed;
    char * testBench;
} RVMA_testCounter;

void testFinished(RVMA_testCounter *ctr);

void testFailed(RVMA_testCounter *ctr);

RVMA_testCounter* initTestCounter();

void setTestbench(RVMA_testCounter *ctr, char* tb);

void printTestFailed(RVMA_testCounter *ctr, char* testCaseStr);

void printTestFooter(RVMA_testCounter *ctr);

void printTestConclusion(RVMA_testCounter *ctr);

#endif //ELEC498_RVMA_TEST_COMMON_H