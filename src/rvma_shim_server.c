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

    fprintf(stderr, "[shim] loaded\n");
}

// Should change to a struct
static int _Atomic sockets_created = 0;
static int _Atomic sockets_accepted = 0;

RVMA_Win *globalWindowPtr = NULL;

void generateWindowPtr(uint64_t vaddr)
{
    globalWindowPtr = rvmaInitWindowMailbox(vaddr);
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
    
    if (sockets_created == 1) return real_setsockopt(fd, level, optname, optval, optlen);
    return 0;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t* optlen)
{
    if (sockets_created == 1) return real_getsockopt(sockfd, level, optname, optval, optlen);
    return 0;
}

int socket(int domain, int type, int protocol)
{
    sockets_created++;
    fprintf(stderr, "[server_shim] sockets created -> %d\n", sockets_created);
    if (sockets_created == 1) fprintf(stderr, "[server_shim] initial socket created\n");
    if (sockets_created == 1) return real_socket(domain, type, protocol);

    uint16_t reserved = 0x0001;
    char* ip = get_ip("ib0");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = domain;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }
    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);
    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    fprintf(stderr, "[server_shim] ip_host_order -> %" PRIu32 " port -> %d\n", ip_host_order, PORT);
    fprintf(stderr, "[server_shim] socket_vaddr -> %" PRIu64 "\n", vaddr);
    // RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);
    generateWindowPtr(vaddr);
    int rvma_fd = rvsocket(SOCK_STREAM, vaddr, globalWindowPtr);
    fprintf(stderr, "[server_shim] fd (RVMA) -> %d\n", rvma_fd);
    return rvma_fd;
}

/*
Maybe UCP for talking to andrew's stuff goes thru TCP i gues
what do I do with options?

*/

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    sockets_accepted++;
    fprintf(stderr, "[server_shim] sockets accepted -> %d\n", sockets_accepted);
    if (sockets_accepted == 1) fprintf(stderr, "[server_shim] initial socket accepted\n");
    if (sockets_accepted == 1) return real_accept(sockfd, addr, addrlen);

    uint16_t reserved = 0x0001;
    struct sockaddr_in *in = (struct sockaddr_in *)addr;
    in->sin_addr.s_addr = INADDR_ANY;
    uint32_t ip_host_order = ntohl(in->sin_addr.s_addr);
    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    fprintf(stderr, "[server_shim] accept_vaddr -> %" PRIu64 "\n", vaddr);
    // RVMA_Win* windowPtr = rvmaInitWindowMailbox(vaddr);
    return rvaccept(sockfd, (struct sockaddr *)in, addrlen, globalWindowPtr);
}

int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
    if (sockets_created == 1) return real_connect(socket, address, address_len);
    uint16_t reserved = 0x0001;
    struct sockaddr_in *in = (struct sockaddr_in *)address;
    // in->sin_addr.s_addr = INADDR_ANY;
    in->sin_family = AF_INET;
    in->sin_port = htons(PORT);
    uint32_t ip_host_order = ntohl(in->sin_addr.s_addr);
    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    fprintf(stderr, "[server_shim] connect_vaddr -> %" PRIu64 "\n", vaddr);
    RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in->sin_addr, ip_str, sizeof(ip_str));
    fprintf(stderr, "[server_shim] (connect) attempting rdma_resolve_addr with -> %s\n", ip_str);
    fprintf(stderr, "[server_shim] (connect) attempting connection with fd -> %d\n", socket);
    int ret = rvconnect(socket, (struct sockaddr *)&in, sizeof(address_len), globalWindowPtr);
    return ret;
}

 int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (sockets_created == 1) return real_bind(sockfd, addr, addrlen);
    struct sockaddr_in *in = (struct sockaddr_in *)addr;
    in->sin_addr.s_addr = INADDR_ANY;
    int status = rvbind(sockfd, (struct sockaddr *)in, addrlen);
    status = rvlisten(sockfd, 5);
    return status;
}

int listen(int sockfd, int backlog) {
    if (sockets_created == 1) return real_listen(sockfd, backlog);
    return 0;
    return rvlisten(sockfd, 5);
}

ssize_t send(int socket, const void *buf, size_t len, int flags)
{
    if(sockets_created == 1) return real_send(socket, buf, len, flags);
    return rvsend(socket, buf, len);
}

ssize_t recv(int socket, void *buf, size_t len, int flags)
{
    // ssize_t r;
    if (sockets_created == 1) return real_recv(socket, buf, len, flags);
    return rvrecv(socket, buf, len, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return real_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t r;
    r = real_read(fd, buf, count);
    return r;
}

int close(int fd)
{
    int close_check = real_close(fd);
    if (close_check != 0) rclose(fd);
    return close_check;
}
