#ifndef RVMA_DEBUG_H
#define RVMA_DEBUG_H

#include <stdio.h>

extern int g_client_id;

// magic header to shadow printf everywhere this header is included
#define printf(fmt, ...) fprintf(stdout, "[Client %d] " fmt, g_client_id, ##__VA_ARGS__)

#endif