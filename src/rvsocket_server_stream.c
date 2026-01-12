#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


int main(int argc, char **argv) {
	uint64_t start, end;
	double cpu_ghz = get_cpu_ghz();
	double elapsed_us;
	uint16_t reserved = 0x0001;
	int listen_fd, conn_fd;
	struct sockaddr_in client_addr;
	memset(&client_addr, 0, sizeof(client_addr));

	client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &client_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

	client_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
	uint32_t ip_host_order = ntohl(client_addr.sin_addr.s_addr);

	uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
	printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

	RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);

    listen_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);

	// Bind address to socket
	rvbind(listen_fd, (struct sockaddr *)&client_addr, sizeof(client_addr));

	// Listen for incoming connections
	rvlisten(listen_fd, 5);
	printf("Server listening on port %d...\n", PORT);

	int size = 10;
	int num_sends = 1000;

	char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

	// Accept a connection from client
	conn_fd = rvaccept(listen_fd, NULL, NULL);
	if (conn_fd < 0) {
		perror("rvaccept failed");
		return -1;
	}
	printf("Client successfully connected!\n");

	for (int i = 0; i < num_sends; i++) {
		// Receive data from client
		uint64_t t2;
		int ret = rvrecv(conn_fd, &t2);
		if (ret < 0) {
			perror("Error receiving message");
		}

		ret = rvsend(conn_fd, messages[i], size);
		if (ret < 0) {
			perror("Error sending message");
		}
	}
	
	// Close the connection
	rclose(conn_fd);
	rclose(listen_fd);
	return 0;
}