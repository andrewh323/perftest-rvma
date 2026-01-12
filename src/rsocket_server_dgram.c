
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

#define PORT 7471
#define PAYLOAD_SIZE 64*1024 // 64KB

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


int main() {
    double cpu_ghz = get_cpu_ghz();
    int sockfd;
    struct sockaddr_in addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    uint64_t start, end;

    /* header definition must match client */
    struct msg_hdr {
        uint32_t seq;
    };

    /* large enough for max payload you will test */
    size_t max_msg_size = 1024 * 1024;
    char *buf = aligned_alloc(64, max_msg_size);

    start = rdtsc();
    sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);
    end = rdtsc();
    printf("rsocket setup time: %.3f µs\n",
           (end - start) / (cpu_ghz * 1e3));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    start = rdtsc();
    rbind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    end = rdtsc();
    printf("rbind time: %.3f µs\n",
           (end - start) / (cpu_ghz * 1e3));

    while (1) {
        ssize_t n = rrecvfrom(sockfd, buf, max_msg_size, 0,
                              (struct sockaddr *)&client_addr, &client_len);

		printf("received!\n");
        if (n < (ssize_t)sizeof(struct msg_hdr))
            continue;

        /* echo back exactly what was received */
        rsendto(sockfd, buf, n, 0,
                (struct sockaddr *)&client_addr, client_len);
		printf("sent!\n");
    }

    rclose(sockfd);
    free(buf);
    return 0;
}