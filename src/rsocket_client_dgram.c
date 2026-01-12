/*
The server first binds address that clients will use to find the server
Server then listens for clients to request a connection
When a request is received by the client the server accepts the connection
The server then posts a receive buffer to the connection
THe client sends a message to the server and the server receives it
The server then sends a message back to the client confirming the message was received
The client then receives the message from the server
The server then disconnects from the client
The client then disconnects from the server
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#define PORT 7471

double get_cpu_ghz() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 2.4; // fallback
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        double mhz;
        if (sscanf(line, "cpu MHz\t: %lf", &mhz) == 1) {
            fclose(fp);
            return mhz / 1000.0; // MHz → GHz
        }
    }
    fclose(fp);
    return 2.4; // fallback
}

// Function to measure clock cycles
static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


int main(int argc, char **argv) {
    double cpu_ghz = get_cpu_ghz();
    int sockfd;
    struct sockaddr_in server_addr;
    uint64_t start, end;
    double elapsed_us = 0.0;
    double rtt = 0.0;

    struct msg_hdr {
		uint32_t seq;
	};

    size_t payload_size = atoi(argv[2]);   // runtime size
    size_t msg_size = sizeof(struct msg_hdr) + payload_size;

    size_t alloc_size = (msg_size + 63) & ~63UL;

    char *send_buf = aligned_alloc(64, alloc_size);
    char *recv_buf = aligned_alloc(64, alloc_size);

    struct msg_hdr *hdr = (struct msg_hdr *)send_buf;
    struct msg_hdr *rhdr = (struct msg_hdr *)recv_buf;

    start = rdtsc();
    sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("rsocket");
        exit(EXIT_FAILURE);
    }
    end = rdtsc();
    elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rsocket setup time: %.3f µs\n", elapsed_us);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Convert IP address from text to binary form

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    // Unnecessary but helps with flow control
    if (rconnect(sockfd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("rconnect");
        exit(1);
	}

    int num_sends = 12;

    memset(send_buf, 0, msg_size);
    memset(send_buf + sizeof(struct msg_hdr), 0xAB, payload_size);

    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0
    };
    rsetsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (uint32_t i = 0; i < num_sends; i++) {
        hdr->seq = htonl(i);

        start = rdtsc();
        rsendto(sockfd, send_buf, msg_size, 0,
                (struct sockaddr *)&server_addr, sizeof(server_addr));

        while (1) {
            ssize_t n = rrecvfrom(sockfd, recv_buf, msg_size, 0, NULL, NULL);

            if (n < 0) {
                if (errno == EWOULDBLOCK) {
                    // retransmit
                    rsendto(sockfd, send_buf, msg_size, 0,
                            (struct sockaddr *)&server_addr, sizeof(server_addr));
                    printf("Sent message!\n");
                    continue;
                }
                perror("rrecvfrom");
                continue;
            }
            printf("Received message!\n");

            if (n < sizeof(struct msg_hdr))
                continue;

            if (ntohl(rhdr->seq) == i)
                break;   // correct reply
        }

        end = rdtsc();

        if (i >= 10) rtt += (end - start) / (cpu_ghz * 1e3);

        if (i == 0) {
            printf("First message RTT: %.3f µs\n",
                   (end - start) / (cpu_ghz * 1e3));
        }
    }

    double one_way = rtt / (2 * (num_sends - 10));
    printf("Average one-way latency: %.3f µs\n", one_way);

    free(send_buf);
    free(recv_buf);
    // Close the socket
    rclose(sockfd);
    return 0;
}