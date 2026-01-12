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

static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    int sockfd;
    double cpu_ghz = get_cpu_ghz();
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

    int ret;
    // In case server is not yet ready for connection request, reattempt
    for (int i = 0; i < 50; i++) {
        ret = rvconnect_dgram(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == 0) {
            break;  // successfully connected
        }
        usleep(1000 * 100); // wait for 100 ms before reattempting
    }

    if (ret != 0) {
        fprintf(stderr, "Failed to connect after multiple retries\n");
        exit(1);
    }
    int res;

    int size = 1024;
    if (argc > 2) {
        size = atoi(argv[2]);
    }
    printf("Sending messages of size %d bytes\n", size);

    int num_sends = 100;
    int warmup_sends = 10; // number of warmup sends

    // Set to 1 to exclude warm-ups
    int exclude_warmup = 1;

    int measured_sends = exclude_warmup ? (num_sends - warmup_sends) : num_sends;
    double *send_times = malloc(measured_sends * sizeof(double));

    double elapsed_us;
    double min_time = 1e9;
    double max_time = 0;
    double sum_time = 0;
    double frag_setup_time = 0;
    double buffer_setup_time = 0;
    double wr_setup_time = 0;
    double poll_time = 0;

    for (int i = 0; i < num_sends; i++) {
        char *message = malloc(size + 1);
        if (!message) {
            perror("malloc failed");
            exit(1);
        }
        // Fill with unique pattern per message
        // Use a repeating sequence that encodes message + fragment indices
        for (int j = 0; j < size; j++) {
            // Use printable pattern: A, B, C... to visualize offsets
            message[j] = 'A' + ((i + j) % 26);
        }
        message[size] = '\0';

        printf("Sending message %d: %.40s...\n", i, message);

        uint64_t t1 = rdtsc();
        res = rvsendto(sockfd, message, size);
        if (res < 0) {
            fprintf(stderr, "Failed to send message %d\n", i);
        }

        res = rvrecv(sockfd, NULL);
        if (res < 0) {
            perror("rvrecv failed");
        }
        uint64_t t2 = rdtsc();
        elapsed_us = (t2 - t1) / (cpu_ghz *1e3);

        // Convert cycles to microseconds
        double fragSetup_us = mailbox->fragSetupCycles / (cpu_ghz * 1e3);
        double bufferSetup_us = mailbox->bufferSetupCycles / (cpu_ghz * 1e3);
        double wrSetup_us = mailbox->wrSetupCycles / (cpu_ghz * 1e3);
        double poll_us = mailbox->pollCycles / (cpu_ghz * 1e3);

        elapsed_us -= (bufferSetup_us);
        elapsed_us /= 2; // One-way time

        /* printf("Message %d send time: %.3f µs (Frag setup: %.3f µs, Buffer setup: %.3f µs, WR setup: %.3f µs, Poll: %.3f µs)\n",
            i, elapsed_us, fragSetup_us, bufferSetup_us, wrSetup_us, poll_us); */
        int record = 1;

        // Exclude warm-ups if configured
        if (exclude_warmup && i < warmup_sends)
            record = 0;

        if (record) {
            int idx = exclude_warmup ? (i - warmup_sends) : i;
            send_times[idx] = elapsed_us;

            if (elapsed_us < min_time) min_time = elapsed_us;
            if (elapsed_us > max_time) max_time = elapsed_us;
            sum_time += elapsed_us;
            frag_setup_time += fragSetup_us;
            buffer_setup_time += bufferSetup_us;
            wr_setup_time += wrSetup_us;
            poll_time += poll_us;
        }
        free(message);
    }

    // Compute averages
    double avg_time = sum_time / measured_sends;
    double avg_frag_setup = frag_setup_time / measured_sends;
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
    printf("Size of each message:     %d bytes\n", size);
    printf("Fragments per message:    %d\n", (size + RS_MAX_TRANSFER - 1) / RS_MAX_TRANSFER);
    printf("Average buffer setup:     %.3f µs\n", avg_buffer_setup);
    printf("Average frag setup:       %.3f µs\n", avg_frag_setup);
    printf("Average WR setup:         %.3f µs\n", avg_wr_setup);
    printf("Average poll time:        %.3f µs\n", avg_poll_time);
    printf("Min send time:            %.3f µs\n", min_time);
    printf("Max send time:            %.3f µs\n", max_time);
    printf("Avg send time:            %.3f µs\n", avg_time);
    printf("Send time stddev:         %.3f µs\n", stddev);
    printf("====================================\n");

    rclose(sockfd);
    return 0;
}