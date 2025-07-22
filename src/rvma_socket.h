#ifndef RVM_SOCKET_H
#define RVM_SOCKET_H

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <rdma/rdma_cma.h>
#include <rdma/rsocket.h>
#include <infiniband/verbs.h>

// ---- Constants ----
#define RS_OLAP_START_SIZE 2048
#define RS_MAX_TRANSFER    65536
#define RS_SNDLOWAT        2048
#define RS_QP_MIN_SIZE     16
#define RS_QP_MAX_SIZE     0xFFFE
#define RS_QP_CTRL_SIZE    4
#define RS_CONN_RETRIES    6
#define RS_SGL_SIZE        2

// ---- Structs ----
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
    _Atomic(int) refcnt;
    int index;
};

union socket_addr {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

struct ds_header {
    uint8_t version;
    uint8_t length;
    __be16 port;
    union {
        __be32 ipv4;
        struct {
            __be32 flowinfo;
            uint8_t addr[16];
        } ipv6;
    } addr;
};

struct ds_qp;
struct ds_dest {
    union socket_addr addr;
    struct ds_qp *qp;
    struct ibv_ah *ah;
    uint32_t qpn;
};

struct ds_qp {
    struct rdma_cm_id *cm_id;
    struct ds_header hdr;
    struct ds_dest dest;
    struct ibv_mr *smr;
    struct ibv_mr *rmr;
    uint8_t *rbuf;
    int cq_armed;
};

struct ds_rmsg {
    struct ds_qp *qp;
    uint32_t offset;
    uint32_t length;
};

struct ds_smsg {
    struct ds_smsg *next;
};

enum rs_state {
    rs_init              = 0,
    rs_bound             = 0x0001,
    rs_listening         = 0x0002,
    rs_opening           = 0x0004,
    rs_resolving_addr    = rs_opening | 0x0010,
    rs_resolving_route   = rs_opening | 0x0020,
    rs_connecting        = rs_opening | 0x0040,
    rs_accepting         = rs_opening | 0x0080,
    rs_connected         = 0x0100,
    rs_writable          = 0x0200,
    rs_readable          = 0x0400,
    rs_connect_rdwr      = rs_connected | rs_readable | rs_writable,
    rs_connect_error     = 0x0800,
    rs_disconnected      = 0x1000,
    rs_error             = 0x2000,
};

struct rvsocket {
    int type;
    int index;
    union {
        struct { // Stream
            struct rdma_cm_id *cm_id;
            int accept_queue[2];
            int remote_sge;
            struct rs_sge remote_sgl;
            struct rs_sge remote_iomap;
            struct ibv_mr *target_mr;
            int target_sge;
            int target_iomap_size;
            void *target_buffer_list;
            volatile struct rs_sge *target_sgl;
            struct rs_iomap *target_iomap;
            int rbuf_msg_index;
            int rbuf_bytes_avail;
            struct ibv_mr *rmr;
            uint8_t *rbuf;
            int sbuf_bytes_avail;
            struct ibv_mr *smr;
            struct ibv_sge ssgl[2];
        };
        struct { // Datagram
            struct ds_qp *qp_list;
            void *dest_map;
            struct ds_dest *conn_dest;
            int udp_sock;
            int epfd;
            int rqe_avail;
            struct ds_smsg *smsg_free;
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
    int rmsg_head;
    int rmsg_tail;
    union {
        struct rs_msg *rmsg;
        struct ds_rmsg *dmsg;
    };
    uint8_t *sbuf;
    struct rs_iomap_mr *remote_iomappings;
    int iomap_pending;
    RVMA_Win                    *rvma_window;
    int                         *rvma_vaddr;
    void                        **rvma_notifBuffPtrAddr;
    void                        **rvma_notifLenPtrAddr;
};

// ---- Function Prototypes ----
#ifdef __cplusplus
extern "C" {
#endif

int rvsocket(int domain, int type, int protocol, RVMA_Mailbox *mailboxPtr);
int rvbind(int socket, const struct sockaddr *addr, socklen_t addrlen);
int rvlisten(int socket, int backlog);
int rvaccept(int socket, struct sockaddr *addr, socklen_t *addrlen);
int rvconnect(int socket, const struct sockaddr *addr, socklen_t addrlen);
ssize_t rvsend(int socket, const void *buf, size_t len, int flags);
ssize_t rvrecv(int socket, void *buf, size_t len, int flags);
int rvmaPutShim(int socket, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // RVM_SOCKET_H
