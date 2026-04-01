#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "log.h"


int main(int argc, char** argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    log_info("Generated RVMA FD %d", server_fd);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7471);
    //addr.sin_addr.s_addr = INADDR_ANY;
    
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
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        return -1;
    }
    log_info("Listening successfully!");

    int client_fd = accept(server_fd, NULL, NULL);
    log_info("Receiving over FD = %d", client_fd);

    char *buf = malloc(strlen("hello")+1);
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);



    log_info("Received: %s", buf);

    char *sendbuf = malloc(strlen("gaming")+1);
    strcpy(sendbuf, "gaming");
    send(client_fd, sendbuf, sizeof(sendbuf), 0);  // no null byte needed

    close(client_fd);
    close(server_fd);
    free(sendbuf);
    free(buf);
    return 0;
}
