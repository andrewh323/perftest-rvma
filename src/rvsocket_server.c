#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if !defined(__FreeBSD__)
#include <malloc.h>
#endif

#include "rvma_socket.h"
#include "perftest_resources.h"
#include "raw_ethernet_resources.h"


#define RVMA_VADDR 123
#define PORT 7471

int main(int argc, char **argv) {

    int listen_fd, conn_fd;
    struct sockaddr_in addr;
    char buffer[1024];

    // Initialize RVMA window with virtual address
    RVMA_Win *rvma_window = rvmaInitWindowMailbox(RVMA_VADDR);


    listen_fd = rvsocket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    rvbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    rvlisten(listen_fd, 5);
    printf("Server listening on port %d...\n", PORT);

    conn_fd = rvaccept(listen_fd, NULL, NULL);
    if (conn_fd < 0) {
        perror("rvaccept");
        exit(EXIT_FAILURE);
    }
    printf("Client successfully connected!\n");
    
    // Client does rvsend

    // rvrecv from client

}
