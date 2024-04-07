/***
 * General Description:
 * This file contains the general functions used in the testing framework
 *
 * Authors: Nicholas Chivaran, Samantha Hawco
 *
 * Reviewers: Ethan Shama, Nathan Kowal
 *
 ***/

#include "rvma_test_common.h"

void testFinished(RVMA_testCounter *ctr){
    ctr->totalCount++;
}

void testFailed(RVMA_testCounter *ctr){
    ctr->totalFailed++;
}

void setTestbench(RVMA_testCounter *ctr, char* tb){
    ctr->testBench = tb;
}

RVMA_testCounter* initTestCounter(){
    RVMA_testCounter* ctr = (RVMA_testCounter*)malloc(sizeof(RVMA_testCounter));
    ctr->totalCount = 0;
    ctr->totalFailed = 0;
    ctr->testBench = "";
    return ctr;
}

void printTestFailed(RVMA_testCounter *ctr, char* testCaseStr){
    if (ctr->totalFailed != 0){
        printf("%s FAILED: %s\n", testCaseStr, get_errorMsg());
    }
    else{
        printf("=============== RVMA %s Test Failures ===============\n", ctr->testBench);
        printf("%s FAILED: %s\n", testCaseStr, get_errorMsg());
    }
}

void printTestFooter(RVMA_testCounter *ctr){
    printf("=============== RVMA %s Test Failures ===============\n", ctr->testBench);
}

void printTestConclusion(RVMA_testCounter *ctr){
    if (ctr->totalFailed == 0){
        printf("%d/%d test cases PASSED for RVMA %s Test\n", ctr->totalCount, ctr->totalCount, ctr->testBench);
    }
    else{
        printTestFooter(ctr);
        printf("%d/%d test cases PASSED for RVMA %s Test, see messages above for failed tests\n",
               (ctr->totalCount - ctr->totalFailed), ctr->totalCount, ctr->testBench);
    }
}



