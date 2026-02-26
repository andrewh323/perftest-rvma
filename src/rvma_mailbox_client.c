#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"


static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t construct_vaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}

int main(int argc, char **argv) {
    double cpu_ghz = get_cpu_ghz();
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

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr); // Initialize mailbox space
    RVMA_Status res = newMailboxIntoHashmap(windowPtr->hashMapPtr, vaddr); // Insert new mailbox into hashmap
    RVMA_Mailbox *mailboxPtr = searchHashmap(windowPtr->hashMapPtr, vaddr); // Retrieve mailbox from hashmap
    if (!mailboxPtr) {
        fprintf(stderr, "Failed to get mailbox for vaddr = %" PRIu64 "\n", vaddr);
        return -1;
    }

    // Create RDMA cm_id
    struct rdma_cm_id *cm_id;
    struct rdma_event_channel *ec = rdma_create_event_channel();
    if (rdma_create_id(ec, &cm_id, NULL, RDMA_PS_TCP)) {
        fprintf(stderr, "rdma_create_id failed\n");
        return -1;
    }
    mailboxPtr->cm_id = cm_id;
    mailboxPtr->ec = ec;

    // Resolves address and route, creates QP and cm event
    if (establishMailboxConnection(mailboxPtr, &server_addr) != 0) {
        fprintf(stderr, "Failed to establish connection\n");
        return -1;
    }
    printf("Client connected to server %s:%d\n", argv[1], port);

    // Prepost buffers
    res = postSendPool(mailboxPtr, 16, vaddr, EPOCH_BYTES);
    if (res != RVMA_SUCCESS) {
        perror("postSendPool failed");
        return -1;
    }
    res = postRecvPool(mailboxPtr, 16, vaddr, EPOCH_BYTES);
    if (res != RVMA_SUCCESS) {
        perror("postRecvPool failed");
        return -1;
    }

    int num_sends = 100;
    int size = 10000;
    uint64_t t2;

    char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

    for (int i = 0; i < num_sends; i++) {
        uint64_t t1 = rdtsc();
        rvmaSend(messages[i], size, vaddr, mailboxPtr);
        rvmaRecv(vaddr, mailboxPtr, &t2);
        double elapsed_us = (t2 - t1) / (cpu_ghz * 1e3);
        printf("RTT: %.2f microseconds\n", elapsed_us);
    }
}