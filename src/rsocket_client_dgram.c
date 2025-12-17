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


int main(int argc, char **argv) {
    double cpu_ghz = get_cpu_ghz();
    int sockfd;
    struct sockaddr_in server_addr;
    uint64_t start, end;
    double elapsed_us;

    start = rdtsc();
    sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }
    end = rdtsc();
    elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rsocket setup time: %.3f µs\n", elapsed_us);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Convert IP address from text to binary form

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    int num_sends = 1000;
    size_t msg_size = atoi(argv[2]);
    char *message = malloc(msg_size);
    memset(message, 0xAB, msg_size); // Fill message with dummy data

    for (int i=0; i<num_sends; i++) {
        start = rdtsc();
        if (rsendto(sockfd, message, strlen(message) + 1, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("rsendto");
            exit(EXIT_FAILURE);
        }
        if (i == 0) {
            end = rdtsc();
            double setup_us = (end - start) / (cpu_ghz * 1e3);
            printf("rsendto first send time: %.3f µs\n", setup_us);
        }else
        if (i < 10) {
            // Don't record warm up rounds
        }
        else {
            end = rdtsc();
            elapsed_us += (end - start) / (2.4 * 1e3);
        }
    }
    printf("Average send time: %.3f microseconds\n", elapsed_us / (num_sends - 10)); 

    // Close the socket
    rclose(sockfd);
    return 0;
}