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

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

struct timespec start_time, end_time;
long ns; // Nanoseconds
double us; // Microseconds

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];

    Mailbox_HashMap *hashmapPtr = initMailboxHashmap();

    sockfd = rvsocket(AF_INET, ip_host_order, hashmapPtr);
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

    // Send message to the server
    const char *message = "Hello from rsocket client!";
    clock_gettime(CLOCK_MONOTONIC, &start_time); // Start timing just before sending
    if (rsend(sockfd, message, strlen(message) + 1, 0) < 0) {
        perror("rsend");
        exit(EXIT_FAILURE);
    }

    // Receive response from the server
    if (rrecv(sockfd, buffer, sizeof(buffer), 0) < 0) {
        perror("rrecv");
        exit(EXIT_FAILURE);
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time); // End timing just after receiving ACK
    printf("Client received message: %s\n", buffer);

    ns = (end_time.tv_sec - start_time.tv_sec) * 1e9 + (end_time.tv_nsec - start_time.tv_nsec);
    us = ns / 1000.0;

    printf("RTT: %.2f microseconds\n", us);

    // Close the socket
    rclose(sockfd);
    return 0;
}