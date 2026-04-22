
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

#define PORT 7471

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
	int listen_fd, conn_fd;
	struct sockaddr_in addr;
	int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

	char *buffer = malloc(size);
	uint64_t start, end, t2, t3;

	start = rdtsc();
	listen_fd = rsocket(AF_INET, SOCK_STREAM, 0);
	end = rdtsc();
	printf("rsocket setup time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET; // IPv4
	// htons converts port number from host byte order to network byte order
	addr.sin_port = htons(PORT);
	// INADDR_ANY is a constant that represents any address (0.0.0.0)
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	// Now we can bind the socket to the address
	start = rdtsc();
	rbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
	end = rdtsc();
	printf("rbind time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));

	// Listen for incoming connections
	start = rdtsc();
	rlisten(listen_fd, 5);
	end = rdtsc();
	printf("rlisten time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));
	printf("Server listening on port %d...\n", PORT);

	// Accept a connection from client
	conn_fd = raccept(listen_fd, NULL, NULL); // print in rsocket.c since raccept is blocking

	int num_recv = 1000;
	double *recv_times = malloc(num_recv * sizeof(double));

	for (int i = 0; i < num_recv; i++) {
		size_t total_recv = 0;
		while (total_recv < size) {
			ssize_t n = rrecv(conn_fd, buffer + total_recv, size - total_recv, 0);
			if (n <= 0) {
				perror("rrecv");
				exit(EXIT_FAILURE);
			}
			total_recv += n;
		}

		size_t total_sent = 0;
		while (total_sent < size) {
			ssize_t n = rsend(conn_fd, buffer + total_sent, size - total_sent, 0);
			if (n <= 0) {
				perror("rsend");
				exit(EXIT_FAILURE);
			}
			total_sent += n;
		}
	}

	// Close the connection
	free(buffer);
	rclose(conn_fd);
	rclose(listen_fd);
	return 0;
}