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
#include <math.h>
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
#define RS_SNDLOWAT 2048
#define RS_QP_MIN_SIZE 16
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4	/* must be power of 2 */
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2
#define MAX_POOL_BUFS 16
#define MAX_RECV_SIZE 1024*1024 // 1MB
#define SIGNAL_INTERVAL 32

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
static int next_fd = 100; // Starting fd for rvsockets

#define rvs_send_wr_id(data) ((uint64_t) data)
#define MAX_PORTS 65536

static uint32_t port_to_qpn[MAX_PORTS];

static struct index_map idm;
struct rvsocket;

union socket_addr {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};

struct ds_header {
    uint8_t length;
    uint16_t port;
    union {
		__be32  ipv4;
		struct {
			__be32 flowinfo;
			uint8_t  addr[16];
		} ipv6;
	} addr;
    int frag_num;
    int total_frags;
};

struct ds_dest {
	union socket_addr addr;	/* must be first */
	struct ds_qp	  *qp;
	struct ibv_ah	  *ah;
	uint32_t	   qpn;
};

struct ds_qp {
	dlist_entry	  list;
	struct rvsocket	  *rvs;
	struct rdma_cm_id *cm_id;
	struct ds_header  hdr;
	struct ds_dest	  dest;

	struct ibv_mr	  *smr;
	struct ibv_mr	  *rmr;
	uint8_t		  *rbuf;

	int		  cq_armed;
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
    struct rdma_cm_id *cm_id; // RDMA CM ID
    int udp_sock; // UDP socket for exchanging connection data
    struct ds_qp	  *qp_list;
    struct ds_dest *conn_dest;

    struct rdma_event_channel *ec;
    int qp_port; // QP port num
    int state;
    
    uint16_t local_lid;
    struct ibv_ah *ah;
    RVMA_Mailbox *mailboxPtr;
    uint64_t vaddr;
    struct ds_dest *dest; // TO REMOVE
};

/* Helper Functions */
static int ds_compare_addr(const void *dst1, const void *dst2)
{
	const struct sockaddr *sa1, *sa2;
	size_t len;

	sa1 = (const struct sockaddr *) dst1;
	sa2 = (const struct sockaddr *) dst2;

	len = (sa1->sa_family == AF_INET6 && sa2->sa_family == AF_INET6) ?
	      sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	return memcmp(dst1, dst2, len);
}

static int ds_get_src_addr(struct rvsocket *rvs,
			   const struct sockaddr *dest_addr, socklen_t dest_len,
			   union socket_addr *src_addr, socklen_t *src_len)
{
	int sock, ret;
	__be16 port;

	*src_len = sizeof(*src_addr);
	ret = getsockname(rvs->udp_sock, &src_addr->sa, src_len);

