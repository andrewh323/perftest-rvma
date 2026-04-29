#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#define PORT 7471
#define MSG_SIZE 1000*2
#define TOTAL_BYTES (128 * 1024 * 1024) // Total bytes to send (128 MB)

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_addr;

    struct msg_hdr {
		uint32_t seq;
	};

    size_t msg_size = sizeof(struct msg_hdr) + MSG_SIZE;

    size_t alloc_size = (msg_size + 63) & ~63UL;

    char *buffer = aligned_alloc(64, alloc_size);

    struct msg_hdr *hdr = (struct msg_hdr *)buffer;

    sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);
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

    // Unnecessary but helps with flow control
    if (rconnect(sockfd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(1);
	}

    int num_sends = 1000;

    memset(buffer, 0, msg_size);
    memset(buffer + sizeof(struct msg_hdr), 0xAB, MSG_SIZE);

    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0
    };
    rsetsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct timespec start_time, end_time;
    size_t bytes_sent = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_time); // Start timing just before sending
    
    while (bytes_sent < TOTAL_BYTES) {
        ssize_t n = rsend(sockfd, buffer, MSG_SIZE, 0);
        if (n < 0) {
            perror("rsend");
            break;
        }
        else if (n == 0) {
            printf("Connection closed by the server, rsend returned 0\n");
            break; // Connection closed by the server
        } else {
            bytes_sent += n;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time); // End timing just after sending

    // Calculate elapsed time in seconds
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double bandwidth_MBps = (double)bytes_sent / (1024 * 1024) / elapsed;
    double bandwidth_GBps = bandwidth_MBps / 1024; // Convert to GiB/s

    printf("Elapsed time: %.2f microseconds\n", elapsed * 1e6);
    printf("Bandwidth: %.2f MiB/s (%.2f GiB/s)\n", bandwidth_MBps, bandwidth_GBps);

    free(buffer);
    // Close the socket
    rclose(sockfd);
    return 0;
}