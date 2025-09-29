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

int main(int argc, char **argv) {
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

    rvconnect_dgram(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    sleep(1);
    // Define data buffer to send
    char *message = malloc(100);
    strcpy(message, "Hello server! This is a message from the client!");
    int64_t size = strlen(message) + 1;

    // Perform rvmasendto on rvma socket
    int res;
    /* for (int i = 1; i <= 10; i++) {
        char *message = malloc(100);
        snprintf(message, 100, "Hello server! This is message %d from the client!", i);
        int64_t size = strlen(message) + 1;
        char *buffer = malloc(size);
        memcpy(buffer, message, size);
        res = rvsendto(sockfd, &buffer, size);
        if (res < 0) {
            fprintf(stderr, "Failed to send message\n");
        }
    } */
    res = rvsendto(sockfd, &message, size);
    if (res < 0) {
        fprintf(stderr, "Failed to send message\n");
    }
    // Close the socket
    rclose(sockfd);
    return 0;
}