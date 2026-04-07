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
// #include "tcp_strings.h"

#define PORT 7471
#define NUM_SCK 1024
#define IPERF

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
#ifdef IPERF
static int (*real_Nwrite)(int, const char *, size_t, int) = NULL;
#endif /* ifdef IPERF */

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
    #ifdef IPERF
    real_Nwrite = dlsym(RTLD_NEXT, "Nwrite");
    #endif /* ifdef IPERF */
    log_debug("init: shared library loaded successfully.");
    
}

static int port_num = PORT;
static int sockets_created = 0;
static int active_sockets = 0;
RVMA_Win *globalWindowPtr = NULL;

#ifdef IPERF
static int control_created = 0;
#endif /* ifdef IPERF */

typedef enum socketType {
    CONTROL_SOCKET,
    RVMA_SOCKET
}socketType;

struct conn_state {
    int fd;
    int port;
    uint64_t vaddr;
    RVMA_Win* windowPtr;
    socketType type;
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
    log_info("generateWindowPtr: vaddr -> %d", vaddr);
    globalWindowPtr = rvmaInitWindowMailbox(vaddr);
    log_info("generateWindowPtr: globalWindowPtr -> %p", globalWindowPtr);
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
    log_trace("setsockopt: FD = %d, opt = %d", fd, optname);
    int status = 0;
    // char *opt = sockopt_to_str(level, optname);
    // log_trace("setsockopt: opt = %s", opt);
    #ifdef IPERF
    if (sockets_created == 1 && control_created && optname != 7 && optname != 8) {
        status = real_setsockopt(fd, level, optname, optval, optlen);
    }
    #endif /* ifdef IPERF */
    log_trace("setsockopt: status = %d", status);
    return status;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t* optlen)
{
    log_trace("getsockopt: FD = %d, optname = %d", sockfd, optname);
    int status = 0;
    #ifdef IPERF
    if (sockets_created == 1 && control_created && optname != 7 && optname != 8) {
        status = real_setsockopt(sockfd, level, optname, optval, optlen);
    }
    #endif /* ifdef IPERF */
    log_trace("getsockopt: status = %d", status);
    return status;
}

int socket(int domain, int type, int protocol)
{

    active_sockets++;
    if (sockets_created == 0) init_conns();
    log_trace("socket: domain = %d", domain);
    log_trace("socket: type = %d", type);
    log_trace("socket: protocol = %d", protocol);
    log_trace("socket: generation %d", sockets_created);

    #ifdef IPERF
    if (sockets_created == 0) {
        int ctrl_fd = real_socket(domain, type, protocol);
        log_trace("socket: IPERF Control FD = %d", ctrl_fd);
        conns[sockets_created].fd = ctrl_fd;
        conns[sockets_created].port = PORT; // probably needs to be changed
        conns[sockets_created].vaddr = 0x00000000;
        conns[sockets_created].windowPtr = NULL;
        conns[sockets_created].type = CONTROL_SOCKET;
        sockets_created++;
        control_created = 1;
        return ctrl_fd;
    }
    #endif /* ifdef IPERF */

    char* ip = NULL;
    uint16_t reserved = 0x0001;
    ip = get_ip("ib0");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = domain;
    addr.sin_port = htons(port_num);

    // THIS IS VERY TEMP
    ip = "10.82.49.1";
    log_trace("socket: CHANGING TO SERVER IP %s", ip);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        log_error("socket: inet_pton failed.");
        return -1;
    }
    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);
    uint64_t vaddr = 0x00000000;
    vaddr = constructVaddr(reserved, ip_host_order, PORT);
    RVMA_Win* windowPtr = rvmaInitWindowMailbox(vaddr);
    int rvma_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    log_debug("socket: PORT -> %d", port_num);
    log_debug("socket: RVMA FD -> %d", rvma_fd);
    
    conns[sockets_created].fd = rvma_fd;
    conns[sockets_created].windowPtr = windowPtr;
    conns[sockets_created].vaddr = vaddr;
    conns[sockets_created].port = port_num;
    conns[sockets_created].type = RVMA_SOCKET;

    // port_num++;
    sockets_created++;
    return rvma_fd;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int r = -1;
    log_trace("accept: accepted over fd  = %d", sockfd);

    #ifdef IPERF
    if (sockets_created == 1) {
        r = real_accept(sockfd, addr, addrlen);
        log_trace("accept: IPERF over r = %d", r);
        return r;
    }
    
    #endif /* ifdef IPERF */

    RVMA_Win *acceptPtr = getConnPtr(sockfd);
    if (!acceptPtr) {
        log_error("accept: No pointer found for fd = %d", sockfd);
        exit(-1);
    }
    r = rvaccept(sockfd, addr, addrlen, acceptPtr);
    log_trace("accept: over r = %d", r);
    return r;
}

