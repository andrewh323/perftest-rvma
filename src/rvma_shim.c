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

#include "rvma_socket.h"


#define PERROR(str) fprintf(stderr, "%s_errno=%d (%s)\n", str, errno, strerror(errno));
#define PORT 5201

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

    fprintf(stderr, "[shim] loaded\n");
}

// Have multiple sockets available
static int _Atomic current_rvma_fd = 0;
static int _Atomic sockets_created = 0;

char * get_ip(char * interface_name)
{
    int fd;
    struct ifreq ifr;

    fd = real_socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    /* display result */
    // printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

    return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

int is_socket(int fd) {
    struct stat st;

    if (fstat(fd, &st) == -1)
        return 0;

    return S_ISSOCK(st.st_mode);
}

int get_fd(int rvsocket); // use strace -f -e trace=network to see sockets


int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!is_socket(fd)) {
        fprintf(stderr, "[shim] ESCAPED SETSOCKOPT\n");
        // Pretend success
        return 0;
    }

    return real_setsockopt(fd, level, optname, optval, optlen);
}

/*
Called by both server/client- how do I separate from client/server
I need to let the first control socket pass, then let the others go after
Need to let first two sockets pass me by
Seems that iperf opens it at the IP layer, not TCP
*/
int socket(int domain, int type, int protocol)
{
    if (sockets_created == 0) {
        sockets_created++;
        fprintf(stderr, "[shim] created REAL socket\n");
        return real_socket(domain, type, protocol);
    }

    int fd = 0;

// Below is rvsocket crap
    uint16_t reserved = 0x0001;
    fprintf(stderr, "[shim] socket generated\n");

    char* ip = get_ip("ib0");
    fprintf(stderr, "[shim] ib0 -> %s\n", ip);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = domain;
    addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

    // addr.sin_addr.s_addr = INADDR_ANY;
    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);

    char ip_str[INET_ADDRSTRLEN];
    if(inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        return -1;
    }
    fprintf(stderr, "[shim] socket address -> %s\n", ip_str);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    fprintf(stderr, "[shim] vaddr -> %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);
    fprintf(stderr, "[shim] windowPtr -> %p\n", windowPtr);

    int rvma_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    fprintf(stderr, "[shim] rvma_fd -> %d\n", rvma_fd);

    current_rvma_fd = rvma_fd;
    fd = rvma_fd;
    return fd;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {

    if (sockets_created == 1) {
        fprintf(stderr, "[shim] created REAL accept\n");
        return real_accept(sockfd, addr, addrlen);
    } else {
    PERROR("accept")
    uint16_t reserved = 0x0001;
    struct sockaddr_in *in = (struct sockaddr_in *)addr;

    in->sin_addr.s_addr = INADDR_ANY;
    uint32_t ip_host_order = ntohl(in->sin_addr.s_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    RVMA_Win* windowPtr = rvmaInitWindowMailbox(&vaddr);
    return rvaccept(sockfd, (struct sockaddr *)in, addrlen, windowPtr);
    }
}

// Called by client
int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
#ifndef RVMA_DISABLE
    return real_connect(socket, address, address_len);
#endif
    PERROR("connect")
    if (address->sa_family == AF_INET)
    {
        uint16_t reserved = 0x0001;
        struct sockaddr_in *in = (struct sockaddr_in *)address;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip));

        fprintf(stderr,
                "[shim] connect fd=%d -> %s:%d\n",
                socket,
                ip,
                ntohs(in->sin_port));

        in->sin_addr.s_addr = INADDR_ANY;
        uint32_t ip_host_order = ntohl(in->sin_addr.s_addr);

        uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
        RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);
        
        // Need to fix this
        int ret = rvconnect(socket, (struct sockaddr *)&in, sizeof(address_len), windowPtr);

        if(ret < 0) {
            perror("rconnect");
            exit(EXIT_FAILURE);
        }

        return ret;
    }
}

// Called by server
 int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {

    int status = 0;
    PERROR("bind")

    if (sockets_created == 1 && is_socket(sockfd)) {
        fprintf(stderr, "[shim] successfully bound on fd = %d\n", sockfd);
        fprintf(stderr, "[shim] created REAL bind\n");
        status = real_bind(sockfd, addr, addrlen);
    } else if (!is_socket(sockfd)){
        status = rvbind(sockfd, addr, sizeof(addr));
    }
    return status;
}

int listen(int sockfd, int backlog) {

    fprintf(stderr, "[shim] successfully started listening\n");
    fprintf(stderr, "[shim] sockfd -> %d\n", sockfd);

    if (sockets_created == 1) {
        fprintf(stderr, "[shim] created REAL listener\n");
        return real_listen(sockfd, backlog);
    } else {
        return rvlisten(sockfd, backlog);
    }
}

ssize_t send(int socket, const void *buf, size_t len, int flags)
{
    fprintf(stderr, "[shim] send fd=%d len=%zu\n", socket, len);
#ifndef RVMA_DISABLE
    return real_send(socket, buf, len, flags);
#else
    return rvsend(socket, buf, len);
#endif
}

ssize_t recv(int socket, void *buf, size_t len, int flags)
{
    ssize_t r;
    uint64_t t2;

#ifndef RVMA_DISABLE
    r = real_recv(socket, buf, len, flags);
#else
    r = rvrecv(socket, &t2);
#endif
    fprintf(stderr, "[shim] recv fd=%d got=%zd\n", socket, r);
    fprintf(stderr, "recv_errno=%d (%s)\n", errno, strerror(errno));

    return r;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    //fprintf(stderr, "[shim] write fd=%d\n", fd);
    if (fd != current_rvma_fd) return real_write(fd, buf, count);
    return rvsend(fd, buf, count);
}

// idm_lookup/idm_at
ssize_t read(int fd, void *buf, size_t count)
{
    // Have to write a get_fd function, if my fd is this then do it
    ssize_t r;
    uint64_t t2;
    
#ifndef RVMA_DISABLE
    r = real_read(fd, buf, count);
#else
    
    if (fd != current_rvma_fd) {
        fprintf(stderr, "[shim] real_fd -> %d\n", fd);
        r = real_read(fd, buf, count);
    } else {
        fprintf(stderr, "[shim] rvma_fd -> %d\n", fd);
        r = rvrecv(fd, &t2);
    }
    
#endif
    //fprintf(stderr, "[shim] read fd=%d got=%zd\n", fd, r);
    //fprintf(stderr, "errno=%d (%s)\n", errno, strerror(errno));

    return r;
}

int close(int fd)
{
    // fprintf(stderr, "[shim] close fd=%d\n", fd);
#ifndef RVMA_DISABLE
    return real_close(fd);
#else
    real_close(fd);
    return rclose(fd);
#endif
}
