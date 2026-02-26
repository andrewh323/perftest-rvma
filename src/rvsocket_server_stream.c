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

static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t get_host_addr(const char *iface_name) {
    struct ifaddrs *ifaddr, *ifa;
    uint32_t ip = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        
        if (strcmp(ifa->ifa_name, iface_name) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            ip = ntohl(sa->sin_addr.s_addr);
            break;
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}


int main(int argc, char **argv) {
	uint64_t start, end;
	double cpu_ghz = get_cpu_ghz();
	double elapsed_time, send_time, recv_time;
	uint16_t reserved = 0x0001;
	struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
	int listen_fd;
	int num_clients = 1;
	int conn_fd[num_clients];

	uint32_t host_ip = get_host_addr("ib0");
	uint64_t vaddr = constructVaddr(reserved, host_ip, PORT);
	printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

	addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

	RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);

    listen_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);

	// Bind address to socket
	rvbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));

	// Listen for incoming connections
	rvlisten(listen_fd, 5);
	printf("Server listening on port %d...\n", PORT);

	int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }
	int num_sends = 1000;

	char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }

	for (int i = 0; i < num_clients; i++) {
		// Accept a connection from client
		conn_fd[i] = rvaccept(listen_fd, NULL, NULL, windowPtr);
		if (conn_fd[i] < 0) {
			perror("rvaccept failed");
			return -1;
		}
		printf("Client %d successfully connected!\n", i+1);
	}
	 uint64_t t1, t2, t3;

	// While server is running, poll all clients and recv messages
	// Currently rvrecv is blocking but maintains flow control
	for (int i = 0; i < num_sends; i++) {
		for (int c = 0; c < num_clients; c++) {
			rvrecv(conn_fd[c], NULL);
			rvsend(conn_fd[c], messages[i], size);
		}
	}
	
	// Close the connection
	for (int i = 0; i < num_clients; i++) {
		rclose(conn_fd[i]);
	}
	rclose(listen_fd);
	return 0;
}