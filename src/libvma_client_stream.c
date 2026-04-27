#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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

    int num_sends = 1000;
    uint64_t *latencies = malloc(num_sends * sizeof(uint64_t));
    uint64_t t1, t2;
    uint64_t total = 0;

    for (int i = 0; i < num_sends; i++) {
        uint64_t t1 = rdtsc();
        size_t total_sent = 0;
        while (total_sent < msg_size) {
            ssize_t n = send(sockfd, message + total_sent, msg_size - total_sent, 0);
            if (n <= 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
            total_sent += n;
        }
        size_t total_recv = 0;
        while (total_recv < msg_size) {
            ssize_t n = recv(sockfd, message + total_recv, msg_size - total_recv, 0);
            if (n <= 0) {
                perror("recv");
                exit(EXIT_FAILURE);
            }
            total_recv += n;
        }
        uint64_t t2 = rdtsc();
        if (i > 0) { // Skip warmup round
            latencies[i - 1] = t2 - t1;
            total += (t2 - t1);
        }
    }

    double mean_cycles = total / (double)(num_sends - 1);
    double mean_us = mean_cycles / (cpu_ghz * 1e3);

    double variance = 0.0;
    for (int i = 0; i < num_sends - 1; i++) {
        double diff = latencies[i] - mean_cycles;
        variance += diff * diff;
    }
    variance /= (num_sends - 1);

    double stddev_cycles = sqrt(variance);
    double stddev_us = stddev_cycles / (cpu_ghz * 1e3);

    uint64_t min = latencies[0];
    uint64_t max = latencies[0];

    for (int i = 1; i < num_sends - 1; i++) {
        if (latencies[i] < min) min = latencies[i];
        if (latencies[i] > max) max = latencies[i];
    }
    
    printf("Mean: %.3f µs\n", mean_us);
    printf("Stddev: %.3f µs\n", stddev_us);
    printf("Min: %.3f µs\n", min / (cpu_ghz * 1e3));
    printf("Max: %.3f µs\n", max / (cpu_ghz * 1e3));

    free(latencies);
    free(message);
    
    // Close the socket
    close(sockfd);
    return 0;
}