
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

#define PORT 7471
#define MSG_SIZE 1000*2

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


int main() {
    double cpu_ghz = get_cpu_ghz();
    int sockfd;
    struct sockaddr_in addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    char *buf = malloc(MSG_SIZE);

    sockfd = rsocket(AF_INET, SOCK_DGRAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    rbind(sockfd, (struct sockaddr *)&addr, sizeof(addr));

    ssize_t n;

	ssize_t total = 0; // Total bytes received
	// Receive data from client
	while ((n = rrecv(sockfd, buf, MSG_SIZE, 0)) > 0) {
		total += n; // Accumulate total bytes received
	}

	printf("Server received %zd bytes\n", total);

    rclose(sockfd);
    free(buf);
    return 0;
}