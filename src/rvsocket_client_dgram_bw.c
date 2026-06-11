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
#define MSG_SIZE 1024*128
#define TOTAL_BYTES (128 * 1024 * 1024) // 128 MB

int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    int sockfd;
    double cpu_ghz = get_cpu_ghz();
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
    
    sockfd = rvsocket(SOCK_DGRAM, vaddr, windowPtr);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }
    
    RVMA_Mailbox *mailbox = searchHashmap(windowPtr->hashMapPtr, vaddr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    int ret;
    // In case server is not yet ready for connection request, reattempt
    for (int i = 0; i < 50; i++) {
        ret = rvconnect_dgram(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == 0) {
            break;  // successfully connected
        }
        usleep(1000 * 100); // wait for 100 ms before reattempting
    }

    if (ret != 0) {
        fprintf(stderr, "Failed to connect after multiple retries\n");
        exit(1);
    }
    int res;

    // Set to 1 to exclude warm-ups
    void *buffer = malloc(MSG_SIZE);

    struct timespec start_time, end_time;
    size_t bytes_sent = 0;
    int count = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_time); // Start timing just before sending

    while (bytes_sent < TOTAL_BYTES) {

        rvmaProgress(mailbox);
        int res = rvsendto(sockfd, buffer, MSG_SIZE, windowPtr);
        if (res < 0) {
            fprintf(stderr, "Failed to send message\n");
        }
        else {
            bytes_sent += res;
            count += 1;
        }
    }
    
    printf("Messages sent: %d\n", count);
    clock_gettime(CLOCK_MONOTONIC, &end_time); // End timing just after sending

    // Calculate elapsed time in seconds
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double bandwidth_MBps = (double)bytes_sent / (1024 * 1024) / elapsed;
    double bandwidth_GBps = bandwidth_MBps / 1024; // Convert to GiB/s

    printf("Elapsed time: %.2f microseconds\n", elapsed * 1e6);
    printf("Bandwidth: %.2f MB/s (%.2f GiB/s)\n", bandwidth_MBps, bandwidth_GBps); //GiB vs GB

    rclose(sockfd);
    return 0;
}