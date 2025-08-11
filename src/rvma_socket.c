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
#include "cma.h"
#include "indexer.h"
#include "perftest_resources.h"
#include "rvma_socket.h"

#define RS_OLAP_START_SIZE 2048
#define RS_MAX_TRANSFER 65536
#define RS_SNDLOWAT 2048
#define RS_QP_MIN_SIZE 16
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4	/* must be power of 2 */
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2

#define MAX_RVSOCKETS 1024
#define PORT 7471

static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
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

struct rs_iomap_mr {
	uint64_t offset;
	struct ibv_mr *mr;
	dlist_entry entry;
	_Atomic(int) refcnt;
	int index;	/* -1 if mapping is local and not in iomap_list */
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

struct ds_qp;

struct ds_dest {
	union socket_addr addr;	/* must be first */
	struct ds_qp	  *qp;
	struct ibv_ah	  *ah;
	uint32_t	   qpn;
};

struct ds_qp {
	dlist_entry	  list;
	struct rvsocket	  *rs;
	struct rdma_cm_id *cm_id;
	struct ds_header  hdr;
	struct ds_dest	  dest;

	struct ibv_mr	  *smr;
	struct ibv_mr	  *rmr;
	uint8_t		  *rbuf;

	int		  cq_armed;
};

struct ds_rmsg {
	struct ds_qp	*qp;
	uint32_t	offset;
	uint32_t	length;
};

struct ds_smsg {
	struct ds_smsg	*next;
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
            int remote_sge; // Remote SGE for data transfer
            struct rs_sge remote_sgl; // Remote SGE list (sgl sge contained in wr)
            struct rs_sge remote_iomap; // Remote I/O map (virtual mailbox address?)

            struct ibv_mr *target_mr; // Target memory region for data transfer
            int target_sge; // Target SGE index
            int target_iomap_size; // Size of the local I/O map
            void *target_buffer_list; // List of target buffers (mailbox buffers?)
            volatile struct rs_sge *target_sgl; // Target SGE list
            struct rs_iomap *target_iomap; // Target I/O map (for data retrieval)
            
            int rbuf_msg_index; // Receive buffer index, local mailbox?
            int rbuf_bytes_avail; // Bytes available in the receive buffer
            struct ibv_mr *rmr; // Receive memory region
            uint8_t *rbuf; // Receive buffer (mailbox buffer?)

            int sbuf_bytes_avail; // Bytes available in the send buffer
            struct ibv_mr *smr; // Send memory region
            struct ibv_sge ssgl[2]; // Send SGE list
        };
        struct { // datagram
            struct ds_qp *qp_list; // Datagram QP list
            void *dest_map; // For mapping dest socket addr to QP
            struct ds_dest *conn_dest; // Destination for rconnect()ed datagrams
            
            int udp_sock; // UDP socket for exchanging connection data
            int epfd; // epoll fd for waiting on completion
            int rqe_avail; // Number of receive queue entries available
            struct ds_smsg *smsg_free; // Pool of small send buffers

        };
    };
    int state;
    int cq_armed;
    int err;
    int sqe_avail;
    uint32_t sbuf_size;
    uint32_t sq_size;
    uint32_t rbuf_size;
    uint32_t rq_size;
    int rmsg_head; // Head of the receive message queue
    int rmsg_tail; // Tail of the receive message queue
    union {
        struct rs_msg *rmsg; // Receive message (for stream)
        struct ds_rmsg *dmsg; // Datagram message (for datagram)
    };
    uint8_t *sbuf; // Send buffer
    struct rs_iomap_mr *remote_iomappings; // Remote I/O mappings
    int iomap_pending; // Flag indicating if there are pending I/O mappings
    
    RVMA_Mailbox *mailboxPtr;
    uint64_t vaddr;
    void **rvma_notifBuffPtrAddr;
    void **rvma_notifLenPtrAddr;
};


/* 
RVSOCKETS IS JUST A SOCKET-LIKE API FOR RVMA
COMPONENT TRANSLATION IS OUTLINED AS FOLLOWS:
    - rvsocket: Initializes RVMA mailbox/window and sets up connection with cm_id
    - rvbind: Calls rdma_bind_addr to bind address and cm_id
    - rvlisten: Calls rdma_listen to listen for incoming connection requests
    - rvaccept: Calls rdma_accept to accept connection request
    - rvconnect: Calls rdma_connect to connect to a specified address
    - rvsend/rvrecv: Calls ibv_post_send/recv
*/

uint64_t constructVaddr(uint16_t reserved, uint32_t ip_host_order, uint16_t port) {
    uint64_t res = (uint64_t)reserved << 48 | ((uint64_t)ip_host_order << 16) | port;
    return res;
}


