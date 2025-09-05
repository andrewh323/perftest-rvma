#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

struct timespec start_time, end_time;
long ns; // Nanoseconds
double us; // Microseconds

int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];
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

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);

    sockfd = rvsocket(SOCK_DGRAM, vaddr, windowPtr);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    // Send message to the server
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Define data buffer to send
    char *message = "Hello server! This is a message from the client!";

    int64_t size = sizeof(message);

    // Perform rvmaPut on vaddr
    int res = rvsend(sockfd, (void *)message, size);
    if (res < 0) {
        fprintf(stderr, "Failed to send message\n");
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    ns = (end_time.tv_sec - start_time.tv_sec) * 1e9 + (end_time.tv_nsec - start_time.tv_nsec);
    us = ns / 1000.0;

    printf("RTT: %.2f microseconds\n", us);

    // Close the socket
    rclose(sockfd);
    return 0;
}