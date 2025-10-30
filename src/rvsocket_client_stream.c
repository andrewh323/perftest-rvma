#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    double cpu_ghz = get_cpu_ghz();
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

    RVMA_Mailbox *mailbox = searchHashmap(windowPtr->hashMapPtr, &vaddr);

    sockfd = rvsocket(SOCK_STREAM, vaddr, windowPtr);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Convert IP address from text to binary form

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    printf("Attempting to connect to server %s:%d...\n", argv[1], PORT);

    if (rvconnect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d!\n", argv[1], PORT);

    // Define message size
    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }
    printf("Sending messages of size %d bytes\n", size);

    int num_sends = 1000;
    int warmup_sends = 10; // number of warmup sends

    // Set to 1 to exclude warm-ups
    int exclude_warmup = 1;

    int measured_sends = exclude_warmup ? (num_sends - warmup_sends) : num_sends;
    double *send_times = malloc(measured_sends * sizeof(double));

    double min_time = 1e9;
    double max_time = 0;
    double sum_time = 0;
    double buffer_setup_time = 0;
    double wr_setup_time = 0;
    double poll_time = 0;

    // Send messages to server
    for (int i = 0; i < num_sends; i++) {
        // Prepare message buffer
        char *message = malloc(size + 1);
        memset(message, 'A', size);
        message[size] = '\0';
        int n = snprintf(message, size + 1, "Msg %d: ", i);
        for (int j = n; j < size; j++) {
            message[j] = 'A';
        }
        message[size] = '\0';

        // Perform rvma send
        int res = rvsend(sockfd, message, size);
        if (res < 0) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }

        // Convert cycles to microseconds
        double elapsed_us = mailbox->cycles / (cpu_ghz * 1e3);
        double bufferSetup_us = mailbox->bufferSetupCycles / (cpu_ghz * 1e3);
        double wrSetup_us = mailbox->wrSetupCycles / (cpu_ghz * 1e3);
        double poll_us = mailbox->pollCycles / (cpu_ghz * 1e3);

        int record = 1;

        // Exclude warm-ups if configured
        if (exclude_warmup && i < warmup_sends)
            record = 0;

        if (record) {
            int idx = exclude_warmup ? (i - warmup_sends) : i;
            send_times[idx] = elapsed_us;

            printf("rvmaSend time [%d]: %.3f µs\n", i, elapsed_us);

            if (elapsed_us < min_time) min_time = elapsed_us;
            if (elapsed_us > max_time) max_time = elapsed_us;
            sum_time += elapsed_us;
            buffer_setup_time += bufferSetup_us;
            wr_setup_time += wrSetup_us;
            poll_time += poll_us;
        }
        free(message);
    }

    // Compute averages
    double avg_time = sum_time / measured_sends;
    double avg_buffer_setup = buffer_setup_time / measured_sends;
    double avg_wr_setup = wr_setup_time / measured_sends;
    double avg_poll_time = poll_time / measured_sends;

    // Compute standard deviation
    double variance = 0.0;
    for (int i = 0; i < measured_sends; i++) {
        double diff = send_times[i] - avg_time;
        variance += diff * diff;
    }
    variance /= (measured_sends - 1);
    double stddev = sqrt(variance);

    // Print results
    printf("\n===== RVMA Send Timing Results =====\n");
    printf("Exclude warm-up:          %s\n", exclude_warmup ? "Yes" : "No");
    printf("Messages measured:        %d of %d\n", measured_sends, num_sends);
    printf("Average buffer setup:     %.3f µs\n", avg_buffer_setup);
    printf("Average WR setup:         %.3f µs\n", avg_wr_setup);
    printf("Average poll:             %.3f µs\n", avg_poll_time);
    printf("Min send time:            %.3f µs\n", min_time);
    printf("Max send time:            %.3f µs\n", max_time);
    printf("Avg send time:            %.3f µs\n", avg_time);
    printf("Send time stddev:         %.3f µs\n", stddev);
    printf("====================================\n");

    free(send_times);
    rclose(sockfd);
    return 0;
}