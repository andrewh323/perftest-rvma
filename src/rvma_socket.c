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
    int index; // Index of the socket in the index map
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
    
    RVMA_Win *rvma_window;
    int *rvma_vaddr;
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


// Create rvsocket using rdma_create_id, add to window and return the vaddr
// Call mailbox/window inits
int rvsocket(int domain, int type, int protocol, RVMA_Mailbox *mailboxPtr) {
	struct rvsocket *rs;
    int index, ret;

    rs_configure();
    rs = rs_alloc(NULL, type);

    if (type == SOCK_STREAM) {
        ret = rdma_create_id(NULL, &rs->cm_id, rs, RDMA_PS_TCP);
        if (ret)
            goto err;

        rs->cm_id->route.addr.src_addr.sa_family = domain;
        index = rs->cm_id->channel->fd;

    } else { // datagram
        ret = ds_init(rs, domain);
        if (ret)
            goto err;
        index = rs->udp_sock;
    }

    ret = rvs_insert_into_mailbox(rs, mailboxPtr);
    if (ret < 0)
        goto err;

    return rs->index;

err:
    rs_free(rs);
    return ret;
}


int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rs;
	int ret;

	rs = idm_lookup(&idm, socket);
	if (!rs) {
		printf("rbind: rs is NULL\n");
        return ERR(EBADF);
    }
    
    // Only implementing stream socket for now
	ret = rdma_bind_addr(rs->cm_id, (struct sockaddr *) addr);
    if (!ret)
        rs->state = rs_bound;
	return ret;
}


int rvlisten(int socket, int backlog) {
    struct rvsocket *rs;
    int ret;

    rs = idm_lookup(&idm, socket);
    if (!rs) {
        printf("rlisten: rs is NULL\n");
        return ERR(EBADF);
    }

    if (rs->state == rs_listening) {
        printf("rlisten: rs is already listening\n");
        return 0;
    }
    ret = rdma_listen(rs->cm_id, backlog);
    if (ret)
        return ret;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, rs->accept_queue);
    if (ret)
        return ret;

    rs->state = rs_listening;
    return 0;
}


int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen) {
    struct rvsocket *rs, *new_rs;
    int ret;
    if (!addr)
        printf("rvaccept: addr is NULL\n");
    if (!addrlen)
        printf("rvaccept: addrlen is NULL\n");

    rs = idm_lookup(&idm, socket);
    if (!rs) {
        printf("rvaccept: rs is NULL\n");
        return ERR(EBADF);
    }

    if (rs->state != rs_listening) {
        printf("rvaccept: rs is not in listening state\n");
        return ERR(EBADF);
    }

    ret = read(rs->accept_queue[0], &new_rs, sizeof(new_rs));
    rgetpeername(new_rs->index, addr, addrlen);

    return new_rs->index;
}


int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen) {
    struct rvsocket *rs;
    int ret;

    rs = idm_lookup(&idm, socket);
    if (!rs) {
        printf("rvconnect: rs is NULL\n");
        return ERR(EBADF);
    }
    
    memcpy(&rs->cm_id->route.addr.dst_addr, addr, addrlen);
    ret = rs_do_connect(rs);
    return ret;
}



ssize_t rvsend(int socket, const void *buf, size_t len) {

}


ssize_t rvrecv(int socket, void *buf, size_t len, int flags) {
    // Read from mailbox buffer with rvmaRecv
}