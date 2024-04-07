/***
 * General Description:
 * This file contains general functions used in the translation layer
 *
 * Authors: Ethan Shama, Nathan Kowal
 *
 * Reviewers: Nicholas Chivaran, Samantha Hawco
 *
 ***/

#include "rvma_common.h"

bool testing_flag = false;

char* errorMsg = "";

void set_testingFlag(bool flag){
    testing_flag = flag;
}

char* get_errorMsg(){
    return errorMsg;
}

void print_error(char *error){
    if (!testing_flag){
        fprintf(stderr, "%s\n", error);
    }
    else{
        errorMsg = ""; // clears previous errormessage
        errorMsg = error; // set error message to a global variable, used in testing to cleanup unit test output
    }
}