int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
    int ret = -1;
    #ifdef IPERF
    if (sockets_created == 1) {
        log_trace("connect: IPERF with fd = %d", socket);
        ret = real_connect(socket, address, address_len);
        log_trace("connect: IPERF status = %d", ret);
        return ret;
    }
    #endif /* ifdef IPERF */

    log_trace("connect: rvconnect with FD = %d", socket);
    RVMA_Win* connectPtr = getConnPtr(socket);
    ret = rvconnect(socket, address, address_len, connectPtr);
    log_trace("connect: status = %d", ret);
    return ret;
}

 int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int status = -1;    
    #ifdef IPERF
    if (sockets_created == 1) {
        log_trace("bind: IPERF on sockfd = %d", sockfd);
        status = real_bind(sockfd, addr, addrlen);
        log_trace("bind: IPERF status = %d", status);
        return status;
    }
    #endif /* ifdef IPERF */
    
    status = rvbind(sockfd, addr, addrlen);
    log_trace("bind: status = %d", status);
    return status;
}

int listen(int sockfd, int backlog) {
    int ret = -1; 
    #ifdef IPERF
    if (sockets_created == 1) {
        ret = real_listen(sockfd, backlog);
        log_trace("listen: IPERF status = %d", ret);
        return ret;
    }
    #endif /* ifdef IPERF */

    ret = rvlisten(sockfd, backlog);
    log_trace("listen: status = %d", ret);
    return ret;
}

ssize_t send(int socket, const void *buf, size_t len, int flags)
{
    log_trace("send: Sending over socket %d", socket);
    int r = -1;

    #ifdef IPERF
    if (sockets_created == 1) {
        r = real_send(socket, buf, len, flags);
        log_trace("send: IPERF send ret = %d", r);
        return (ssize_t) r;
    }
    #endif /* ifdef IPERF */

    r = rvsend(socket, buf, len);
    log_trace("send: send ret = %d", r);
    log_trace("send: Sending %zu bytes", len);
    return (ssize_t) r;
}

ssize_t recv(int socket, void *buf, size_t len, int flags)
{
    int r = -1;
    #ifdef IPERF
    if (sockets_created == 1) {
        r = real_recv(socket, buf, len, flags);
        log_trace("recv: IPERF ret = %d", r);
        return (ssize_t) r;
    }
    #endif /* ifdef IPERF */

    log_trace("recv: Receiving over socket %d", socket);
    r = rvrecv(socket, buf, len, flags);
    log_trace("recv: ret = %d", r);
    log_trace("recv: Received %zu bytes", len);
    return (ssize_t) r;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    log_trace("write: Writing to fd = %d", fd);
    return real_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t r;
    log_trace("read: Reading from fd = %d", fd);
    r = real_read(fd, buf, count);
    return r;
}

#ifdef IPERF
int Nwrite(int fd, const char *buf, size_t count, int prot) {
    log_trace("Nwrite: Attempting to write (%d), buf = %lx", fd, buf);
    if (sockets_created == 1) {
        log_trace("Nwrite: IPERF writing to FD = %d", fd);
        return real_Nwrite(fd, buf, count, prot);  
    }
    return send(fd, buf, count, 0);
}
#endif /* ifdef IPERF */

int close(int fd)
{
    active_sockets--;
    log_trace("close: Closing %d", fd);
    return rclose(fd);
    if (active_sockets == 0) {
        log_trace("close: Freeing connections");
        control_created = 0;
        close(fd);
        free(conns);
    }
}


