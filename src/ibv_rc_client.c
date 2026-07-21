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

static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}


int main(int argc, char **argv) {
    const char *iface_name = "ib0"; // Search for RDMA device
    struct sockaddr_in addr;
    double cpu_ghz = get_cpu_ghz();
    memset(&addr, 0, sizeof(addr));

    struct rdma_cm_event *event;

    uint32_t host_ip = get_host_ip(iface_name);

    
	addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

    uint32_t ip_host_order = ntohl(addr.sin_addr.s_addr);

    // Create RDMA cm_id
    struct rdma_cm_id *cm_id;
    struct rdma_event_channel *ec = rdma_create_event_channel();
    if (rdma_create_id(ec, &cm_id, NULL, RDMA_PS_TCP)) {
        fprintf(stderr, "rdma_create_id failed\n");
        return -1;
    }

    // Bind cm_id to address
    rdma_bind_addr(cm_id, (struct sockaddr *)&addr);

    rdma_get_cm_event(ec, &event);
    struct rdma_cm_id *client_cm_id = event->id;

    if (rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&addr, 2000)) {
        perror("rdma_resolve_addr");
        return -1;
    }
    if (rdma_get_cm_event(ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        fprintf(stderr, "rdma_resolve_addr failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    // Resolve route
    if (rdma_resolve_route(cm_id, 2000)) {
        perror("rdma_resolve_route");
        return -1;
    }
    if (rdma_get_cm_event(ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        fprintf(stderr, "rdma_resolve_route failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    rdma_ack_cm_event(event);

    // Define protection domain
    struct ibv_pd *pd = ibv_alloc_pd(client_cm_id->verbs);
    if (!pd) {
        perror("ibv_alloc_pd failed");
        return -1;
    }

    struct ibv_cq *send_cq = ibv_create_cq(client_cm_id->verbs, 16, NULL, NULL, 0);
    if (!send_cq) {
        perror("ibv_create_cq failed");
        return -1;
    }

    struct ibv_cq *recv_cq = ibv_create_cq(client_cm_id->verbs, 16, NULL, NULL, 0);
    if (!recv_cq) {
        perror("ibv_create_cq failed");
        return -1;
    }

    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = send_cq,
        .recv_cq = recv_cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };
    if (rdma_create_qp(client_cm_id, pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }
    
    // Link mailbox qp and cm_id
    cm_id = client_cm_id;
    struct ibv_qp *qp = client_cm_id->qp;

    // Connect
    if (rdma_connect(cm_id, NULL)) {
        perror("rdma_connect");
        return -1;
    }
    if (rdma_get_cm_event(ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ESTABLISHED) {
        fprintf(stderr, "rdma_connect failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    rdma_ack_cm_event(event);

    printf("Server accepted connection and created qp\n");

	int num_sends = 1000;
    int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    printf("Sending messages of size %d bytes\n", size);

    printf("Beginning send/recv loop\n");

    void *buf = malloc(size);
    RVMA_Status status;
    struct ibv_recv_wr *bad_wr = NULL;
    struct ibv_send_wr *bad_send_wr = NULL;

    struct ibv_mr *send_mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
    if (!send_mr) {
        perror("ibv_reg_mr failed");
        return -1;
    }

    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = size,
        .lkey = send_mr->lkey
    };

    struct ibv_recv_wr recv_wr = {
        .wr_id = (uintptr_t)buf,
        .sg_list = &sge,
        .num_sge = 1,
        .next = NULL
    };
    struct ibv_send_wr send_wr = {
        .wr_id = (uintptr_t)buf,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,
        .next = NULL
    };

    struct ibv_wc recv_wc, send_wc;
    int ne;
    
	for (int i = 0; i < num_sends; i++) {
        // Send
        if (ibv_post_send(qp, &send_wr, &bad_send_wr) < 0) {
            fprintf(stderr, "ibv_post_send failed\n");
            return -1;
        }
        do {
            ne = ibv_poll_cq(send_cq, 1, &send_wc);
        } while (ne == 0);
        if (ne < 0 || send_wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "send failed\n");
            return -1;
        }

        // Receive
        if (ibv_post_recv(qp, &recv_wr, &bad_wr) < 0) {
            fprintf(stderr, "ibv_post_recv failed\n");
            return -1;
        }
        do {
            ne = ibv_poll_cq(recv_cq, 1, &recv_wc);
        } while (ne == 0);
        if (ne < 0 || recv_wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "recv failed\n");
            return -1;
        }
    }

    return 0;
}