#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <rdma/rsocket.h>
#include <stdatomic.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>

#include "log.h"
#include "rvma_socket.h"

#define PORT 7471
#define NUM_SCK 1024

static int (*real_socket)(int, int, int) = NULL;
static int (*real_connect)(int, const struct sockaddr *, socklen_t) = NULL;
static int (*real_bind)(int, const struct sockaddr *, socklen_t) = NULL;
static int (*real_listen)(int, int) = NULL;
static int (*real_accept)(int, struct sockaddr *, socklen_t *) = NULL; // Pointers were _Nullable restrict
static ssize_t (*real_send)(int, const void *, size_t, int) = NULL;
static ssize_t (*real_recv)(int, const void *, size_t, int) = NULL;
static ssize_t (*real_write)(int, const void *, size_t) = NULL;
static ssize_t (*real_read)(int, void *, size_t) = NULL;
static int (*real_close)(int) = NULL;
static int (*real_setsockopt)(int, int, int, const void *, socklen_t) = NULL;
static int (*real_getsockopt)(int, int, int, void *, socklen_t*) = NULL;
static int (*real_Nwrite)(int, const char *, size_t, int) = NULL;

__attribute__((constructor)) void init()
{
    real_socket = dlsym(RTLD_NEXT, "socket");
    real_connect = dlsym(RTLD_NEXT, "connect");
    real_bind = dlsym(RTLD_NEXT, "bind");
    real_listen = dlsym(RTLD_NEXT, "listen");
    real_accept = dlsym(RTLD_NEXT, "accept");
    real_send = dlsym(RTLD_NEXT, "send");
    real_recv = dlsym(RTLD_NEXT, "recv");
    real_write = dlsym(RTLD_NEXT, "write");
    real_read = dlsym(RTLD_NEXT, "read");
    real_close = dlsym(RTLD_NEXT, "close");
    real_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    real_getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    real_Nwrite = dlsym(RTLD_NEXT, "Nwrite");
    log_debug("RVMA PIPE SHIM loaded.");
    
}

static int port_num = PORT;
static int sockets_created = 0;
RVMA_Win *globalWindowPtr = NULL;

struct conn_state {
    int fd;
    int port;
    uint64_t vaddr;
    RVMA_Win* windowPtr;
};

static struct conn_state *conns = NULL;

RVMA_Win* getConnPtr(int fd) {
    for(int i = 0; i < sockets_created; i++) {
        if (fd == conns[i].fd) {
            log_trace("getConnPtr: vaddr found = %lx", conns[i].vaddr);
            log_trace("getConnPtr: ptr found = %p", conns[i].windowPtr);
            return conns[i].windowPtr;
        }
    }
    return NULL;
}

void init_conns() {
    conns = malloc(sizeof(struct conn_state) * NUM_SCK);
    if (!conns) {
        log_error("init_conns: malloc failed");
        exit(1);
    }
}

void generateWindowPtr(uint64_t vaddr)
{
    log_info("vaddr -> %d", vaddr);
    globalWindowPtr = rvmaInitWindowMailbox(vaddr);
    log_info("globalWindowPtr -> %p", globalWindowPtr);
}

char * get_ip(char * interface_name)
{
    int fd;
    struct ifreq ifr;

    fd = real_socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    real_close(fd);
    return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

int is_socket(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        return 0;
    return S_ISSOCK(st.st_mode);
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    log_trace("Returned from setsockopt FD = %d", fd);
    return 0;
    return real_setsockopt(fd, level, optname, optval, optlen);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t* optlen)
{
    log_trace("Returned from getsockopt FD = %d", sockfd);
    return 0;
    return real_getsockopt(sockfd, level, optname, optval, optlen);
}

int socket(int domain, int type, int protocol)
{
    if (sockets_created == 0) init_conns();
    log_trace("Socket domain = %d", domain);
    log_trace("Socket type = %d", type);
    log_trace("Socket protocol = %d", protocol);
    log_trace("Entering socket generation %d", sockets_created);
    // if (sockets_created == 2) return -1;

    char* ip = NULL;
    uint16_t reserved = 0x0001;
    ip = get_ip("ib0");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = domain;
    addr.sin_port = htons(port_num);

    // THIS IS VERY TEMP
    ip = "10.82.49.2";
    log_trace("CHANGING TO SERVER IP %s", ip);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        log_error("inet_pton failed.");
        perror("inet_pton failed");
        return -1;
    }
    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);
    uint64_t vaddr = 0x00000000;
    vaddr = constructVaddr(reserved, ip_host_order, PORT);
    // log_debug("ip_host_order -> %" PRIu32 "\n", ip_host_order);
    // log_debug("Port -> %d\n", PORT);
    // log_debug("Vaddr -> %" PRIu64 "\n", vaddr);
    RVMA_Win* windowPtr = rvmaInitWindowMailbox(vaddr);
    int rvma_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    log_debug("PORT -> %d", port_num);
    log_debug("RVMA FD -> %d", rvma_fd);
    
    conns[sockets_created].fd = rvma_fd;
    conns[sockets_created].windowPtr = windowPtr;
    conns[sockets_created].vaddr = vaddr;
    conns[sockets_created].port = port_num;

    port_num++;
    sockets_created++;
    return rvma_fd;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    log_trace("IN FUNC: Accepting connection over %d", sockfd);
    RVMA_Win *acceptPtr = getConnPtr(sockfd);
    if (!acceptPtr) {
        log_error("No pointer found for fd = %d", sockfd);
        exit(-1);
    }
    int r = rvaccept(sockfd, addr, addrlen, acceptPtr);
    log_trace("IN FUNC: Accepted over r = %d", r);
    return r;
}

int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
    log_trace("Entering connect with FD = %d", socket);
    RVMA_Win* connectPtr = getConnPtr(socket);
    int ret = rvconnect(socket, address, address_len, connectPtr);
    log_trace("rvconnect outcome: %d", ret);
    return ret;
}

 int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int status = rvbind(sockfd, addr, addrlen);
    log_trace("Bind status = %d", status);
    return status;
}

int listen(int sockfd, int backlog) {
    int ret = rvlisten(sockfd, backlog);
    log_trace("Listen status = %d", ret);
    return ret;
}

ssize_t send(int socket, const void *buf, size_t len, int flags)
{
    int r = rvsend(socket, buf, len);
    log_trace("IN FUNC: send ret = %d", r);
    log_trace("IN FUNC: Sending %zu bytes", len);
    return (ssize_t) r;
}

ssize_t recv(int socket, void *buf, size_t len, int flags)
{
    int r = rvrecv(socket, buf, len, flags);
    log_trace("IN FUNC: recv ret = %d", r);
    log_trace("IN FUNC: Received %zu bytes", len);
    return (ssize_t) r;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    // return send(fd, buf, count, 0);
    // temp
    log_trace("Writing to fd = %d", fd);
    return real_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t r;
    // r = recv(fd, buf, count, 0);
    // return r;
    
    // temp
    r = real_read(fd, buf, count);
    return r;
}

int Nwrite(int fd, const char *buf, size_t count, int prot) {
    log_trace("Nwrite: Attempting to write (%d), buf = %s", fd, buf);
    return send(fd, buf, count, 0);
}

int close(int fd)
{
    log_trace("Closing %d", fd);
    return rclose(fd);
}


