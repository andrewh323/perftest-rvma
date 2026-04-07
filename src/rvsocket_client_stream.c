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
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    };

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

    // Define message size
    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }
    printf("Sending messages of size %d bytes\n", size);

    int num_sends = 100;
    int warmup_sends = 10; // number of warmup sends

    // Set to 1 to exclude warm-ups
    int exclude_warmup = 1;

    int measured_sends = exclude_warmup ? (num_sends - warmup_sends) : num_sends;
    double *send_times = malloc(measured_sends * sizeof(double));

    double elapsed_us = 0;
    double min_time = 1e9;
    double max_time = 0;
    double sum_time = 0;
    double buffer_setup_time = 0;
    double wr_setup_time = 0;
    double poll_time = 0;
    double regmr_time = 0;

    char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

    void *recv_buf = malloc(size);
    uint64_t t1, t2;
    int completed = 0, sent = 0;

    // Send messages to server
    while (completed < num_sends) {
        // Perform rvma send
        t1 = rdtsc();
        while (sent < num_sends) {
            int res = rvsend(sockfd, messages[sent], size);
            if (res < 0) {
                printf("No more buffers available for send, sent %d messages so far\n", sent);
                break;
            }
            sent++;
        }

        rvrecv(sockfd, recv_buf, size, 0);
        completed++;
    }

    free(send_times);
    rclose(sockfd);
    return 0;
}