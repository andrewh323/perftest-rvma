/*
The server first binds address that clients will use to find the server
Server then listens for clients to request a connection
When a request is received by the client the server accepts the connection
The server then posts a receive buffer to the connection
THe client sends a message to the server and the server receives it
The server then sends a message back to the client confirming the message was received
The client then receives the message from the server
The server then disconnects from the client
The client then disconnects from the server
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#define PORT 7471
#define MSG_SIZE 4096 // Size of message buffer
#define TOTAL_BYTES (128 * 1024 * 1024) // Total bytes to send (128 MB)


int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_addr;
    char *buffer = malloc(MSG_SIZE); // Allocate 4096 bytes
    if (!buffer) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    sockfd = rsocket(AF_INET, SOCK_STREAM, 0);
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

    if (rconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d\n", argv[1], PORT);
    
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
    double bandwidth_Gbps = bandwidth_MBps * 8 / 1000; // Convert to Gbps

    printf("Elapsed time: %.2f microseconds\n", elapsed * 1e6);
    printf("Bandwidth: %.2f MB/s (%.2f Gbps)\n", bandwidth_MBps, bandwidth_Gbps);

    // Close the socket
    rclose(sockfd);
    return 0;
}