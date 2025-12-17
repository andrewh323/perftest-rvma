
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
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
	char buffer[1024*1024];
	uint64_t start, end;

	start = rdtsc();
	sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);
	end = rdtsc();
	printf("rsocket setup time: %.3f µs\n", (end - start) / (2.45 * 1e3));

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET; // IPv4
	// htons converts port number from host byte order to network byte order
	addr.sin_port = htons(PORT);
	// INADDR_ANY is a constant that represents any address (0.0.0.0)
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	// Now we can bind the socket to the address
	start = rdtsc();
	rbind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
	end = rdtsc();
	printf("rbind time: %.3f µs\n", (end - start) / (2.45 * 1e3));

	int num_recv = 100;
    double elapsed_us = 0;
	
	for (int i=0; i<num_recv; i++) {
        start = rdtsc();
		// Receive data from client
		rrecvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
		// printf("Server received message: %s\n", buffer);
        if (i < 10) {} // Do not record warmup rounds
        else {
            end = rdtsc();
            elapsed_us += ((end - start) / (2.4 * 1e3));
        }
	}

    printf("Average rrecvfrom time: %.3f microseconds\n", elapsed_us / (num_recv - 1));

	// Close the connection
	rclose(sockfd);
	return 0;
}