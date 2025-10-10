#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471
#define CPU_FREQ_GHZ 2.4 // From /proc/cpuinfo

int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    int sockfd;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    };

    // Convert IP to host byte order and construct vaddr
    uint32_t ip_host_order = ntohl(server_addr.sin_addr.s_addr);

    uint64_t vaddr = constructVaddr(reserved, ip_host_order, PORT);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    RVMA_Win *windowPtr = rvmaInitWindowMailbox(&vaddr);

    sockfd = rvsocket(SOCK_DGRAM, vaddr, windowPtr);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    // Request connection to server to exchange UD connection info
    rvconnect_dgram(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Perform rvmasendto on rvma socket
    int res;
    int size = 10000;
    for (int i = 1; i <= 10; i++) {
        char *message = malloc(size + 1);
        if (!message) {
            perror("malloc failed");
            exit(1);
        }

        // Fill with unique pattern per message
        // Use a repeating sequence that encodes message + fragment indices
        for (int j = 0; j < size; j++) {
            // Use printable pattern: e.g. A, B, C... to visualize offsets
            message[j] = 'A' + ((i + j) % 26);
        }
        message[size] = '\0';

        // Optional: add a readable prefix (won’t affect total len if you trim manually)
        snprintf(message, size, "Msg %02d BEGIN ", i);

        printf("Sending message %d: %.40s...\n", i, message);

        res = rvsendto(sockfd, message, size);
        if (res < 0) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }

        free(message);
    }


    RVMA_Mailbox *mailbox = searchHashmap(windowPtr->hashMapPtr, &vaddr);
    printf("Total elapsed time for sends: %.3f microseconds\n", mailbox->cycles / (CPU_FREQ_GHZ * 1e3));

    rclose(sockfd);
    return 0;
}