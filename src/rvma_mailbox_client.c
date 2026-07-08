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

    int num_sends = 1000;
    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }
    printf("Sending messages of size %d bytes\n", size);


    uint64_t t1, t2;

    void *recv_buf = malloc(size);
    char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

    uint64_t *latencies = malloc(num_sends * sizeof(uint64_t));
    uint64_t total = 0;
    RVMA_Status status;

    for (int i = 0; i < num_sends; i++) {
        t1 = rdtsc();
        do {
            status = rvmaSend(messages[i], size, vaddr, mailboxPtr);
        } while (status == RVMA_RETRY);

        if (rvmaRecv(vaddr, recv_buf, size, 0, mailboxPtr) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaRecv failed\n");
            return -1;
        }
        t2 = rdtsc();
        latencies[i] = t2 - t1;
    }
    
    uint64_t min = 999999999999999999;
    uint64_t max = 0;

    // Skip the first send for warmup
    for (int i = 1; i < num_sends; i++) {
        if (latencies[i] < min) min = latencies[i];
        if (latencies[i] > max) max = latencies[i];
        total += latencies[i];
    }
    double mean_cycles = total / (double)(num_sends - 1);
    double mean_us = mean_cycles / (cpu_ghz * 1e3);

    double variance = 0.0;
    for (int i = 1; i < num_sends; i++) {
        double diff = latencies[i] - mean_cycles;
        variance += diff * diff;
    }
    variance /= (num_sends - 1);

    double stddev_cycles = sqrt(variance);
    double stddev_us = stddev_cycles / (cpu_ghz * 1e3);

    printf("Mean: %.3f µs\n", mean_us);
    printf("Stddev: %.3f µs\n", stddev_us);
    printf("Min: %.3f µs\n", min / (cpu_ghz * 1e3));
    printf("Max: %.3f µs\n", max / (cpu_ghz * 1e3));

    return 0;
}