#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

int main(int argc, char **argv) {
	uint64_t start, end;
	double elapsed_time, send_time, recv_time;
	uint16_t reserved = 0x0001;
	int sockfd;

	struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // Arg 1 - Server address
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    };
    // Arg 2 - Message size (Default to 1 KB)
    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }

    // Convert IP to host byte order and construct vaddr
    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);

    sockfd = rvsocket(SOCK_STREAM, vaddr, windowPtr);

	void *recv_buf = malloc(size);

    // Connect to mediation server
    if (rvconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr), windowPtr) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d!\n", argv[1], PORT);

	uint64_t t1, t2, t3;

    // Receive message from mediation server
    rvrecv(sockfd, recv_buf, size, 0);
    rvsend(sockfd, recv_buf, size); // Echo back message
	
	// Close the connection
	rvclose(sockfd);
	return 0;
}