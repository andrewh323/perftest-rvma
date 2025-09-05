#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471


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


int main(int argc) {
	uint16_t reserved = 0x0001;
	struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
	int dgram_fd;

    // Construct virtual address
    uint32_t host_ip = get_host_addr("ib0");
    uint32_t ip_host_order = ntohl(host_ip);
	uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
	printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

	addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

	RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);

    dgram_fd = rvsocket(SOCK_DGRAM, vaddr, windowPtr);

	// Bind host address for datagram socket
	rvbind(dgram_fd, (struct sockaddr *)&addr, sizeof(addr));
	printf("Host IP address bound to socket\n");

	// Receive data from client
    int ret = rvrecv(dgram_fd, windowPtr);
	if (ret < 0) {
		perror("Error receiving message");
	}
    close(dgram_fd);
	return 0;
}