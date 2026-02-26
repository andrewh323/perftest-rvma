#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <rdma/rsocket.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>

#include "rvma_socket.h"
//#include "rvma_write.h"

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

    fprintf(stderr, "[shim] loaded\n");
}

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

struct sockaddr_in sock_setup(void) {
    return;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
#ifndef RVMA_DISABLE
    return real_accept(sockfd, addr, addrlen);
#endif

    return rvaccept(sockfd, NULL, NULL);
}

// Called by both server/client
int socket(int domain, int type, int protocol)
{
    uint16_t reserved = 0x0001;
    fprintf(stderr, "[shim] socket generated\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = domain;
    server_addr.sin_port = htons(PORT);

    char* ip = get_ip("ib0");

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1)
    {
        perror("inet_pton failed");
        return -1;
    };

    server_addr.sin_addr.s_addr = INADDR_ANY;
    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);

    fprintf(stderr, "[shim] socket address -> %s\n", server_addr.sin_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);

    fprintf(stderr, "[shim] vaddr -> %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);

    fprintf(stderr, "[shim] windowPtr -> %p\n", windowPtr);

    int rvma_fd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    fprintf(stderr, "[shim] rvma_fd -> %d\n", rvma_fd);
    int fd = rvma_fd;
#ifndef RVMA_DISABLE
    fd = real_socket(domain, type, protocol);
#endif
    return fd;
}

// Called by client
int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
#ifndef RVMA_DISABLE
    return real_connect(socket, address, address_len);
#endif

    if (address->sa_family == AF_INET)
    {
        struct sockaddr_in *in = (struct sockaddr_in *)address;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip));

        fprintf(stderr,
                "[shim] connect fd=%d -> %s:%d\n",
                socket,
                ip,
                ntohs(in->sin_port));
        
        int ret = rvconnect(socket, (struct sockaddr *)&in, sizeof(address_len));

        if(ret < 0) {
            perror("rconnect");
            exit(EXIT_FAILURE);
        }

        return ret;
    }
}

// Called by server
 int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {

    
    fprintf(stderr, "[shim] successfully bound\n");

#ifndef RVMA_DISABLE
    return real_bind(sockfd, addr, addrlen);
#endif
    return rvbind(sockfd, addr, sizeof(addr));
}

int listen(int sockfd, int backlog) {

    fprintf(stderr, "[shim] successfully started listening\n");

#ifndef RVMA_DISABLE
    return real_listen(sockfd, backlog);
#endif
    return rvlisten(sockfd, backlog);
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

    return r;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    fprintf(stderr, "[shim] write fd=%d\n", fd);
    return rvsend(socket, buf, count);
    //return real_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t r;
    uint64_t t2;
#ifndef RVMA_DISABLE
    //r = real_read(fd, buf, count);
    r = real_recv(fd, buf, count, 0);
#else  
    r = rvrecv(fd, &t2);
#endif

    fprintf(stderr, "[shim] read fd=%d got=%zd\n", fd, r);

    return r;
}

int close(int fd)
{
    fprintf(stderr, "[shim] close fd=%d\n", fd);
#ifndef RVMA_DISABLE
    return real_close(fd);
#else
    return rclose(fd);
#endif
}