	port = src_addr->sin.sin_port;
	sock = socket(dest_addr->sa_family, SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	ret = connect(sock, dest_addr, dest_len);
	if (ret)
		goto out;

	*src_len = sizeof(*src_addr);
	ret = getsockname(sock, &src_addr->sa, src_len);
	src_addr->sin.sin_port = port;
out:
	close(sock);
	return ret;
}

static int ds_create_qp(struct rvsocket *rvs, union socket_addr *src_addr,
			socklen_t addrlen, struct ds_qp **new_qp, RVMA_Win *windowPtr)
{
    struct ds_qp *qp;
    struct ibv_qp_init_attr qp_attr;

    qp = calloc(1, sizeof(*qp));
	if (!qp)
		return -1;

    int ret = rdma_create_id(NULL, &rvs->cm_id, rvs, RDMA_PS_UDP);
    if(ret){
        perror("ds_create_qp: rdma_create_id failed");
        return ret;
    }

    // Format headers
    qp->hdr.length = 8;
    qp->hdr.port = src_addr->sin.sin_port;
    qp->hdr.addr.ipv4 = src_addr->sin.sin_addr.s_addr;

    ret = rdma_bind_addr(rvs->cm_id, &src_addr->sa);
    if(ret){
        perror("ds_create_qp: rdma_bind_addr failed");
        return ret;
    }

    // Create new mailbox for this connection
    if (newMailboxIntoHashmap(windowPtr->hashMapPtr, rvs->vaddr) != RVMA_SUCCESS) {
        fprintf(stderr, "ds_create_qp: Failed to create mailbox for new connection\n");
        rdma_destroy_id(rvs->cm_id);
        return -1;
    }

    // TODO: maybe use completion channels to improve performance
    // Create cq
    rvs->cm_id->recv_cq = ibv_create_cq(rvs->cm_id->verbs, 256, rvs->cm_id, NULL, 0);
    if (!rvs->cm_id->recv_cq) {
        perror("ds_create_qp: ibv_create_cq failed");
        rdma_destroy_id(rvs->cm_id);
        return -1;
    }
    rvs->cm_id->send_cq = rvs->cm_id->recv_cq;

    // prepost buffer pools
    if (postSendPool(rvs->mailboxPtr, MAX_POOL_BUFS, rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
        fprintf(stderr, "ds_create_qp: Failed to post send buffer pool for new connection\n");
        rdma_destroy_id(rvs->cm_id);
        return -1;
    }
    if (postRecvPool(rvs->mailboxPtr, MAX_POOL_BUFS, rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
        fprintf(stderr, "ds_create_qp: Failed to post receive buffer pool for new connection\n");
        rdma_destroy_id(rvs->cm_id);
        return -1;
    }

    memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_context = qp;
	qp_attr.send_cq = qp->cm_id->send_cq;
	qp_attr.recv_cq = qp->cm_id->recv_cq;
	qp_attr.qp_type = IBV_QPT_UD;
	qp_attr.sq_sig_all = 1;
	qp_attr.cap.max_send_wr = 128;
	qp_attr.cap.max_recv_wr = 128;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_sge = 2;
	ret = rdma_create_qp(qp->cm_id, NULL, &qp_attr);
	if (ret) {
        perror("ds_create_qp: rdma_create_qp failed");
        rdma_destroy_id(qp->cm_id);
        return ret;
    }

    *new_qp = qp;
    return 0;
}

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

// Create rvsocket
// Return socketfd after inserting into idm
uint64_t rvsocket(int domain, int type, int protocol, uint64_t vaddr, RVMA_Win *window) {
    double cpu_ghz = get_cpu_ghz();
	uint64_t start, end;
    double rdmaTime = 0;
    start = rdtsc();
    struct rvsocket *rvs;
    int ret;

    rvs = calloc(1, sizeof(*rvs));
    if (!rvs)
        return -1;
    
    rvs->type = type;

    // Set rvsocket vaddr and mailbox
    rvs->vaddr = vaddr;
    rvs->ec = rdma_create_event_channel();

    if (rdma_create_id(rvs->ec, &rvs->cm_id, rvs, rvs->type == SOCK_DGRAM ? RDMA_PS_UDP : RDMA_PS_TCP)) {
        rdma_destroy_event_channel(rvs->ec);
        print_error("rvsocket: rdma_create_id failed");
        free(rvs);
        return -1;
    }

    if (type == SOCK_STREAM) {
        // For stream sockets, pd, cq, and qp are allocated in accept/connect
        rvs->index = next_fd++;
    } else { // datagram
        // Create socket index for insertion
        rvs->udp_sock = socket(domain, SOCK_DGRAM, 0);
        rvs->index = rvs->udp_sock;
    }
    // Insert rvsocket into index map
    ret = rs_insert(rvs, rvs->index);
    if (ret < 0) {
        fprintf(stderr, "Failed to insert rvsocket at index %d\n", rvs->index);
        rs_free(rvs);
        return ret;
    }
    end = rdtsc();

    double elapsed_us = (end - start) / (cpu_ghz * 1e3) - rdmaTime;
    printf("rvsocket total setup time: %.3f µs\n", elapsed_us);
    // return rvsocket index
    return rvs->index;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end;
    start = rdtsc();
    struct rvsocket *rvs;
	int ret = -1;

	rvs = idm_lookup(&idm, socket);
	if (!rvs) {
		fprintf(stderr, "rvbind: rvs is NULL\n");
        return -1;
    }
    if (rvs->type == SOCK_STREAM) {
	    ret = rdma_bind_addr(rvs->cm_id, (struct sockaddr *)addr);
        if (!ret)
            rvs->state = rs_bound;
    } else { // Datagram
        if (rvs->state == rs_init) {
            ret = bind(rvs->udp_sock, addr, addrlen);
            if (ret == 0)
                rvs->state = rs_bound;
        }
    }
    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvbind total time: %.3f µs\n", elapsed_us);
    return ret;
}


int rvlisten(int socket, int backlog) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start = rdtsc();
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
    ret = rdma_listen(rvs->cm_id, backlog);
    if (ret) {
        perror("rdma_listen failed");
        return ret;
    }

    rvs->state = rs_listening;
    uint64_t end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvlisten total time: %.3f µs\n", elapsed_us);
    return 0;
}


int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen, RVMA_Win *window) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end;
    struct rvsocket *rvs, *new_rvs;
    struct rdma_cm_event *event;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        fprintf(stderr, "rvaccept: rvs is NULL");
        return -1;
    }
    
    if (rvs->ec == NULL) {
        fprintf(stderr, "rvaccept: rvs event channel is NULL");
    }
    // Poll for connection request
    if (rdma_get_cm_event(rvs->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }

    uint64_t rdmaStart = rdtsc(); // Start timing after get_cm_event since it is blocking

    // Check if event is a connection request
    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        fprintf(stderr, "Unexpected event: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    struct rdma_cm_id *client_cm_id = event->id;

    // Retrieve IP address and construct actual client virtual address
    struct sockaddr_in *client_addr = rdma_get_peer_addr(client_cm_id);
    uint32_t client_ip = ntohl(client_addr->sin_addr.s_addr);
    uint16_t client_port = getPort(rvs->vaddr);

    uint64_t vaddr = constructVaddr(0x0001, client_ip, client_port);
    
    // Define protection domain
    struct ibv_pd *pd = ibv_alloc_pd(client_cm_id->verbs);
    if (!pd) {
        perror("ibv_alloc_pd failed");
        ibv_dealloc_pd(pd);
        return -1;
    }

    // Create completion queue
    struct ibv_cq *cq = ibv_create_cq(client_cm_id->verbs, 256, client_cm_id, NULL, 0);
    if (!cq) {
        perror("ibv_create_cq failed");
        ibv_dealloc_pd(pd);
        return -1;
    }

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
    uint64_t rdmaEnd = rdtsc();
    double rdmaTime = (rdmaEnd - rdmaStart) / (cpu_ghz * 1e3);
    printf("Time to setup rdma resources in rvaccept: %.3f µs\n", rdmaTime);
    
    start = rdtsc();

    // Accept connection
    if (rdma_accept(client_cm_id, NULL)) {
        perror("rdma_accept");
        rdma_ack_cm_event(event);
        return -1;
    }

    // Drain the event channel for established event
    if (rdma_get_cm_event(rvs->ec, &event)) {
        perror("rdma_get_cm_event ESTABLISHED");
        return -1;
    }

    if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
        fprintf(stderr, "Expected ESTABLISHED event: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    rdma_ack_cm_event(event);

    // Create a new rvsocket for accepted connection
    new_rvs = calloc(1, sizeof(*new_rvs));
    if (!new_rvs) {
        perror("calloc");
        return -1;
    }

    new_rvs->vaddr = vaddr;
    new_rvs->type = SOCK_STREAM;
    
    // Create a mailbox for the new socket (server calls rvaccept for each client)
    RVMA_Status status = newMailboxIntoHashmap(window->hashMapPtr, new_rvs->vaddr);
    if (status != RVMA_SUCCESS) {
        perror("Failed to allocate new rvs");
        return -1;
    }

    // Construct new rvsocket
    new_rvs->mailboxPtr = searchHashmap(window->hashMapPtr, new_rvs->vaddr);
    new_rvs->cm_id = client_cm_id;
    new_rvs->ec = client_cm_id->channel;
    new_rvs->mailboxPtr->pd = pd;
    new_rvs->mailboxPtr->cq = cq;
    new_rvs->mailboxPtr->qp = client_cm_id->qp;

    new_rvs->index = next_fd++;
    new_rvs->state = rs_connected;

    end = rdtsc();

    // Prepost buffer pools
    if (postSendPool(new_rvs->mailboxPtr, MAX_POOL_BUFS, new_rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
        perror("postSendPool failed");
        return -1;
    }
    if (postRecvPool(new_rvs->mailboxPtr, MAX_POOL_BUFS, new_rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
        perror("postRecvPool failed");
        return -1;
    }

    // Insert new rvsocket into index map
    rs_insert(new_rvs, new_rvs->index);

    if (addr && addrlen)
        rgetpeername(new_rvs->index, addr, addrlen);

    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("rvaccept time: %.3f µs\n", elapsed_us);

    return new_rvs->index;
}


int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen, RVMA_Win *window) {
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
    if (rdma_resolve_addr(rvs->cm_id, NULL, (struct sockaddr *)addr, 2000)) {
        perror("rdma_resolve_addr");
        return -1;
    }
    // Wait for address resolved event
    if (rdma_get_cm_event(rvs->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        fprintf(stderr, "rdma_resolve_addr failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    // Resolve route
    if (rdma_resolve_route(rvs->cm_id, 2000)) {
        perror("rdma_resolve_route");
        return -1;
    }
    // Wait for route resolved event
    if (rdma_get_cm_event(rvs->ec, &event)) {
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

    RVMA_Status status = newMailboxIntoHashmap(window->hashMapPtr, rvs->vaddr);
    if (status != RVMA_SUCCESS) {
        perror("Failed to allocate new rvs");
        return -1;
    }

    rvs->mailboxPtr = searchHashmap(window->hashMapPtr, rvs->vaddr);
    if (!rvs->mailboxPtr) {
        perror("rvconnect: searchHashmap failed");
        return -1;
    }

    // Allocate PD
    struct ibv_pd *pd = ibv_alloc_pd(rvs->cm_id->verbs);
    if (!pd) {
        perror("ibv_alloc_pd failed");
        ibv_dealloc_pd(pd);
        return -1;
    }
    rvs->mailboxPtr->pd = pd;

    // Create CQ
    struct ibv_cq *cq = ibv_create_cq(rvs->cm_id->verbs, 256, rvs->cm_id, NULL, 0);
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

    if (rdma_create_qp(rvs->cm_id, pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }
    rvs->mailboxPtr->qp = rvs->cm_id->qp;

    uint64_t rdmaSetup = rdtsc();

    // Connect
    if (rdma_connect(rvs->cm_id, NULL)) {
        perror("rdma_connect");
        return -1;
    }
    // Wait for CM event
    if (rdma_get_cm_event(rvs->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }
    if(event->event != RDMA_CM_EVENT_ESTABLISHED) {
        fprintf(stderr, "rdma_connect failed: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }
    rdma_ack_cm_event(event);

    uint64_t beforePostRecv = rdtsc();
    // Prepost buffer pools
    if (postSendPool(rvs->mailboxPtr, MAX_POOL_BUFS, rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
            perror("postSendPool failed");
            return -1;
    }
    if (postRecvPool(rvs->mailboxPtr, MAX_POOL_BUFS, rvs->vaddr, EPOCH_OPS) != RVMA_SUCCESS) {
        perror("postRecvPool failed");
        return -1;
    }

    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    double rdmaTime = (rdmaSetup - addrRouteResolved) / (cpu_ghz * 1e3);
    double postRecvTime = (end - beforePostRecv) / (cpu_ghz * 1e3);
    elapsed_us -= (rdmaTime + postRecvTime);
    printf("postRecvPool time in rvconnect: %.3f µs\n", postRecvTime);
    printf("rvconnect time for address and route resolution: %.3f µs\n", addrRouteTime);
    printf("rvconnect time for RDMA resources setup: %.3f µs\n", rdmaTime);
    printf("rvconnect total time: %.3f µs\n", elapsed_us);

    return 0;
}


// Send for stream sockets
int rvsend(int socket, void *buf, int64_t len) {
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;
    if (rvs->type == SOCK_STREAM) {
        if (rvmaSend(buf, len, vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
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
// TODO: Address resolution, header for routing
int rvsendto(int socket, void *buf, int64_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen, RVMA_Win *windowPtr) {
    uint64_t elapsed = 0, frag_setup = 0, buffer_setup = 0, wr_setup = 0, total_poll = 0;
    union socket_addr src_addr;
	socklen_t src_len;
	struct ds_qp *qp;
	struct ds_dest **tdest, *new_dest;
	int ret = 0;

    struct rvsocket *rvs = idm_at(&idm, socket);
    uint64_t vaddr = rvs->vaddr;

    int hardware_counter = 0; // Counter to compare with threshold (Should only exist in hardware)

    // Determine number of fragments (bufferLen/MTU)
    int64_t threshold = (len + RS_MAX_TRANSFER - 1) / RS_MAX_TRANSFER;

    // Initialize notification pointers
    void **notifBuffPtr = malloc(sizeof(void *));
    int *notifLenPtr = malloc(sizeof(int));

    for (int offset = 0; offset < threshold; offset++) {
        // Define message fragment
        // i=0->RS_MAX_TRANSFER-1, RS_MAX_TRANSFER->2*RS_MAX_TRANSFER-1, ...
        size_t frag_len = (offset == threshold - 1) ? (len - offset * RS_MAX_TRANSFER) : RS_MAX_TRANSFER;
        void *frag_ptr = (uint8_t *)buf + offset * RS_MAX_TRANSFER;

        // Retrieve a buffer from the send pool
        RVMA_Buffer_Entry *entry = dequeue(rvs->mailboxPtr->sendBufferQueue);
        if (!entry) {
            perror("rvsendto: Failed to retrieve buffer from send pool");
            return -1;
        }

        struct ds_header *hdr = (struct dgram_frag_header *)entry->realBuff;
        hdr->frag_num = offset + 1;
        hdr->total_frags = threshold;

        memcpy((char *)entry->realBuff + sizeof(*hdr), frag_ptr, frag_len);
        size_t total_size = sizeof(*hdr) + frag_len;

        // if dest is null or different from last, create dest from addr and save in rvs->dest
        // ah creation, qpn, qkey extraction, etc.
        if (!rvs->dest || ds_compare_addr(dest_addr, &rvs->dest->addr)) {
            int ret = ds_get_src_addr(rvs, dest_addr, addrlen, &src_addr, &src_len);
            if (ret) {
                perror("ds_get_src_addr failed");
                return -1;
            }
            ds_create_qp(rvs, &src_addr, addrlen, qp, windowPtr);
            new_dest = calloc(1, sizeof(*new_dest));
            if (!new_dest) {
                perror("calloc new_dest failed");
                return -1;
            }
            memcpy(&new_dest->addr, dest_addr, addrlen);
            new_dest->qp = qp;
        }

        printf("Buffer posted with size %zu bytes for fragment %d/%d\n", total_size, hdr->frag_num, hdr->total_frags);
        // Build sge, wr
        struct ibv_sge sge = {
            .addr = (uintptr_t)entry->realBuff,
            .length = total_size,
            .lkey = entry->mr->lkey
        };

        struct ibv_send_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id              = (uintptr_t)entry;
        wr.sg_list            = &sge;
        wr.num_sge            = 1;
        wr.opcode             = IBV_WR_SEND;
        wr.send_flags         = IBV_SEND_SIGNALED;
        wr.wr.ud.ah           = rvs->dest->ah;
        wr.wr.ud.remote_qpn   = rvs->dest->qpn;
        wr.wr.ud.remote_qkey  = RDMA_UDP_QKEY;

        struct ibv_send_wr *bad_wr = NULL;

        if (ibv_post_send(rvs->mailboxPtr->qp, &wr, &bad_wr)) {
            perror("rvmasendto: ibv_post_send failed");
            return -1;
        }

        hardware_counter++; // Hardware counter is incremented after every operation (By # ops)
        if (hardware_counter == threshold) {
            // Write address of head of buffer to notification pointer
            *notifBuffPtr = entry->realBuff;
            // Write length of buffer to notifLenPtr in case buffer is reused
            *notifLenPtr = total_size;
        }
    }

    int completed = 0;
    while (completed < threshold) {
        struct ibv_wc wc;
        int n = ibv_poll_cq(rvs->cm_id->send_cq, 1, &wc);
        if (n < 0) {
            perror("rvsendto: ibv_poll_cq failed");
            return -1;
        }
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "CQ error\n");
            return -1;
        }

        if (wc.opcode == IBV_WC_SEND) {
            enqueue(rvs->mailboxPtr->sendBufferQueue, (RVMA_Buffer_Entry *)wc.wr_id);
            completed++;
        }
    }
    free(notifBuffPtr);
    free(notifLenPtr);
    return 0;
}


static void ds_set_src(struct sockaddr *addr, socklen_t *addrlen, void *vaddr) {
    union socket_addr sa;

    memset(&sa, 0, sizeof sa);
    if (*addrlen > sizeof(sa.sin))
        *addrlen = sizeof(sa.sin);

    sa.sin.sin_family = AF_INET;
    sa.sin.sin_port = 0;
    sa.sin.sin_addr.s_addr = 0;
    memcpy(addr, &sa, *addrlen);
}


// Receive buffer pool should already be preposted, manage fragments and poll completion
int rvrecvfrom(int socket, void *buf, size_t len,
               int flags, struct sockaddr *src_addr,
               socklen_t *addrlen)
{
    struct rvsocket *rvs = idm_at(&idm, socket);
    RVMA_Mailbox *mailbox = rvs->mailboxPtr;

    int total_frags = 0;
    int received_frags = 0;
    int expected_alloc = 0;
    int actual_total_bytes = 0;

    void *msg_buf = NULL;

    struct ibv_wc wc;
    printf("Im in rvrecvfrom! 1\n");
    while (1) {
        int n;
        do {
            n = ibv_poll_cq(rvs->cm_id->recv_cq, 1, &wc);
        } while (n == 0);
        if (n < 0 || wc.status != IBV_WC_SUCCESS) {
            perror("rvrecvfrom: CQ poll error");
            return -1;
        }
        if (wc.opcode != IBV_WC_RECV)
            continue;

        printf("Im in rvrecvfrom! 2\n");
        RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)wc.wr_id;

        char *recv_buf = (char *)entry->realBuff;
        int grh = 40;
        int data_len = wc.byte_len - grh;
        char *data = recv_buf + grh;

        if (data_len < (int)sizeof(struct ds_header))
            continue;

        struct ds_header *hdr = (struct ds_header *)data;
        char *payload = data + sizeof(struct ds_header);
        int payload_len = data_len - sizeof(struct ds_header);
        if (payload_len < 0)
            continue;

        printf("Im in rvrecvfrom! 3\n");
        ds_set_src(src_addr, addrlen, hdr);

        printf("Received fragment %d/%d (%d bytes)\n",
               hdr->frag_num,
               hdr->total_frags,
               payload_len);

        if (hdr->frag_num == 1) {
            total_frags = hdr->total_frags;
            expected_alloc = total_frags * RS_MAX_TRANSFER;
            msg_buf = malloc(expected_alloc);
            if (!msg_buf) {
                perror("malloc msg_buf failed");
                return -1;
            }

            received_frags = 0;
            actual_total_bytes = 0;
        }

        int offset = (hdr->frag_num - 1) * RS_MAX_TRANSFER;

        memcpy((char *)msg_buf + offset,
               payload,
               payload_len);

        received_frags++;
        actual_total_bytes += payload_len;

        // Repost receive buffer
        struct ibv_sge sge = {
            .addr = (uintptr_t)recv_buf,
            .length = MAX_RECV_SIZE,
            .lkey = entry->mr->lkey
        };

        struct ibv_recv_wr recv_wr = {
            .wr_id = (uintptr_t)entry,
            .sg_list = &sge,
            .num_sge = 1
        };
        struct ibv_recv_wr *bad_wr = NULL;

        printf("Before posted recv\n");
        if (ibv_post_recv(mailbox->qp, &recv_wr, &bad_wr)) {
            perror("rvrecvfrom: post_recv failed");
            free(msg_buf);
            return -1;
        }
        printf("After posted recv\n");

        // Check for completion
        if (received_frags == total_frags) {
            if ((size_t)actual_total_bytes > len)
                actual_total_bytes = len;
            memcpy(buf, msg_buf, actual_total_bytes);
            free(msg_buf);
            return actual_total_bytes;
        }
    }
}


int rvrecv(int socket, void *buf, size_t len, int flags) {
    // Read from mailbox buffer with rvmaRecv
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;

    RVMA_Mailbox *mailbox = rvs->mailboxPtr;

    if (rvs->type == SOCK_DGRAM) {
        fprintf(stderr, "rvrecv: Socket type is datagram, use rvrecvfrom instead\n");
        return -1;
    } else {
        if (rvmaRecv(vaddr, buf, len, 0, mailbox) != RVMA_SUCCESS) {
            fprintf(stderr, "rvrecv: rvmaRecv failed\n");
            return -1;
        }
    }
    return 0;
}