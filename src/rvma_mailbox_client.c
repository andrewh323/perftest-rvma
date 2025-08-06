#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"


uint64_t construct_vaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}

int main(int argc, char **argv) {
    int port = 7471;
    uint16_t reserved = 0x0001; // Reserved 16 bits for vaddr structure

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    };

    // Convert IP to host byte order and construct vaddr
    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);
    uint64_t vaddr = construct_vaddr(reserved, ip_host_order, port);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);
    RVMA_Mailbox *mailboxPtr = searchHashmap(windowPtr->hashMapPtr, &vaddr);

    if (!mailboxPtr) {
        fprintf(stderr, "Failed to get mailbox for vaddr = %" PRIu64 "\n", vaddr);
        return -1;
    }

    // Resolves address and route, creates QP and cm event
    if (establishMailboxConnection(mailboxPtr, &server_addr) != 0) {
        fprintf(stderr, "Failed to establish connection\n");
        return -1;
    }
    printf("Client connected to server %s:%d\n", argv[1], port);

    for (int i = 1; i < 10; i++) {
        // Define data buffer to send
        char *message = malloc(100);
        snprintf(message, 100, "Hello server! This is message %d from the client!", i);

        int64_t size = strlen(message) + 1;

        char *buffer = malloc(size);
        memcpy(buffer, message, size);

        // Perform rvmaPut on vaddr
        RVMA_Status status = rvmaSend((void *)buffer, size, &vaddr, windowPtr);
        if (status != RVMA_SUCCESS) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }
        // Free data buffer memory once finished
        free(buffer);
        free(message);
    }
}