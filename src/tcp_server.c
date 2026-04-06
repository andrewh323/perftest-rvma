#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include "log.h"

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


int main(int argc, char** argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    log_info("Generated RVMA FD %d", server_fd);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7471);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // ib0
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return -1;
    }
    log_info("Bound successfully!");
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        return -1;
    }
    log_info("Listening successfully!");

    int client_fd = accept(server_fd, NULL, NULL);
    log_info("Receiving over FD = %d", client_fd);

    int *buf = malloc(sizeof(int)*1);
    if (buf == NULL) {
        log_error("buf is null.");
        close(server_fd);
        close(client_fd);
        exit(1);
    }
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);



    log_info("Received: %d", *buf);
    
    int to_send = 1;
    int *sendbuf = &to_send;
    if (sendbuf == NULL) {
        log_error("sendbuf is null.");
        close(server_fd);
        close(client_fd);
        exit(1);
    }
    
    log_info("Server sending %d", *sendbuf);
    send(client_fd, sendbuf, sizeof(sendbuf), 0);  // no null byte needed

    close(client_fd);
    close(server_fd);
    // free(sendbuf);
    free(buf);
    return 0;
}
