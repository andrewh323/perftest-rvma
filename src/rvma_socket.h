#ifndef RVMA_SOCKET_H
#define RVMA_SOCKET_H

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <rdma/rdma_cma.h>
#include <rdma/rsocket.h>
#include <infiniband/verbs.h>

#include "perftest_resources.h"
#include "rvma_write.h"

#define RS_MAX_TRANSFER 4050 /* 4KB MTU - 40B GRH */ /* set to 4050 so message fragments can be observed*/

struct dgram_frag_header {
    uint32_t frag_num;
    uint32_t total_frags;
};

uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port);

uint64_t rvsocket(int type, uint64_t vaddr, RVMA_Win *window);

int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen);

int rvlisten(int socket, int backlog);

int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen, RVMA_Win *window);

int rvaccept_dgram(int dgram_fd, int tcp_listenfd, struct sockaddr *addr, socklen_t *addrlen);

int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen);

int rvconnect_dgram(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int rvsend(int socket, void *buf, int64_t len);

int rvsendto(int socket, void *buf, int64_t len);

int rvrecvfrom(RVMA_Mailbox *mailbox);

int rvrecv(int socket, uint64_t *recv_timestamp);

#endif // RVMA_SOCKET_H