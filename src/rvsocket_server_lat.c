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
#include <rdma/rsocket.h>
#include <arpa/inet.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

int main(int argc, char **argv) {
	int listen_fd, conn_fd;
	struct sockaddr_in client_addr;
	memset(&client_addr, 0, sizeof(client_addr));

	client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &client_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

	client_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
	uint32_t ip_host_order = ntohl(client_addr.sin_addr.s_addr);

	RVMA_Win *windowPtr = rvmaInitWindowMailbox(&client_addr.sin_addr.s_addr);

    listen_fd = rvsocket(AF_INET, ip_host_order, windowPtr);

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