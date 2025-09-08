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
#define RS_MAX_TRANSFER 4096 /* 4 KB */
#define RS_SNDLOWAT 2048
#define RS_QP_MIN_SIZE 16
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4	/* must be power of 2 */
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2
#define MAX_RECV_SIZE 65536 /* 64 KB */

static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

#define rs_send_wr_id(data) ((uint64_t) data)

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

struct ds_dest {
	union socket_addr addr;	/* must be first */
	struct ds_qp	  *qp;
	struct ibv_ah	  *ah;
	uint32_t	   qpn;
};

struct ds_qp {
	dlist_entry	  list;
	struct rsocket	  *rs;
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
            struct ds_qp *qp_list; // Datagram QP list
            void *dest_map; // For mapping dest socket addr to QP
            struct ds_dest *conn_dest; // Destination for datagrams
            
            int udp_sock; // UDP socket for exchanging connection data
            int epfd; // epoll fd for waiting on completion
            int rqe_avail; // Number of receive queue entries available
            struct ds_smsg *smsg_free; // Pool of small send buffers

        };
    };
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
    union {
        struct rs_msg *rmsg; // Receive message (for stream)
        struct ds_rmsg *dmsg; // Datagram message (for datagram)
    };
    struct rs_iomap_mr *remote_iomappings;
    int iomap_pending; // Flag indicating if there are pending I/O mappings
    
    RVMA_Mailbox *mailboxPtr;
    uint64_t vaddr;
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

static int ds_init(struct rvsocket *rvs, int domain)
{
	rvs->udp_sock = socket(domain, SOCK_DGRAM, 0);
	if (rvs->udp_sock < 0)
		return rvs->udp_sock;

	rvs->epfd = epoll_create(2);
	if (rvs->epfd < 0)
		return rvs->epfd;

	return 0;
}


static void ds_free(struct rvsocket *rvs) {
    // TODO
}


static void rs_free(struct rvsocket *rvs) {
    if (rvs->type == SOCK_DGRAM) {
        ds_free(rvs);
        return;
    }
    if (rvs->rmsg)
        free(rvs->rmsg);
    if (rvs->index >= 0) {
        rs_remove(rvs);
    }
    if (rvs->cm_id) {
        rdma_destroy_id(rvs->cm_id);
    }
    free(rvs);
}

// Magic function
static int rvs_get_ctx(struct rvsocket *rvs) {
    if (!rvs->mailboxPtr) {
        rvs->mailboxPtr = calloc(1, sizeof(RVMA_Mailbox));
        if (!rvs->mailboxPtr) return -1;
    }

    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list) return -1;

    for (int i = 0; dev_list[i]; i++) {
        struct ibv_context *ctx = ibv_open_device(dev_list[i]);
        if (!ctx) continue;

        struct ibv_device_attr dev_attr;
        if (ibv_query_device(ctx, &dev_attr)) {
            ibv_close_device(ctx);
            continue;
        }

        for (int port = 1; port <= dev_attr.phys_port_cnt; port++) {
            struct ibv_port_attr port_attr;
            if (ibv_query_port(ctx, port, &port_attr)) continue;
            if (port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND) continue;

            if ((port_attr.phys_state == IBV_PORT_ACTIVE || port_attr.phys_state == 5) &&
                port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND) {

                rvs->mailboxPtr->ctx = ctx;
                rvs->mailboxPtr->port_num = port;
                break;
            }
        }
    }

    ibv_free_device_list(dev_list);
    return 0;
}


uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
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

    if (type == SOCK_STREAM) {
        // Use the mailbox's CM ID (already set with RDMA_PS_TCP)
        index = rvs->mailboxPtr->cm_id->channel->fd;
    } else { // datagram
        // Create UDP socket
        ret = ds_init(rvs, SOCK_DGRAM);
        if (ret) {
            rs_free(rvs);
            return ret;
        }
        index = rvs->udp_sock;
    }
    // Insert rvsocket into index map
    ret = rs_insert(rvs, index);
    if (ret < 0) {
        rs_free(rvs);
        return ret;
    }
    // return rvsocket index (vaddr)
    return rvs->index;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
	int ret;

	rvs = idm_lookup(&idm, socket);
	if (!rvs) {
		printf("rbind: rvs is NULL\n");
        return -1;
    }
    if (rvs->type == SOCK_STREAM) {
	    ret = rdma_bind_addr(rvs->mailboxPtr->cm_id, (struct sockaddr *)addr);
        if (!ret)
            rvs->state = rs_bound;
    } else { // Datagram
        void *big_buf = malloc(MAX_RECV_SIZE);

        // Set notification pointer to buffer address
        uintptr_t *notifBuffPtr = malloc(sizeof(uintptr_t));
        *notifBuffPtr = 0;

        int *notifLenPtr = malloc(sizeof(int));
        *notifLenPtr = 0;

        // Define threshold as max number of transfers that fit in the buffer
        int threshold = MAX_RECV_SIZE / RS_MAX_TRANSFER;

        ret = rvs_get_ctx(rvs);

        struct ibv_pd *pd = ibv_alloc_pd(rvs->mailboxPtr->ctx);
        if (!pd) {
            perror("ibv_alloc_pd failed");
            return -1;
        }
        rvs->mailboxPtr->pd = pd;

        struct ibv_mr *mr = ibv_reg_mr(pd, big_buf, MAX_RECV_SIZE, IBV_ACCESS_LOCAL_WRITE);
        if (!mr) {
            perror("ibv_reg_mr failed");
            ibv_dealloc_pd(pd);
            return -1;
        }

        RVMA_Status status = rvmaPostBuffer((void**)&big_buf, MAX_RECV_SIZE, (void **)&notifBuffPtr,
                                        (void **)&notifLenPtr, rvs->vaddr, rvs->mailboxPtr, threshold, EPOCH_OPS);

        ret = bind(rvs->udp_sock, addr, addrlen);

        // Create completion queue
        struct ibv_cq *cq = ibv_create_cq(rvs->mailboxPtr->ctx, 128, NULL, NULL, 0);
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
            .qp_type = IBV_QPT_UD,
            .cap = {
                .max_send_wr = 16,
                .max_recv_wr = 16,
                .max_send_sge = 1,
                .max_recv_sge = 1
            }
        };

        struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);
        if (!qp) {
            perror("ibv_create_qp");
            ibv_destroy_cq(cq);
            ibv_dealloc_pd(pd);
            return -1;
        }
        rvs->mailboxPtr->qp = qp;

        // MUST SET QP TO INIT
        struct ibv_qp_attr attr = {0};
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = rvs->mailboxPtr->port_num;
        attr.qkey = 0x11111111;

        if (ibv_modify_qp(qp, &attr,
                        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY)) {
            perror("ibv_modify_qp to INIT failed");
            return -1;
        }
        
        // Prepost initial receive buffers
        // TODO: Handle logic for when big buffer is full; create new big buffer and post it
        // Use threshold to determine when to create new buffer
        for (int i = 0; i < 4; i++) {
            uint32_t offset = i * RS_MAX_TRANSFER;
            RVMA_Buffer_Entry *entry = malloc(sizeof(RVMA_Buffer_Entry));
            if (!entry) {
                perror("malloc failed");
                return -1;
            }
            entry->realBuff = big_buf;
            entry->realBuffAddr = (uint8_t *)big_buf + offset;
            entry->realBuffSize = RS_MAX_TRANSFER;
            entry->mr = mr;
            entry->epochCount = i;

            // Build sge and wr
            struct ibv_sge sge = {
                .addr = (uintptr_t)entry->realBuffAddr,
                .length = entry->realBuffSize,
                .lkey = entry->mr->lkey };
            
            struct ibv_recv_wr wr = {
                .wr_id = (uintptr_t)entry,
                .sg_list = &sge,
                .num_sge = 1,
                .next = NULL
            };
            struct ibv_recv_wr *bad_wr = NULL;

            printf("Posting buffer %d\n", i);
            if (ibv_post_recv(rvs->mailboxPtr->qp, &wr, &bad_wr)) {
                perror("ibv_post_recv failed");
                free(entry);
                return -1;
            }
            printf("Enqueuing buffer\n");
            // Enqueue new buffer
            if (enqueue(rvs->mailboxPtr->bufferQueue, entry) != RVMA_SUCCESS) {
                fprintf(stderr, "enqueue failed\n");
            }
        }
    }
	return 0;
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
    struct ibv_cq *cq = ibv_create_cq(client_cm_id->verbs, 16, NULL, NULL, 0);
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


int rvsend(int socket, void *buf, int64_t len) {
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;

    if (rvs->type == SOCK_DGRAM) {
        if (rvmaSendto(buf, len, &vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaSendto failed\n");
            return -1;
        }
    } else {
        if (rvmaSend(buf, len, &vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaSend failed\n");
            return -1;
        }
    }
    return 0;
}


int rvrecv(int socket, RVMA_Win *window) {
    // Read from mailbox buffer with rvmaRecv
    struct rvsocket *rvs;
    uint64_t vaddr;

    rvs = idm_at(&idm, socket);
    vaddr = rvs->vaddr;

    if (rvs->type == SOCK_DGRAM) {
        if (rvmaRecvfrom(&vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaRecvfrom failed\n");
            return -1;
        }
    } else {
        if (rvmaRecv(&vaddr, rvs->mailboxPtr) != RVMA_SUCCESS) {
            fprintf(stderr, "rvmaRecv failed\n");
            return -1;
        }
    }
    return 0;
}