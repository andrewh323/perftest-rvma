#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "rvma_socket.h"

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
    uint64_t start, end;
    int sockfd;
    struct sockaddr_in server_addr;
    double elapsed_us;
    double rtt = 0.0;

    start = rdtsc();
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    end = rdtsc();
    elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("socket setup time: %.3f µs\n", elapsed_us);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Convert IP address from text to binary form

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    start = rdtsc();
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    end = rdtsc();
    elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("connect time: %.3f µs\n", elapsed_us);

    int msg_size = 1024;
    if (argc > 2) {
        msg_size = atoi(argv[2]);
    }

    char *message = malloc(msg_size);

    memset(message, 'A', msg_size - 1);
    message[msg_size - 1] = '\0';

    uint64_t t1, t2;
    uint64_t total = 0;

    t1 = rdtsc();
    ssize_t n = send(sockfd, message, msg_size, 0);
    if (n <= 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }
    n = recv(sockfd, message, msg_size, 0);
    if (n <= 0) {
        perror("recv");
        exit(EXIT_FAILURE);
    }
    t2 = rdtsc();

    double cycles = t2 - t1;
    double latency_us = cycles / (cpu_ghz * 1e3);
    
    printf("RTT: %.3f µs\n", latency_us);
    free(message);
    
    // Close the socket
    close(sockfd);
    return 0;
}