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

    if (establishMailboxConnection(mailboxPtr, &server_addr) != 0) {
        fprintf(stderr, "Failed to establish connection\n");
        return -1;
    }
    printf("Client connected to server %s:%d\n", argv[1], port);

    // Define data buffer to send
    const char *message = "Hello server! This message is from the client!";

    int64_t size = strlen(message) + 1;

    char *buffer = malloc(size);
    // Should I make the buffer size fixed? Or have a preallocated buffer pool?

    // Do an rvmaPut on the vaddr
    RVMA_Status status = rvmaPut((void *)buffer, size, &vaddr, windowPtr);

    // Free data buffer memory once finished
    free(buffer);

}