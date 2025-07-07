#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>


struct rvs_context {
	struct rdma_cm_id *cm_id;
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    void *buf;     // buffer for RDMA operations
    size_t size;

    uint32_t rkey; // remote key for memory region
    uint64_t addr; // virtual address
    int is_server;

    struct ibv_sge sge;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
};

#define BUF_SIZE 4096

int rvs_init_connection(struct rvs_context *ctx, const char *ip, const char *port, int is_server) {
    
    struct rdma_addrinfo hints = {}, *res;
    struct ibv_qp_init_attr qp_attr = {};
    int ret;

    ctx->is_server = is_server;
    ctx->size = BUF_SIZE;
    ctx->buf = malloc(ctx->size);
    if (!ctx->buf) return -1;

    hints.ai_port_space = RDMA_PS_TCP;
    hints.ai_flags = is_server ? RAI_PASSIVE : 0;

    ret = rdma_getaddrinfo(is_server ? NULL : ip, port, &hints, &res);
    if (ret) {
        perror("rdma_getaddrinfo");
        return -1;
    }

    ret = rdma_create_ep(&ctx->cm_id, res, NULL, NULL);
    if (ret) {
        perror("rdma_create_ep");
        rdma_freeaddrinfo(res);
        return -1;
    }

    ctx->pd = ctx->cm_id->pd ? ctx->cm_id->pd : ibv_alloc_pd(ctx->cm_id->verbs);
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, ctx->size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    ctx->rkey = ctx->mr->rkey;
    ctx->addr = (uintptr_t)ctx->buf;

    qp_attr.send_cq = ctx->cq = ibv_create_cq(ctx->cm_id->verbs, 10, NULL, NULL, 0);
    qp_attr.recv_cq = ctx->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;

    ret = rdma_create_qp(ctx->cm_id, ctx->pd, &qp_attr);
    if (ret) {
        perror("rdma_create_qp");
        return -1;
    }

    ctx->qp = ctx->cm_id->qp;

    if (is_server) {
        ret = rdma_listen(ctx->cm_id, 1);
        if (ret) {
            perror("rdma_listen");
            return -1;
        }
        ret = rdma_get_request(ctx->cm_id, &ctx->cm_id);
        if (ret) {
            perror("rdma_get_request");
            return -1;
        }
        ret = rdma_accept(ctx->cm_id, NULL);
        if (ret) {
            perror("rdma_accept");
            return -1;
        }
    } else {
        ret = rdma_connect(ctx->cm_id, NULL);
        if (ret) {
            perror("rdma_connect");
            return -1;
        }
    }

    rdma_freeaddrinfo(res);
    return 0;
}

int rs_post_write_with_imm(struct rvs_context *ctx, uint32_t imm, int flags) {
    struct ibv_send_wr wr = {}, *bad_wr = NULL;
    struct ibv_sge sge;

    sge.addr   = (uintptr_t)ctx->buf;
    sge.length = ctx->size;
    sge.lkey   = ctx->mr->lkey;

    wr.wr_id = imm;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = flags;
    wr.imm_data = htonl(imm);
    wr.sg_list = &sge;
    wr.num_sge = 1;

    wr.wr.rdma.remote_addr = ctx->addr; // Normally, you'd exchange this
    wr.wr.rdma.rkey = ctx->rkey;

    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

int rvs_poll_cq(struct rvs_context *ctx) {
    struct ibv_wc wc;
    int ret;

    do {
        ret = ibv_poll_cq(ctx->cq, 1, &wc);
    } while (ret == 0);

    if (ret < 0) {
        perror("ibv_poll_cq");
        return -1;
    }
    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s\n", ibv_wc_status_str(wc.status));
        return -1;
    }
    return 0;
}