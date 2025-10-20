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


double get_cpu_ghz() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 2.4; // fallback
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        double mhz;
        if (sscanf(line, "cpu MHz\t: %lf", &mhz) == 1) {
            fclose(fp);
            return mhz / 1000.0; // MHz → GHz
        }
    }
    fclose(fp);
    return 2.4; // fallback
}


int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    double cpu_ghz = get_cpu_ghz();
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

    RVMA_Mailbox *mailbox = searchHashmap(windowPtr->hashMapPtr, &vaddr);

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

    printf("Attempting to connect to server %s:%d...\n", argv[1], PORT);

    if (rvconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
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

    double min_time = 1e9; // large initial value
    double max_time = 0;
    double sum_time = 0;
    // send messages to server
    for (int i = 1; i <= 10; i++) {
        // Define data buffer to send
        char *message = malloc(size + 1);
        memset(message, 'A', size);
        message[size] = '\0';
        int n = snprintf(message, size + 1, "Msg %d: ", i);
        for (int j = n; j < size; j++) {
            message[j] = 'A';
        }
        message[size] = '\0';

        // Perform rvma send on the socket
        int res = rvsend(sockfd, message, size);
        if (res < 0) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }

        double elapsed_us = mailbox->lastCycle / (cpu_ghz * 1e3);
        printf("rvmaSend time: %.3f microseconds\n", elapsed_us);
        if (elapsed_us < min_time) min_time = elapsed_us;
        if (elapsed_us > max_time) max_time = elapsed_us;
        sum_time += elapsed_us;

        free(message);
    }

    double avg_time = sum_time / 10.0;
    printf("Min send time: %.3f us\n", min_time);
    printf("Max send time: %.3f us\n", max_time);
    printf("Avg send time: %.3f us\n", avg_time);
    printf("Total send time: %.3f us\n", sum_time);

    // Close the socket
    rclose(sockfd);
    return 0;
}