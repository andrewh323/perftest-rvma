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
#include <byteswap.h>
#include <util/compiler.h>
#include <util/util.h>
#include <ccan/container_of.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <rdma/rsocket.h>
#include "perftest_resources.h"
#include "rvma_socket.h"
#include "indexer.h"

#define MAX_RVSOCKETS 1024
#define PORT 7471
#define RS_OLAP_START_SIZE 2048
#define RS_MAX_TRANSFER 4056 /* 4 KB */
#define RS_SNDLOWAT 2048
#define RS_QP_MIN_SIZE 16
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4	/* must be power of 2 */
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2
#define MAX_RECV_SIZE 16*1024 /* 16 KB */

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

struct rs_sge {
	uint64_t addr;
	uint32_t key;
	uint32_t length;
};

struct rs_msg {
	uint32_t op;
	uint32_t data;
};

struct rs_iomap {
	uint64_t offset;
	struct rs_sge sge;
};

union socket_addr {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};

struct ds_header {
	uint8_t		  version;
	uint8_t		  length;
	__be16		  port;
	union {
		__be32  ipv4;
		struct {
			__be32 flowinfo;
			uint8_t  addr[16];
		} ipv6;
	} addr;
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
    // fastlocks
    union {
        struct { // data stream
            struct rdma_cm_id *cm_id; // RDMA CM ID
            int accept_queue[2]; // Accept queue for incoming connections
            /* 
            sseq, rseq attributes for control messages
            - generating ACKs and ensuring in-order delivery */
            
            struct ibv_sge ssgl[2]; // Send SGE list
        };
        struct { // datagram
            int udp_sock; // UDP socket for exchanging connection data
            int epfd; // epoll fd for waiting on completion
            int rqe_avail; // Number of receive queue entries available
            struct ds_smsg *smsg_free; // Pool of small send buffers

        };
    };
    int qp_port;
    int state;
    int err;
    int sqe_avail;
    uint32_t sbuf_size;
    uint32_t sq_size;
    uint16_t sq_inline;
    uint32_t rbuf_size;
    uint32_t rq_size;
    int rmsg_head; // Head of the receive message queue
    int rmsg_tail; // Tail of the receive message queue
    uint8_t *sbuf;
    struct rs_iomap_mr *remote_iomappings;
    int iomap_pending; // Flag indicating if there are pending I/O mappings
    
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
    // TODO
    return;
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


uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}

uint16_t extract_port(uint64_t vaddr) {
    return (uint16_t)(vaddr & 0xFFFF);
}

uint32_t extract_ip(uint64_t vaddr) {
    return (uint32_t)((vaddr >> 16) & 0xFFFFFFFF);
}


// Create rvsocket using rdma_create_id (called in rvmaInitWindowMailbox)
// Return socketfd
uint64_t rvsocket(int type, uint64_t vaddr, RVMA_Win *window) {
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

    if (type == SOCK_STREAM) {
        // For stream sockets, pd, cq, and qp are allocated in accept/connect
        // Use the mailbox's CM ID (should already be set when mailbox is created)
        index = rvs->mailboxPtr->cm_id->channel->fd;
        rvs->index = index;
    } else { // datagram
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
        printf("QP created: %p, type=%d, pd=%p\n", qp, qp->qp_type, qp->pd);
        ibv_query_port(ctx, 1, &port_attr);
        printf("Port state=%d, lid=0x%x\n",
            port_attr.state, port_attr.lid);

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

        // Create socket index for insertion
        rvs->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        rvs->index = rvs->udp_sock;
        index = rvs->index;
    }
    // Insert rvsocket into index map
    ret = rs_insert(rvs, index);
    if (ret < 0) {
        printf("Failed to insert rvsocket at index %d\n", index);
        rs_free(rvs);
        return ret;
    }
    // return rvsocket index
    return rvs->index;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
	int ret = 0;

	rvs = idm_lookup(&idm, socket);
	if (!rvs) {
		printf("rvbind: rvs is NULL\n");
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
    return ret;
}


int rvlisten(int socket, int backlog) {
    struct rvsocket *rvs;
    int ret;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rlisten: rvs is NULL\n");
        return -1;
    }

    if (rvs->state == rs_listening) {
        printf("rlisten: rvs is already listening\n");
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
    struct rvsocket *rvs, *new_rvs;
    struct rdma_cm_event *event;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rvaccept: rvs is NULL\n");
        return -1;
    }

    if (rvs->state != rs_listening) {
        printf("rvaccept: rs is not in listening state\n");
        return -1;
    }
    
    // Poll for connection request
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }

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

    // Accept connection
    if (rdma_accept(client_cm_id, NULL)) {
        perror("rdma_accept");
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

    // Fill in new rvsocket fields
    new_rvs->vaddr = rvs->vaddr; // Same vaddr as listening socket
    new_rvs->mailboxPtr = rvs->mailboxPtr; // Same mailbox - Should be new mailbox with new vaddr
    new_rvs->mailboxPtr->cm_id = client_cm_id;
    new_rvs->mailboxPtr->pd = pd;
    new_rvs->mailboxPtr->cq = cq;
    new_rvs->mailboxPtr->qp = client_cm_id->qp;
    new_rvs->index = client_cm_id->channel->fd;

    // Insert new rvsocket into index map
    rs_insert(new_rvs, new_rvs->index);

    if (addr && addrlen)
        rgetpeername(new_rvs->index, addr, addrlen);

    return new_rvs->index;
}


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

    printf("Server local_info.lid = 0x%x\n", local_info.lid);
    
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

    printf("Client remote_info.lid = 0x%x\n", remote_info.lid);
    
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
    printf("Created AH: %p, dlid: 0x%x, port_num: %d\n", ah, ah_attr.dlid, ah_attr.port_num);

    rvs->dest->ah = ah;
    rvs->dest->qpn = remote_info.qpn;
    rvs->dest->qkey = remote_info.qkey;

    close(tcp_fd);

    rvs->state = rs_connected;
    return rvs->index;
}


