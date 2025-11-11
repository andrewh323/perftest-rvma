#define _GNU_SOURCE
#include <config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <endian.h>
#include <stdarg.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <search.h>
#include <time.h>
#include <stdint.h>
#include <byteswap.h>
#include <util/compiler.h>
#include <util/util.h>
#include <ccan/container_of.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include "perftest_resources.h"
#include "rvma_socket.h"
#include "indexer.h"

#define PORT 7471
#define RS_MAX_TRANSFER 4050 /* 4KB MTU - 40B GRH */ /* set to 4050 so message fragments can be observed*/
#define RS_SNDLOWAT 2048
#define RS_QP_MIN_SIZE 16
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4	/* must be power of 2 */
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2
#define MAX_RECV_BUFS 64
#define SIGNAL_INTERVAL 64

enum {
	RS_OP_DATA,
	RS_OP_RSVD_DATA_MORE,
	RS_OP_WRITE, /* opcode is not transmitted over the network */
	RS_OP_RSVD_DRA_MORE,
	RS_OP_SGL,
	RS_OP_RSVD,
	RS_OP_IOMAP_SGL,
	RS_OP_CTRL
};

static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

#define rvs_send_wr_id(data) ((uint64_t) data)

static struct index_map idm;
struct rvsocket;

union socket_addr {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};

struct rv_dest {
    struct ibv_ah *ah;
    uint32_t qpn;
    uint32_t qkey;
    uint16_t lid;
    uint8_t gid[16];
    uint64_t vaddr;
    int port_num;
};

/*
 * rsocket states are ordered as passive, connecting, connected, disconnected.
 */
enum rs_state {
	rs_init,
	rs_bound	   =		    0x0001,
	rs_listening	   =		    0x0002,
	rs_opening	   =		    0x0004,
	rs_resolving_addr  = rs_opening |   0x0010,
	rs_resolving_route = rs_opening |   0x0020,
	rs_connecting      = rs_opening |   0x0040,
	rs_accepting       = rs_opening |   0x0080,
	rs_connected	   =		    0x0100,
	rs_writable 	   =		    0x0200,
	rs_readable	   =		    0x0400,
	rs_connect_rdwr    = rs_connected | rs_readable | rs_writable,
	rs_connect_error   =		    0x0800,
	rs_disconnected	   =		    0x1000,
	rs_error	   =		    0x2000,
};

struct rvsocket {
    int type; // SOCK_STREAM or SOCK_DGRAM
    int index;
    union {
        struct { // data stream
            struct rdma_cm_id *cm_id; // RDMA CM ID
        };
        struct { // datagram
            int udp_sock; // UDP socket for exchanging connection data
            int sq_size; // Current size of send queue
            int sq_limit; // Max size of send queue
        };
    };
    int qp_port; // QP port num
    int state;
    int err;
    
    RVMA_Mailbox *mailboxPtr;
    uint64_t vaddr;
    struct rv_dest *dest;
};


static int rs_insert(struct rvsocket *rvs, int index)
{
	pthread_mutex_lock(&mut);
	rvs->index = idm_set(&idm, index, rvs);
	pthread_mutex_unlock(&mut);
	return rvs->index;
}

static void rs_remove(struct rvsocket *rvs)
{
	pthread_mutex_lock(&mut);
	idm_clear(&idm, rvs->index);
	pthread_mutex_unlock(&mut);
}

static void ds_free(struct rvsocket *rvs) {
    if (rvs->udp_sock >= 0)
        close(rvs->udp_sock);
    if (rvs->index >= 0)
        rs_remove(rvs);
    free(rvs);
}

static void rs_free(struct rvsocket *rvs) {
    if (rvs->type == SOCK_DGRAM) {
        ds_free(rvs);
        return;
    }
    if (rvs->index >= 0) {
        rs_remove(rvs);
    }
    if (rvs->cm_id) {
        rdma_destroy_id(rvs->cm_id);
    }
    free(rvs);
}

// Helper to construct virtual address
uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}

// Helpers to get port and ip from vaddr
uint16_t getPort(uint64_t vaddr) {
    return (uint16_t)(vaddr & 0xFFFF);
}
uint32_t getIP(uint64_t vaddr) {
    return (uint32_t)((vaddr >> 16) & 0xFFFFFFFF);
}

