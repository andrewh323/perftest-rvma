
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

#define PORT 7471

int main() {
	int listen_fd, conn_fd;
	struct sockaddr_in addr;
	char buffer[1024];

	listen_fd = rsocket(AF_INET, SOCK_STREAM, 0);

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET; // IPv4
	// htons converts port number from host byte order to network byte order
	addr.sin_port = htons(PORT);
	// INADDR_ANY is a constant that represents any address (0.0.0.0)
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	// Now we can bind the socket to the address
	rbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));

	// Listen for incoming connections
	rlisten(listen_fd, 5);
	printf("Server listening on port %d...\n", PORT);

	// Accept a connection from client
	conn_fd = raccept(listen_fd, NULL, NULL);
	printf("Client successfully connected!\n");

	// Receive data from client
	rrecv(conn_fd, buffer, sizeof(buffer), 0);
	printf("Server received message: %s\n", buffer);

	// Send a response back to the client
	char *response = "Hello from the server! Message was received!";
	rsend(conn_fd, response, strlen(response) + 1, 0);

	// Close the connection
	rclose(conn_fd);
	rclose(listen_fd);
	return 0;
}