#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if !defined(__FreeBSD__)
#include <malloc.h>
#endif

#include "rvma_socket.h"
#include "raw_ethernet_resources.h"


#define RVMA_VADDR 123 // Change later to be ip+port
#define PORT 7471

int main(int argc, char **argv) {

    int listen_fd, conn_fd;
    struct sockaddr_in addr;
    char buffer[1024];

    // Initialize RVMA hashmap
    RVMA_Win *rvma_window = rvmaInitWindowMailbox(RVMA_VADDR);
    // This sets and returns windowPtr hashMapPtr and key
    // Also allocates new mailbox into hashmap by calling setupMailbox()
    // Mailbox accessible using searchHashmap() (called by rvmaPostBuffer)
    // Also sets up buffer queue for buffers to attach

    RVMA_Mailbox *mailboxPtr = searchHashmap(rvma_window->hashMapPtr, RVMA_VADDR);

    listen_fd = rvsocket(AF_INET, SOCK_STREAM, 0, mailboxPtr);

    memset(&addr, 0, sizeof(addr));
    // Set address structure
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the address
    rvbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    // Listen for connection on specified IP and port
    rvlisten(listen_fd, 5);
    printf("Server listening on port %d...\n", PORT);
    // Do I need IP connection? Can't I just send to the RVMA vaddr?

    conn_fd = rvaccept(listen_fd, NULL, NULL);
    if (conn_fd < 0) {
        perror("rvaccept");
        exit(EXIT_FAILURE);
    }
    printf("Client successfully connected!\n");
    
    // Client does rvsend

    // rvrecv from client

}