// Create rvsocket using rdma_create_id (called in rvmaInitWindowMailbox)
// Return socketfd
uint64_t rvsocket(int type, uint32_t ip_host_order, RVMA_Win *window) {
	struct rvsocket *rvs;
    int index, ret;
    uint16_t reserved = 0x0001;

    rs_configure();

    rvs = calloc(1, sizeof(*rvs));
    if (!rvs)
        return ERR(ENOMEM);

    // Set rvsocket vaddr and mailbox
    rvs->vaddr = constructVaddr(reserved, ip_host_order, PORT);
    rvs->mailboxPtr = searchHashmap(window->hashMapPtr, &rvs->vaddr);

    if (type == SOCK_STREAM) {
        index = rvs->cm_id->channel->fd;

    } else { // datagram
        // TODO
    }
    
    // Insert rvsocket into index map
    ret = rs_insert(rvs, index);

    // return rvsocket index (vaddr)
    return rvs->index;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
	int ret;

	rvs = idm_lookup(&idm, socket);
	if (!rvs) {
		printf("rbind: rvs is NULL\n");
        return ERR(EBADF);
    }
    
    // Only implementing stream socket for now
	ret = rdma_bind_addr(rvs->mailboxPtr->cm_id, (struct sockaddr *)addr);

	return ret;
}


int rvlisten(int socket, int backlog) {
    struct rvsocket *rvs;
    struct rdma_cm_event *event;
    int ret;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rlisten: rvs is NULL\n");
        return ERR(EBADF);
    }

    if (rvs->state == rs_listening) {
        printf("rlisten: rvs is already listening\n");
        return 0;
    }

    ret = rdma_listen(rvs->mailboxPtr->cm_id, backlog);
    if (ret)
        return ret;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, rvs->accept_queue);
    if (ret)
        return ret;

    rvs->state = rs_listening;
    return 0;
}


int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen) {
    struct rvsocket *rvs, *new_rvs;
    int ret;
    struct rdma_cm_event *event;
    struct rdma_cm_id *client_cm_id = event->id;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rvaccept: rvs is NULL\n");
        return ERR(EBADF);
    }

    if (rvs->state != rs_listening) {
        printf("rvaccept: rs is not in listening state\n");
        return ERR(EBADF);
    }
    
    if (rdma_get_cm_event(rvs->mailboxPtr->ec, &event)) {
        perror("rdma_get_cm_event");
        return -1;
    }

    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        fprintf(stderr, "Unexpected event: %s\n", rdma_event_str(event->event));
        rdma_ack_cm_event(event);
        return -1;
    }

    // Define protection domain
    struct ibv_pd *pd = ibv_alloc_pd(client_cm_id->verbs);
    rvs->mailboxPtr->pd = pd;
    if (!pd) {
        perror("ibv_alloc_pd failed");
        return -1;
    }

    // Create completion queue
    struct ibv_cq *cq = ibv_create_cq(client_cm_id->verbs, 16, NULL, NULL, 0);
    rvs->mailboxPtr->cq = cq;
    if (!cq) {
        perror("ibv_create_cq failed");
        return -1;
    }

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

    if (rdma_accept(client_cm_id, NULL)) {
        perror("rdma_accept");
        rdma_ack_cm_event(event);
        return -1;
    }

    rdma_ack_cm_event(event);

    new_rvs = calloc(1, sizeof(*new_rvs));
    if (!new_rvs) {
        perror("calloc");
        return -1;
    }

    // Fill in new rvsocket fields
    new_rvs->vaddr = rvs->vaddr; // Same vaddr as listening socket
    new_rvs->mailboxPtr = rvs->mailboxPtr; // Same mailbox
    new_rvs->mailboxPtr->cm_id = client_cm_id;
    new_rvs->mailboxPtr->pd = pd;
    new_rvs->mailboxPtr->cq = cq;
    new_rvs->mailboxPtr->qp = client_cm_id->qp;
    new_rvs->index = client_cm_id->channel->fd;

    idm_insert(&idm, new_rvs, new_rvs->index);

    if (addr && addrlen)
        rgetpeername(new_rvs->index, addr, addrlen);

    return new_rvs->index;
}


int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rvs;
    int ret;

    rvs = idm_lookup(&idm, socket);
    if (!rvs) {
        printf("rvconnect: rvs is NULL\n");
        return ERR(EBADF);
    }

    // Resolve address
    if (rdma_resolve_addr(rvs->mailboxPtr->cm_id, NULL, (struct sockaddr *)addr, 2000)) {
        perror("rdma_resolve_addr");
        return -1;
    }

    memcpy(&rvs->mailboxPtr->cm_id->route.addr.dst_addr, addr, addrlen);
    ret = rs_do_connect(rvs);
    return ret;
}



ssize_t rvsend(int socket, const void *buf, size_t len) {

}


ssize_t rvrecv(int socket, void *buf, size_t len, int flags) {
    // Read from mailbox buffer with rvmaRecv
}