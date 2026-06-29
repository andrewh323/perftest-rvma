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

// Function to measure clock cycles
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
	int rvma_conn_fd; // fd for RVMA side connection
    int tcp_conn_fd; // fd for client TCP connection

	int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

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

	void *recv_buf = malloc(size);

	// Accept a connection from client
	rvma_conn_fd = rvaccept(listen_fd, NULL, NULL, windowPtr);
    if (rvma_conn_fd < 0) {
        perror("rvaccept failed");
        return -1;
    }
	printf("RVMA server successfully connected!\n");

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)); // Bind to same port for TCP connection
    
    listen(listen_fd, 5);
    printf("RVMA mediation server listening on port %d for TCP connections...\n", PORT);

    tcp_conn_fd = accept(listen_fd, NULL, NULL);
    printf("TCP client connected!\n");

	uint64_t t1, t2, t3;

    // First receive message from TCP client
    recv(tcp_conn_fd, recv_buf, size, 0);
    //printf("Received message from TCP client: %.*s\n", size, (char *)recv_buf);
    // Send message to the RVMA server
    t1 = rdtsc();
    rvsend(rvma_conn_fd, recv_buf, size);

    // Receive message back from RVMA server
    rvrecv(rvma_conn_fd, recv_buf, size, 0);
    t2 = rdtsc();
    double elapsed_us = (t2 - t1) / (cpu_ghz * 1e3);
    printf("Round-trip time for RVMA send and recv: %.2f µs\n", elapsed_us);
    //printf("Received message back from RVMA server: %.*s\n", size, (char *)recv_buf);
    // Send message back to TCP client
    send(tcp_conn_fd, recv_buf, size, 0);
	
	// Close the connection
	rvclose(rvma_conn_fd);
    close(tcp_conn_fd);
	close(listen_fd);
	return 0;
}