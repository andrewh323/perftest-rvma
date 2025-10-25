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
#include <stdint.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

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


// Function to measure clock cycles
static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


struct timespec start_time, end_time;
long ns; // Nanoseconds
double us; // Microseconds

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];

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

    const char *message = "Hello from rsocket client!";

    uint64_t start = rdtsc();

    for (int i=0; i<1000; i++) {
        if (rsend(sockfd, message, strlen(message) + 1, 0) < 0) {
            perror("rsend");
            exit(EXIT_FAILURE);
        }
    }

    uint64_t end = rdtsc();

    uint64_t elapsed = end - start;

    double cpu_ghz = get_cpu_ghz();
    double elapsed_us = elapsed / (cpu_ghz * 1e3);
    printf("Sent 1000 messages to server\n");
    printf("Average time per send: %.3f microseconds\n", elapsed_us / 1000.0); 

    // Close the socket
    rclose(sockfd);
    return 0;
}