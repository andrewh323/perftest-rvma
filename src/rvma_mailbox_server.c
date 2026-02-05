#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rvma_mailbox_hashmap.h"
#include "rvma_write.h"

#define PORT 7471


uint32_t get_host_ip(const char *iface_name) {
    struct ifaddrs *ifaddr, *ifa;
    uint32_t ip = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        
        if (strcmp(ifa->ifa_name, iface_name) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            ip = ntohl(sa->sin_addr.s_addr);
            break;
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}


uint64_t construct_vaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}


int main(int argc, char **argv) {
    const char *iface_name = "ib0"; // Search for RDMA device
    uint16_t reserved = 0x0001; // Reserved 16 bits for vaddr structure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    struct rdma_cm_event *event;

    uint32_t host_ip = get_host_ip(iface_name);

    
	addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);
    // Construct virtual address
    uint64_t vaddr = construct_vaddr(reserved, ip_host_order, PORT);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    // Calls newMailboxIntoHashmap, which calls setupMailbox, which gets the
    // key, bufferQueue, ec, rdma cm_id
    RVMA_Win *windowPtr = rvmaInitWindowMailbox(vaddr);
    if (!windowPtr) {
        fprintf(stderr, "Failed to initialize RVMA window mailbox\n");
        return -1;
    }

    RVMA_Status res = newMailboxIntoHashmap(windowPtr->hashMapPtr, vaddr);
    if (res != RVMA_SUCCESS) {
        freeHashmap(&windowPtr->hashMapPtr);
        free(windowPtr);
        print_error("rvmaInitWindowMailbox: Failure creating mailbox in the hashmap...");
        return -1;
    }

    RVMA_Mailbox *mailboxPtr = searchHashmap(windowPtr->hashMapPtr, vaddr);
    if (!mailboxPtr) {
        fprintf(stderr, "Failed to get mailbox for vaddr = %" PRIu64 "\n", vaddr);
        return -1;
    }

    // Create RDMA cm_id
    struct rdma_cm_id *cm_id;
    struct rdma_event_channel *ec = rdma_create_event_channel();
    if (rdma_create_id(ec, &cm_id, NULL, RDMA_PS_TCP)) {
        fprintf(stderr, "rdma_create_id failed\n");
        return -1;
    }
    mailboxPtr->cm_id = cm_id;
    mailboxPtr->ec = ec;

    // Bind cm_id to address
    rdma_bind_addr(mailboxPtr->cm_id, (struct sockaddr *)&addr);
    
    // Listen for incoming connections
    printf("Listening for incoming connections...\n");
    rdma_listen(mailboxPtr->cm_id, 1);

    rdma_get_cm_event(mailboxPtr->ec, &event);
    struct rdma_cm_id *client_cm_id = event->id;

    // Define protection domain
    struct ibv_pd *pd = ibv_alloc_pd(client_cm_id->verbs);
    mailboxPtr->pd = pd;
    if (!pd) {
        perror("ibv_alloc_pd failed");
        return -1;
    }

    mailboxPtr->cq = ibv_create_cq(client_cm_id->verbs, 16, NULL, NULL, 0);
    if (!mailboxPtr->cq) {
        perror("ibv_create_cq failed");
        return -1;
    }

    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = mailboxPtr->cq,
        .recv_cq = mailboxPtr->cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };
    if (rdma_create_qp(client_cm_id, mailboxPtr->pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }
    
    // Link mailbox qp and cm_id
    mailboxPtr->cm_id = client_cm_id;
    mailboxPtr->qp = client_cm_id->qp;

    // Accept incoming connection
    rdma_accept(mailboxPtr->cm_id, NULL);
    rdma_ack_cm_event(event);

    printf("Server accepted connection and created qp\n");

    // Prepost buffers
    res = postSendPool(mailboxPtr, 16, vaddr, EPOCH_BYTES);
    if (res != RVMA_SUCCESS) {
        perror("postSendPool failed");
        return -1;
    }
    res = postRecvPool(mailboxPtr, 16, vaddr, EPOCH_BYTES);
    if (res != RVMA_SUCCESS) {
        perror("postRecvPool failed");
        return -1;
    }

    uint64_t t2;
    int size = 10;
	int num_sends = 100;

    // Construct messages
	char *messages[num_sends];
    for (int i = 0; i < num_sends; i++) {
        messages[i] = malloc(size);
        memset(messages[i], 'A', size);
        snprintf(messages[i], size, "Msg %d", i);
    }
    
	for (int i = 0; i < num_sends; i++) {
		rvmaRecv(vaddr, mailboxPtr, &t2);
		rvmaSend(messages[i], size, vaddr, mailboxPtr);
	}
}