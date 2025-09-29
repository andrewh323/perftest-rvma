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


uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port);

uint64_t rvsocket(int type, uint64_t vaddr, RVMA_Win *window);

int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen);

int rvlisten(int socket, int backlog);

int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen);

int rvaccept_dgram(int dgram_fd, int tcp_listenfd, struct sockaddr *addr, socklen_t *addrlen);

int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen);

int rvconnect_dgram(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int rvsend(int socket, void *buf, int64_t len);

int rvsendto(int socket, void **buf, int64_t len);

int rvrecv(int socket, RVMA_Win *window);

#endif // RVMA_SOCKET_H