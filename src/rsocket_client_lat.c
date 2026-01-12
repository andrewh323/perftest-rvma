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
    uint64_t start, end;
    int sockfd;
    struct sockaddr_in server_addr;
    double elapsed_us;
    double rtt = 0.0;

    start = rdtsc();
    sockfd = rsocket(AF_INET, SOCK_STREAM, 0);
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

    start = rdtsc();
    if (rconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    end = rdtsc();
    elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rconnect time: %.3f µs\n", elapsed_us);

    size_t msg_size = atoi(argv[2]);
    char *message = malloc(msg_size);

    memset(message, 'A', msg_size - 1);
    message[msg_size - 1] = '\0';

    int num_sends = 2;
    double *latencies = malloc(num_sends * sizeof(double));
    char ack[8];

    for (int i = 0; i < num_sends; i++) {
        uint64_t t1 = rdtsc();
        if (rsend(sockfd, message, msg_size, 0) < 0) {
            perror("rsend");
            exit(EXIT_FAILURE);
        }

        if (rrecv(sockfd, ack, sizeof(ack), 0) < 0) {
            perror("rrecv");
            exit(EXIT_FAILURE);
        }
        uint64_t t4 = rdtsc();
        if (i == 0) {
            latencies[i] = 0;
        }
        else {
            rtt += ((t4 - t1) / (cpu_ghz * 1e3));
        }
        usleep(10);
    }

    double one_way = rtt / (2 * (num_sends));
    printf("One-way time per send: %.3f microseconds\n", one_way);

    // Close the socket
    rclose(sockfd);
    return 0;
}