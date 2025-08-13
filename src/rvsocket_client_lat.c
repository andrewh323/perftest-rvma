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

    sockfd = rvsocket(AF_INET, vaddr, windowPtr);
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

    printf("Attempting to connect to server %s:%d...\n", argv[1], PORT);

    if (rvconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d!\n", argv[1], PORT);

    // Send message to the server
    clock_gettime(CLOCK_MONOTONIC, &start_time); // Start timing just before sending
    
    for (int i = 1; i <= 10; i++) {
        // Define data buffer to send
        char *message = malloc(100);
        snprintf(message, 100, "Hello server! This is message %d from the client!", i);

        int64_t size = strlen(message) + 1;

        char *buffer = malloc(size);
        memcpy(buffer, message, size);

        // Perform rvmaPut on vaddr
        int res = rvsend(sockfd, (void *)buffer, size, windowPtr);
        if (res < 0) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }
        // Free data buffer memory once finished
        free(buffer);
        free(message);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time); // End timing just after receiving ACK

    ns = (end_time.tv_sec - start_time.tv_sec) * 1e9 + (end_time.tv_nsec - start_time.tv_nsec);
    us = ns / 1000.0;

    printf("RTT: %.2f microseconds\n", us);

    // Close the socket
    rclose(sockfd);
    return 0;
}