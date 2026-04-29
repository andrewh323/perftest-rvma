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
#define MSG_SIZE 1024*4
#define TOTAL_BYTES (128 * 1024 * 1024) // 128 MB


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

    int64_t msg_size = MSG_SIZE;
    if (argc > 2) {
        msg_size = atoi(argv[2]);
    }

    char *buffer = malloc(msg_size);

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


    struct timespec start_time, end_time;
    size_t bytes_sent = 0;
    int count = 0;

    // Start timing after warmup
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (bytes_sent < TOTAL_BYTES) {
        int res = rvsend(sockfd, buffer, msg_size);
        if (res < 0) break;
        
        bytes_sent += msg_size;
        count += 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time); // End timing just after sending

    printf("Messages sent: %d\n", count);
    // Calculate elapsed time in seconds
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double bandwidth_MBps = (double)bytes_sent / (1024 * 1024) / elapsed;
    double bandwidth_GBps = bandwidth_MBps / 1024; // Convert to GiB/s

    printf("Elapsed time: %.2f microseconds\n", elapsed * 1e6);
    printf("Bandwidth: %.2f MiB/s (%.2f GiB/s)\n", bandwidth_MBps, bandwidth_GBps); //GiB vs GB
    
    rvclose(sockfd);
    return 0;
}