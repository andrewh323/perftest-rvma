/*
Test if mailbox is set up correctly - DONE
Test if connection is establised
Test if rvma_write is successful
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

int main(int argc, char **argv) {
    int port = 7471;
    int vaddr = 135;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);
    RVMA_Mailbox *mailboxPtr = searchHashmap(windowPtr->hashMapPtr, &vaddr);

    if (!mailboxPtr) {
        fprintf(stderr, "Failed to get mailbox for vaddr = %d\n", vaddr);
        exit(1);
    }

    if (establishMailboxConnection(mailboxPtr, &server_addr) != 0) {
        fprintf(stderr, "Failed to establish connection\n");
        exit(1);
    }
    printf("Client connected to server %s:%d\n", argv[1], port);
}