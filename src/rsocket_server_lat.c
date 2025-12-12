
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

int main() {
	int listen_fd, conn_fd;
	struct sockaddr_in addr;
	char buffer[1024];
	uint64_t start, end;

	start = rdtsc();
	listen_fd = rsocket(AF_INET, SOCK_STREAM, 0);
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
	rbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
	end = rdtsc();
	printf("rbind time: %.3f µs\n", (end - start) / (2.45 * 1e3));

	// Listen for incoming connections
	start = rdtsc();
	rlisten(listen_fd, 5);
	end = rdtsc();
	printf("rlisten time: %.3f µs\n", (end - start) / (2.45 * 1e3));
	printf("Server listening on port %d...\n", PORT);

	// Accept a connection from client
	conn_fd = raccept(listen_fd, NULL, NULL); // print in rsocket.c since raccept is blocking

	int num_recv = 100;
	start = rdtsc();
	for (int i=0; i<num_recv; i++) {
		// Receive data from client
		rrecv(conn_fd, buffer, sizeof(buffer), 0);
		// printf("Server received message: %s\n", buffer);
	}
	end = rdtsc();
	printf("rrecv time for %d messages: %.3f µs\n", num_recv, (end - start) / (2.45 * 1e3));

	// Close the connection
	rclose(conn_fd);
	rclose(listen_fd);
	return 0;
}