int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
    struct rdma_cm_event *event;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rvconnect: rvs is NULL\n");
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
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    if (rdma_create_qp(rvs->mailboxPtr->cm_id, pd, &qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }
    rvs->mailboxPtr->qp = rvs->mailboxPtr->cm_id->qp;

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

    return 0;
}


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

    printf("Created AH: %p, dlid: 0x%x, port_num: %d\n", ah, ah_attr.dlid, ah_attr.port_num);

    rvs->dest->ah = ah;
    rvs->dest->qpn  = remote_info.qpn;
    rvs->dest->qkey = remote_info.qkey;

    close(tcp_fd);

    rvs->state = rs_connected;
    return 0;
}


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
        fprintf(stderr, "rvsend: Socket type is not datagram\n");
    }
    return 0;
}

int rvsendto(int socket, void **buf, int64_t len) {
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;
    struct rv_dest *dest = rvs->dest;

    int *notifBuffPtr = malloc(sizeof(int));
    *notifBuffPtr = 0;

    int *notifLenPtr = malloc(sizeof(int));
    *notifLenPtr = 0;

    int64_t threshold = MAX_RECV_SIZE/RS_MAX_TRANSFER;

    RVMA_Status status = rvmaPostBuffer(buf, len, (void *)notifBuffPtr, (void *)notifLenPtr, vaddr,
                                        rvs->mailboxPtr, threshold, EPOCH_OPS);
    if(status != RVMA_SUCCESS) {
        perror("rvsendto: rvmaPostBuffer failed");
        return -1;
    }
    RVMA_Buffer_Entry *entry = dequeue(rvs->mailboxPtr->bufferQueue);
    if (!entry) {
        fprintf(stderr, "rvsendto: dequeue failed\n");
        return -1;
    }

    printf("Send: MR pd=%p, QP pd=%p, addr=%p, len=%ld\n",
       entry->mr->pd,
       rvs->mailboxPtr->qp->pd,
       entry->realBuff,
       len);

    if (!dest || dest->vaddr != vaddr) {
        if (!dest) {
            perror("rvsendto: dest not initialized");
            return -1;
        }
        dest->vaddr = vaddr;
    }

    entry->realBuff = *buf;
    entry->mr = ibv_reg_mr(rvs->mailboxPtr->pd, entry->realBuff, len,
                        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

    // Build sge, wr
    struct ibv_sge sge = {
        .addr = (uintptr_t)entry->realBuff,
        .length = len,
        .lkey = entry->mr->lkey
    };

    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));

    wr.wr_id              = 1;
    wr.sg_list            = &sge;
    wr.num_sge            = 1;
    wr.opcode             = IBV_WR_SEND;
    wr.send_flags         = IBV_SEND_SIGNALED;
    wr.wr.ud.ah           = dest->ah;
    wr.wr.ud.remote_qpn   = dest->qpn;
    wr.wr.ud.remote_qkey  = dest->qkey;

    struct ibv_send_wr *bad_wr = NULL;

    printf("Send buffer addr=%p, lkey=0x%x, qp_pd=%p, mr_pd=%p\n",
    entry->realBuff, entry->mr->lkey,
    rvs->mailboxPtr->qp->pd,
    entry->mr->pd);

    int rc = ibv_post_send(rvs->mailboxPtr->qp, &wr, &bad_wr);
    if (rc) {
        fprintf(stderr, "ibv_post_send failed: %s\n", strerror(errno));
    }
    // Poll cq
    struct ibv_wc wc;
    int ne;
    int max_retry = 1000;

    do {
        ne = ibv_poll_cq(rvs->mailboxPtr->cq, 1, &wc);
        if (ne < 0) {
            perror("ibv_poll_cq");
            return -1;
        }
        if (ne > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Completion error: %s (%d)\n",
                        ibv_wc_status_str(wc.status), wc.status);
                return -1;
            }
            printf("Send completion success!\n");
            break;
        }
    } while (--max_retry > 0);

    if (max_retry == 0) {
        fprintf(stderr, "Timeout waiting for send completion\n");
    }
    return status;
}


int rvrecv(int socket, RVMA_Win *windowPtr) {
    // Read from mailbox buffer with rvmaRecv
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;

    RVMA_Mailbox *mailbox = rvs->mailboxPtr;

    if (rvs->type == SOCK_DGRAM) {
        if (rvrecvfrom(&vaddr, mailbox) != RVMA_SUCCESS) {
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