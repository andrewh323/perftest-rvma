#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471


static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


int main(int argc, char **argv) {
    double cpu_ghz = get_cpu_ghz();
    uint16_t reserved = 0x0001;
    int sockfd;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // Arg 1 - Server address
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    };
    // Arg 2 - Message size (Default to 1 KB)
    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }
    printf("Sending messages of size %d bytes\n", size);

    // Convert IP to host byte order and construct vaddr
    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);

    sockfd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Convert IP address from text to binary form

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    printf("Attempting to connect to server with vaddr %" PRIu64 "...\n", vaddr);

    if (rvconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr), windowPtr) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d!\n", argv[1], PORT);

    int num_sends = 1000;

    uint64_t *latencies = malloc(num_sends * sizeof(uint64_t));

    char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

    void *recv_buf = malloc(size);
    uint64_t t1, t2;
    uint64_t total = 0;

    // Send messages to server
    for (int i = 0; i < num_sends; i++) {
        t1 = rdtsc();
        rvsend(sockfd, messages[i], size);
        rvrecv(sockfd, recv_buf, size, 0);
        t2 = rdtsc();
        if (i > 0) { // Skip warmup round
            latencies[i - 1] = t2 - t1;
            total += (t2 - t1);
        }
    }

    double mean_cycles = total / (double)(num_sends - 1);
    double mean_us = mean_cycles / (cpu_ghz * 1e3);

    double variance = 0.0;
    for (int i = 0; i < num_sends - 1; i++) {
        double diff = latencies[i] - mean_cycles;
        variance += diff * diff;
    }
    variance /= (num_sends - 1);

    double stddev_cycles = sqrt(variance);
    double stddev_us = stddev_cycles / (cpu_ghz * 1e3);

    uint64_t min = latencies[0];
    uint64_t max = latencies[0];

    for (int i = 1; i < num_sends - 1; i++) {
        if (latencies[i] < min) min = latencies[i];
        if (latencies[i] > max) max = latencies[i];
    }
    
    printf("Mean: %.3f µs\n", mean_us);
    printf("Stddev: %.3f µs\n", stddev_us);
    printf("Min: %.3f µs\n", min / (cpu_ghz * 1e3));
    printf("Max: %.3f µs\n", max / (cpu_ghz * 1e3));

    free(latencies);
    rvclose(sockfd);
    return 0;
}