#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "log.h"


int main(int argc, char** argv) {

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(7471);

    // ib0
    inet_pton(AF_INET, argv[1], &server.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    log_info("Attempting connection with FD = %d", sock);
    int ret = connect(sock, (struct sockaddr*)&server, sizeof(server));
    log_info("Successfully connected with FD = %d", sock);
    char *msg = malloc(strlen("hello")+1);
    strcpy(msg, "hello");
    send(sock, msg, sizeof(msg), 0);
    log_info("Sent %s to sock %d with ret %d", msg, sock, ret);
        
    char *buf = malloc(strlen("gaming")+1);
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    log_info("Received: %s", buf);
    
    close(sock);
    free(buf);
    free(msg);
    return 0;
}
