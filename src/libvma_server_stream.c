
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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
	int listen_fd, conn_fd;
	struct sockaddr_in addr;
	int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

	char *buffer = malloc(size);
	uint64_t start, end, t2, t3;

	start = rdtsc();
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	end = rdtsc();
	printf("socket setup time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET; // IPv4
	// htons converts port number from host byte order to network byte order
	addr.sin_port = htons(PORT);
	// INADDR_ANY is a constant that represents any address (0.0.0.0)
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	// Now we can bind the socket to the address
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	start = rdtsc();
	bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
	end = rdtsc();
	printf("bind time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));

	// Listen for incoming connections
	start = rdtsc();
	listen(listen_fd, 5);
	end = rdtsc();
	printf("listen time: %.3f µs\n", (end - start) / (cpu_ghz * 1e3));
	printf("Server listening on port %d...\n", PORT);

	// Accept a connection from client
	conn_fd = accept(listen_fd, NULL, NULL); // print in rsocket.c since raccept is blocking

	int num_recv = 1000;
	double *recv_times = malloc(num_recv * sizeof(double));

	for (int i = 0; i < num_recv; i++) {
		size_t total_recv = 0;
		while (total_recv < size) {
			ssize_t n = recv(conn_fd, buffer + total_recv, size - total_recv, 0);
			if (n <= 0) {
				perror("recv");
				exit(EXIT_FAILURE);
			}
			total_recv += n;
		}

		size_t total_sent = 0;
		while (total_sent < size) {
			ssize_t n = send(conn_fd, buffer + total_sent, size - total_sent, 0);
			if (n <= 0) {
				perror("send");
				exit(EXIT_FAILURE);
			}
			total_sent += n;
		}
	}

	// Close the connection
	free(buffer);
	close(conn_fd);
	close(listen_fd);
	return 0;
}