// Function to measure clock cycles
static inline uint64_t rdtsc(){
    unsigned int lo, hi;
    // Serialize to prevent out-of-order execution affecting timing
    asm volatile ("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Create rvsocket using rdma_create_id (called in rvmaInitWindowMailbox)
// Return socketfd after inserting into idm
uint64_t rvsocket(int type, uint64_t vaddr, RVMA_Win *window) {
    double cpu_ghz = get_cpu_ghz();
	uint64_t start, end = 0;
    start = rdtsc();
    struct rvsocket *rvs;
    int index, ret;

    rvs = calloc(1, sizeof(*rvs));
    if (!rvs)
        return -1;
    
    rvs->type = type;
    rvs->index = -1;

    // Set rvsocket vaddr and mailbox
    rvs->vaddr = vaddr;
    rvs->mailboxPtr = searchHashmap(window->hashMapPtr, &rvs->vaddr);
    if (rvs->mailboxPtr == NULL) {
        fprintf(stderr, "rvsocket: Failed to find mailbox for vaddr = %" PRIu64 "\n", rvs->vaddr);
        free(rvs);
        return -1;
    }
    rvs->mailboxPtr->type = type; // Set mailbox type

    uint64_t mailboxSetup = rdtsc();
    double mailboxTime = (mailboxSetup - start) / (cpu_ghz * 1e3);

    if (type == SOCK_STREAM) {
        // For stream sockets, pd, cq, and qp are allocated in accept/connect
        // Use the mailbox's CM ID (should already be set when mailbox is created)
        index = rvs->mailboxPtr->cm_id->channel->fd;
        rvs->index = index;
    } else { // datagram
        // Datagrams do not accept/connect, so we must setup pd, cq, and qp here
        // To allocate pd, we need a valid context
        char *devname = "mlx5_0"; // mlx_0/1/2 probably - change as needed
        struct ibv_device *ib_dev = ctx_find_dev(&devname);
        if (!ib_dev) {
            fprintf(stderr, "rvsocket: Failed to find IB device\n");
            return -1;
        }
        struct ibv_context *ctx = ibv_open_device(ib_dev);
        if (!ctx) {
            fprintf(stderr, "rvsocket: Failed to open device\n");
            return -1;
        }

        struct ibv_device_attr dev_attr;
        if (ibv_query_device(ctx, &dev_attr)) {
            perror("ibv_query_device");
            return -1;
        }
        struct ibv_port_attr port_attr;
        int ret = ibv_query_port(ctx, 1, &port_attr);
        if (ret) {
            perror("ibv_query_port failed");
            return -1;
        }
        rvs->qp_port = 1;

        // Allocate pd, cq, and qp here
        struct ibv_pd *pd = ibv_alloc_pd(ctx);
        if (!pd) {
            fprintf(stderr, "rvsocket: Failed to allocate pd\n");
            return -1;
        }
        rvs->mailboxPtr->pd = pd;
        
        struct ibv_cq *cq = ibv_create_cq(ctx, 128, NULL, NULL, 0);
        if (!cq) {
            fprintf(stderr, "rvsocket: Failed to create cq\n");
            return -1;
        }
        rvs->mailboxPtr->cq = cq;

        // Define QP
        struct ibv_qp_init_attr qp_attr = {
            .send_cq = cq,
            .recv_cq = cq,
            .qp_type = IBV_QPT_UD,
            .cap = {
                .max_send_wr = 128,
                .max_recv_wr = 128,
                .max_send_sge = 1,
                .max_recv_sge = 1
            },
        };
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);
        if (!qp) {
            perror("ibv_create_qp failed");
            return -1;
        }

        // Now transition the qp to RTS
        // INIT
        struct ibv_qp_attr attr = {0};
        attr.qp_state   = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num   = rvs->qp_port;
        attr.qkey       = 0x11111111;
        int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
        if(ibv_modify_qp(qp, &attr, mask)) {
            perror("INIT transition failed");
            return -1;
        }
        // RTR
        attr.qp_state = IBV_QPS_RTR;
        if(ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            perror("RTR transition failed");
            return -1;
        }
        // RTS
        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = lrand48() & 0xffffff;
        mask = IBV_QP_STATE | IBV_QP_SQ_PSN;
        if (ibv_modify_qp(qp, &attr, mask)) {
            perror("RTS transition failed");
            return -1;
        }
        // Save qp to rvs
        rvs->mailboxPtr->qp = qp;
        if (postRecvPool(rvs->mailboxPtr, MAX_RECV_BUFS, vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
            perror("postRecvPool failed");
            return -1;
        }
        rvs->sq_size = 0;
        rvs->sq_limit = 128; // Match max_send_wr

        // Create socket index for insertion
        rvs->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        rvs->index = rvs->udp_sock;
        index = rvs->index;
    }
    uint64_t beforeInsert = rdtsc();
    
    // Insert rvsocket into index map
    ret = rs_insert(rvs, index);
    if (ret < 0) {
        fprintf(stderr, "Failed to insert rvsocket at index %d\n", index);
        rs_free(rvs);
        return ret;
    }
    end = rdtsc();
    printf("rvsocket mailbox setup time: %.3f µs\n", mailboxTime);
    double insertTime = (end - beforeInsert) / (cpu_ghz * 1e3);
    printf("rvsocket insert time: %.3f µs\n", insertTime);
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvsocket total setup time: %.3f µs\n", elapsed_us);
    // return rvsocket index
    return rvs->index;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end;
    start = rdtsc();
    struct rvsocket *rvs;
	int ret = 0;

	rvs = idm_lookup(&idm, socket);
	if (!rvs) {
		fprintf(stderr, "rvbind: rvs is NULL\n");
        return -1;
    }
    if (rvs->type == SOCK_STREAM) {
	    ret = rdma_bind_addr(rvs->mailboxPtr->cm_id, (struct sockaddr *)addr);
        if (!ret)
            rvs->state = rs_bound;
    } else { // Datagram
        if (rvs->state == rs_init) {
            ret = bind(rvs->udp_sock, addr, addrlen);
            if (ret == 0)
                rvs->state = rs_bound;
        } else {
            // Already bound
            ret = 0;
        }
    }
    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvbind total time: %.3f µs\n", elapsed_us);
    return ret;
}


int rvlisten(int socket, int backlog) {
    struct rvsocket *rvs;
    int ret;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        fprintf(stderr, "rlisten: rvs is NULL");
        return -1;
    }

    if (rvs->state == rs_listening) {
        fprintf(stderr, "rlisten: rvs is already listening");
        return 0;
    }

    // Listen on server cm_id
    ret = rdma_listen(rvs->mailboxPtr->cm_id, backlog);
    if (ret) {
        perror("rdma_listen failed");
        return ret;
    }

    rvs->state = rs_listening;
    return 0;
}


int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end;
    struct rvsocket *rvs, *new_rvs;
    struct rdma_cm_event *event;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        fprintf(stderr, "rvaccept: rvs is NULL");
        return -1;
    }

    if (rvs->state != rs_listening) {
        fprintf(stderr, "rvaccept: rs is not in listening state");
        return -1;
    }
    
    // Poll for connection request
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }

    start = rdtsc();
    // Check if event is a connection request
    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        fprintf(stderr, "Unexpected event: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    struct rdma_cm_id *client_cm_id = event->id;

    // Define protection domain
    struct ibv_pd *pd = ibv_alloc_pd(client_cm_id->verbs);
    if (!pd) {
        perror("ibv_alloc_pd failed");
        ibv_dealloc_pd(pd);
        return -1;
    }
    rvs->mailboxPtr->pd = pd;

    // Create completion queue
    struct ibv_cq *cq = ibv_create_cq(client_cm_id->verbs, 1024, NULL, NULL, 0);
    if (!cq) {
        perror("ibv_create_cq failed");
        ibv_dealloc_pd(pd);
        return -1;
    }
    rvs->mailboxPtr->cq = cq;

    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 128,
            .max_recv_wr = 128,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    if (rdma_create_qp(client_cm_id, pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }

    // Accept connection
    if (rdma_accept(client_cm_id, NULL)) {
        perror("rdma_accept");
        rdma_ack_cm_event(event);
        return -1;
    }
    rdma_ack_cm_event(event);

    uint64_t rdmaSetup = rdtsc();
    double rdmaTime = (rdmaSetup - start) / (cpu_ghz * 1e3);

    // Create a new rvsocket for accepted connection
    new_rvs = calloc(1, sizeof(*new_rvs));
    if (!new_rvs) {
        perror("calloc");
        return -1;
    }

    // Fill in new rvsocket fields
    new_rvs->vaddr = rvs->vaddr;
    new_rvs->mailboxPtr = rvs->mailboxPtr;
    new_rvs->mailboxPtr->cm_id = client_cm_id;
    new_rvs->mailboxPtr->pd = pd;
    new_rvs->mailboxPtr->cq = cq;
    new_rvs->mailboxPtr->qp = client_cm_id->qp;
    new_rvs->index = client_cm_id->channel->fd;
    new_rvs->state = rs_connected;

    // Prepost recv buffer pool
    uint64_t beforePostRecv = rdtsc();
    RVMA_Status status = postRecvPool(rvs->mailboxPtr, MAX_RECV_BUFS, rvs->vaddr, EPOCH_BYTES);
    if (status != RVMA_SUCCESS) {
        fprintf(stderr, "rvaccept: postRecvPool failed\n");
        return -1;
    }
    uint64_t afterPostRecv = rdtsc();
    double postRecvTime = (afterPostRecv - beforePostRecv) / (cpu_ghz * 1e3);

    // Insert new rvsocket into index map
    rs_insert(new_rvs, new_rvs->index);

    if (addr && addrlen)
        rgetpeername(new_rvs->index, addr, addrlen);

    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvaccept time for RDMA resources setup: %.3f µs\n", rdmaTime);
    printf("rvaccept time for postRecvPool: %.3f µs\n", postRecvTime);
    printf("rvaccept total time: %.3f µs\n", elapsed_us);

    return new_rvs->index;
}


// Accepts datagram connection and exchanges endpoint info
int rvaccept_dgram(int dgram_fd, int tcp_listenfd, struct sockaddr *addr, socklen_t *addrlen) {
    struct rvsocket *rvs;
    struct rv_dest local_info, remote_info;
    int tcp_fd;

    rvs = idm_lookup(&idm, dgram_fd);

    if (!rvs->dest) {
        rvs->dest = calloc(1, sizeof(*rvs->dest));
        if (!rvs->dest) {
            perror("calloc rvs->dest");
            return -1;
        }
    }

    tcp_fd = accept(tcp_listenfd, addr, addrlen);
    if (tcp_fd < 0) {
        perror("rvaccept_dgram: accept failed");
        return -1;
    }
    struct ibv_port_attr port_attr;
    ibv_query_port(rvs->mailboxPtr->pd->context, rvs->qp_port, &port_attr);
    local_info.lid  = port_attr.lid;
    local_info.qpn  = rvs->mailboxPtr->qp->qp_num;
    local_info.qkey = 0x11111111;
    local_info.port_num = rvs->qp_port;
    
    ssize_t n;
    n = write(tcp_fd, &local_info, sizeof(local_info));
    if (n != sizeof(local_info)) {
        perror("write local_info");
        return -1;
    }
    n = read(tcp_fd, &remote_info, sizeof(remote_info));
    if (n != sizeof(remote_info)) {
        perror("read remote_info");
        return -1;
    }
    
    struct ibv_ah_attr ah_attr = {
        .is_global     = 0,
        .dlid          = remote_info.lid,
        .sl            = 0,
        .src_path_bits = 0,
        .port_num      = rvs->qp_port
    };

    struct ibv_ah *ah = ibv_create_ah(rvs->mailboxPtr->pd, &ah_attr);
    if (!ah) {
        perror("rvaccept_dgram: ibv_create_ah failed");
        close(tcp_fd);
        return -1;
    }

    rvs->dest->ah = ah;
    rvs->dest->qpn = remote_info.qpn;
    rvs->dest->qkey = remote_info.qkey;

    close(tcp_fd);
    rvs->state = rs_connected;
    return rvs->index;
}


int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    uint64_t start, end;
    double cpu_ghz = get_cpu_ghz();
    start = rdtsc();
    struct rvsocket *rvs;
    struct rdma_cm_event *event;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        fprintf(stderr, "rvconnect: rvs is NULL\n");
        return -1;
    }

    // Resolve address
    if (rdma_resolve_addr(rvs->mailboxPtr->cm_id, NULL, (struct sockaddr *)addr, 2000)) {
        perror("rdma_resolve_addr");
        return -1;
    }
    // Wait for address resolved event
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        fprintf(stderr, "rdma_resolve_addr failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    // Resolve route
    if (rdma_resolve_route(rvs->mailboxPtr->cm_id, 2000)) {
        perror("rdma_resolve_route");
        return -1;
    }
    // Wait for route resolved event
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        fprintf(stderr, "rdma_resolve_route failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    uint64_t addrRouteResolved = rdtsc();
    double addrRouteTime = (addrRouteResolved - start) / (cpu_ghz * 1e3);
    printf("rvconnect time for address and route resolution: %.3f µs\n", addrRouteTime);

    // Allocate PD
    struct ibv_pd *pd = ibv_alloc_pd(rvs->mailboxPtr->cm_id->verbs);
    if (!pd) {
        perror("ibv_alloc_pd failed");
        ibv_dealloc_pd(pd);
        return -1;
    }
    rvs->mailboxPtr->pd = pd;

    // Create CQ
    struct ibv_cq *cq = ibv_create_cq(rvs->mailboxPtr->cm_id->verbs, 16, NULL, NULL, 0);
    if (!cq) {
        perror("ibv_create_cq failed");
        ibv_dealloc_pd(pd);
        return -1;
    }
    rvs->mailboxPtr->cq = cq;

    // Create QP
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 128,
            .max_recv_wr = 128,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    if (rdma_create_qp(rvs->mailboxPtr->cm_id, pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }
    rvs->mailboxPtr->qp = rvs->mailboxPtr->cm_id->qp;

    uint64_t rdmaSetup = rdtsc();
    double rdmaTime = (rdmaSetup - addrRouteResolved) / (cpu_ghz * 1e3);
    printf("rvconnect time for RDMA resources setup: %.3f µs\n", rdmaTime);

    // Connect
    if (rdma_connect(rvs->mailboxPtr->cm_id, NULL)) {
        perror("rdma_connect");
        return -1;
    }
    // Wait for CM event
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ESTABLISHED) {
        fprintf(stderr, "rdma_connect failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    rdma_ack_cm_event(event);

    end = rdtsc();
    double connectTime = (end - rdmaSetup) /(cpu_ghz * 1e3);
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvconnect time for RDMA connect: %.3f µs\n", connectTime);
    printf("rvconnect total time: %.3f µs\n", elapsed_us);

    return 0;
}


// Connects to datagram socket and exchanges endpoint info
int rvconnect_dgram(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
    struct rv_dest local_info, remote_info;
    int tcp_fd;

    rvs = idm_lookup(&idm, sockfd);
    if (!rvs) {
        fprintf(stderr, "rvconnect_dgram: rvs is NULL\n");
        return -1;
    }
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        perror("socket failed");
        return -1;
    }
    if (!rvs->dest) {
        rvs->dest = calloc(1, sizeof(*rvs->dest));
        if (!rvs->dest) {
            perror("calloc rvs->dest");
            close(tcp_fd);
            return -1;
        }
    }

    if (connect(tcp_fd, addr, addrlen) < 0) {
        perror("connect failed");
        close(tcp_fd);
        return -1;
    }

    // Fill in local UD info
    struct ibv_port_attr port_attr;
    ibv_query_port(rvs->mailboxPtr->pd->context, rvs->qp_port, &port_attr);

    local_info.lid  = port_attr.lid;
    local_info.qpn  = rvs->mailboxPtr->qp->qp_num;
    local_info.qkey = 0x11111111;
    local_info.port_num = rvs->qp_port;

    // Exchange endpoint info
    ssize_t n;
    n = read(tcp_fd, &remote_info, sizeof(remote_info));
    if (n != sizeof(remote_info)) {
        perror("read remote_info");
        return -1;
    }
    n = write(tcp_fd, &local_info, sizeof(local_info));
    if (n != sizeof(local_info)) {
        perror("write local_info");
        return -1;
    }

    // Build AH from remote info
    struct ibv_ah_attr ah_attr = {
        .is_global     = 0,
        .dlid          = remote_info.lid,
        .sl            = 0,
        .src_path_bits = 0,
        .port_num      = rvs->qp_port
    };

    struct ibv_ah *ah = ibv_create_ah(rvs->mailboxPtr->pd, &ah_attr);
    if (!ah) {
        perror("rvconnect_dgram: ibv_create_ah failed");
        close(tcp_fd);
        return -1;
    }
    rvs->dest->ah = ah;
    rvs->dest->qpn  = remote_info.qpn;
    rvs->dest->qkey = remote_info.qkey;

    close(tcp_fd);
    rvs->state = rs_connected;

    return 0;
}


// Send for stream sockets
int rvsend(int socket, void *buf, int64_t len) {
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;
    if (rvs->type == SOCK_STREAM) {
        if (rvmaSend(buf, len, &vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
        fprintf(stderr, "rvmaSend failed\n");
        return -1;
        }
    }
    else {
        fprintf(stderr, "rvsend: Socket type is not stream\n");
    }
    return 0;
}

// Send for datagram sockets
int rvsendto(int socket, void *buf, int64_t len) {
    uint64_t frag_setup = 0, buffer_setup = 0, wr_setup = 0, total_poll = 0;
    uint64_t start = rdtsc();

    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);

    vaddr = rvs->vaddr;
    struct rv_dest *dest = rvs->dest;

    int *notifBuffPtr = malloc(sizeof(int));
    *notifBuffPtr = 0;
    int *notifLenPtr = malloc(sizeof(int));
    *notifLenPtr = 0;

    // Store array of completed buffers to free after signaling
    RVMA_Buffer_Entry *pendingBuffers[SIGNAL_INTERVAL];
    int pendingCount = 0;
    for (int i = 0; i < SIGNAL_INTERVAL; ++i) pendingBuffers[i] = NULL;
    int retired_idx = 0;

    // Determine number of fragments (bufferLen/MTU)
    int64_t threshold = (len + RS_MAX_TRANSFER - 1) / RS_MAX_TRANSFER;

    for (int offset = 0; offset < threshold; offset++) {
        uint64_t frag_setup_start = rdtsc();
        // Define message fragment
        // i=0->RS_MAX_TRANSFER-1, RS_MAX_TRANSFER->2*RS_MAX_TRANSFER-1, ...
        int64_t frag_size = (offset == threshold - 1) ? (len - offset * RS_MAX_TRANSFER) : RS_MAX_TRANSFER;
        void *frag_ptr = (uint8_t *)buf + offset * RS_MAX_TRANSFER;

        // Construct fragment header (for ordering at receiver)
        struct dgram_frag_header header = {
            .frag_num = offset + 1,
            .total_frags = threshold
        };

        size_t total_size = sizeof(header) + frag_size;
        // Allocate buffer for fragment + header
        void *full_buf = malloc(total_size);
        if (!full_buf) {
            fprintf(stderr, "rvsendto: malloc full_buf failed\n");
            return -1;
        }

        // Copy header and fragment into full buffer
        memcpy(full_buf, &header, sizeof(header));
        memcpy((uint8_t *)full_buf + sizeof(header), frag_ptr, frag_size);

        struct dgram_frag_header *hdr = (struct dgram_frag_header *)full_buf;
        char *payload = (char *)full_buf + sizeof(*hdr);

        uint64_t frag_setup_end = rdtsc();
        frag_setup += frag_setup_end - frag_setup_start;

        printf("Sending fragment %d/%d (%zu byte payload) | Payload: %.40s...\n",
            hdr->frag_num, hdr->total_frags, frag_size, payload);

        uint64_t buffer_setup_start = rdtsc();

        // Post buffer to mailbox, fill mailbox entry
        RVMA_Buffer_Entry *entry = rvmaPostBuffer((void *)&full_buf, total_size, (void *)notifBuffPtr, (void *)notifLenPtr, vaddr,
                                    rvs->mailboxPtr, threshold, EPOCH_OPS);
        if (!entry) {
            fprintf(stderr, "rvsendto: rvmaPostBuffer failed\n");
            return -1;
        }
        entry->realBuff = full_buf;

        uint64_t buffer_setup_end = rdtsc();
        buffer_setup += buffer_setup_end - buffer_setup_start;

        // Build sge, wr
        struct ibv_sge sge = {
            .addr = (uintptr_t)entry->realBuff,
            .length = total_size,
            .lkey = entry->mr->lkey
        };

        int send_flag;
        if ((rvs->sq_size % SIGNAL_INTERVAL == 0 && rvs->sq_size != 0))
            send_flag = IBV_SEND_SIGNALED;
        else
            send_flag = 0;

        struct ibv_send_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id              = (uintptr_t)entry;
        wr.sg_list            = &sge;
        wr.num_sge            = 1;
        wr.opcode             = IBV_WR_SEND;
        wr.send_flags         = send_flag;
        wr.wr.ud.ah           = dest->ah;
        wr.wr.ud.remote_qpn   = dest->qpn;
        wr.wr.ud.remote_qkey  = dest->qkey;

        struct ibv_send_wr *bad_wr = NULL;

        if (ibv_post_send(rvs->mailboxPtr->qp, &wr, &bad_wr)) {
            perror("rvmasendto: ibv_post_send failed");
            return -1;
        }
        uint64_t wr_setup_end = rdtsc();
        wr_setup += wr_setup_end - buffer_setup_end;

        uint64_t poll_start = rdtsc();

        // Store buffer to retire
        pendingBuffers[pendingCount++] = entry;

        rvs->sq_size++;

        printf("sq_size: %d\n", rvs->sq_size);
        printf("Size of buffer queue: %d\n", rvs->mailboxPtr->bufferQueue->size);

        // Completion generated every SIGNAL_INTERVAL sends, which indicates all prior sends are complete
        if (send_flag == IBV_SEND_SIGNALED) {
            struct ibv_wc wc;
            int num_wc = 0;
            do {
                num_wc = ibv_poll_cq(rvs->mailboxPtr->cq, 1, &wc);
            } while (num_wc == 0);

            if (num_wc < 0 || wc.status != IBV_WC_SUCCESS) {
                fprintf(stderr, "rvsendto: ibv_poll_cq failed\n");
                return -1;
            }
            // Free all completed buffers
            for (int i = 0; i < pendingCount; i++) {
                if (pendingBuffers[i]) {
                    printf("Freeing Buffers!\n");
                    removeEntry(rvs->mailboxPtr->bufferQueue, pendingBuffers[i]);
                    freeBufferEntry(pendingBuffers[i]);
                    pendingBuffers[i] = NULL;
                }
            }
            rvs->sq_size -= pendingCount;
            pendingCount = 0;
        }
        uint64_t poll_end = rdtsc();
        total_poll += poll_end - poll_start;
    }

    free(notifBuffPtr);
    free(notifLenPtr);
    uint64_t end = rdtsc();
    double elapsed = end - start;
    double avg_frag_setup = frag_setup / (double)threshold;
    double avg_buffer_setup = buffer_setup / (double)threshold;
    double avg_wr_setup = wr_setup / (double)threshold;
    double avg_poll = total_poll / (double)threshold;

    rvs->mailboxPtr->cycles = elapsed;
    rvs->mailboxPtr->fragSetupCycles = avg_frag_setup;
    rvs->mailboxPtr->bufferSetupCycles = avg_buffer_setup;
    rvs->mailboxPtr->wrSetupCycles = avg_wr_setup;
    rvs->mailboxPtr->pollCycles = avg_poll;
    return 0;
}


int rvrecv(int socket, RVMA_Win *windowPtr) {
    // Read from mailbox buffer with rvmaRecv
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;

    RVMA_Mailbox *mailbox = rvs->mailboxPtr;

    if (rvs->type == SOCK_DGRAM) {
        if (rvrecvfrom(mailbox) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaRecvfrom failed\n");
            return -1;
        }
    } else {
        if (rvmaRecv(&vaddr, mailbox) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaRecv failed\n");
            return -1;
        }
    }
    return 0;
}