//
// Created by Ethan Shama on 2024-01-25.
//

#ifndef ELEC498_RVMA_COMMON_H
#define ELEC498_RVMA_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "perftest_parameters.h"

#define QUEUE_CAPACITY 1000

typedef enum {
    RVMA_SUCCESS,
    RVMA_FAILURE,
    RVMA_ERROR,
    RVMA_TRUE,
    RVMA_FALSE,
    RVMA_QUEUE_FULL
} RVMA_Status;

char* get_errorMsg();

void set_testingFlag(bool flag);

void print_error(char *error);

#endif //ELEC498_RVMA_COMMON_H
