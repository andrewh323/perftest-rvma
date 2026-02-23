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
static ssize_t (*real_send)(int, const void *, size_t, int) = NULL;
static ssize_t (*real_recv)(int, const void *, size_t, int) = NULL;
static ssize_t (*real_write)(int, const void *, size_t) = NULL;
static ssize_t (*real_read)(int, void *, size_t) = NULL;
static int (*real_close)(int) = NULL;

__attribute__((constructor)) void init()
{
    real_socket = dlsym(RTLD_NEXT, "socket");
    real_connect = dlsym(RTLD_NEXT, "connect");
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
    printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

    return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

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

    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);

    fprintf(stderr, "[shim] socket address -> %p\n", server_addr.sin_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);

    fprintf(stderr, "[shim] vaddr -> %d\n", vaddr);

    int fd = real_socket(domain, type, protocol);

    return fd;
}

int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
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
    }

    return real_connect(socket, address, address_len);
}

ssize_t send(int socket, const void *buf, size_t len, int flags)
{
    fprintf(stderr, "[shim] send fd=%d len=%zu\n", socket, len);

    return real_send(socket, buf, len, flags);
}

ssize_t recv(int socket, void *buf, size_t len, int flags)
{
    ssize_t r = real_recv(socket, buf, len, flags);

    fprintf(stderr, "[shim] recv fd=%d got=%zd\n", socket, r);

    return r;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (fd != 5)
        fprintf(stderr, "[shim] write fd=%d\n", fd);

    return real_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{

    ssize_t r = real_read(fd, buf, count);

    fprintf(stderr, "[shim] read fd=%d got=%zd\n", fd, r);

    return r;
}

int close(int fd)
{
    fprintf(stderr, "[shim] close fd=%d\n", fd);
    return real_close(fd);
}
