/***
 * General Description:
 * This file contains all the high level RVMA translation functions and functions for initializing and freeing functions the RVMA window and mailbox
 *
 * Authors: Ethan Shama, Nathan Kowal
 *
 * Reviewers: Nicholas Chivaran, Samantha Hawco
 *
 ***/
#include <stdint.h>
#include <math.h>
#include "rvma_write.h"
#include "rvma_socket.c"

#define MAX_SEND_SIZE 1024*1024
#define MAX_RECV_SIZE 1024*1024 // 1 MB

// Helper function to get CPU frequency
double get_cpu_ghz() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 2.4; // default
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        double mhz;
        if (sscanf(line, "cpu MHz\t: %lf", &mhz) == 1) {
            fclose(fp);
            return mhz / 1000.0; // GHz
        }
    }
    fclose(fp);
    return 2.4;
}

RVMA_Win *rvmaInitWindowMailboxKey(uint64_t virtualAddress, key_t key) {

    if (!virtualAddress){
        print_error("rvmaInitWindowMailboxKey: Virtual address is null");
        return NULL;
    }
    if (key <= 0){
        print_error("rvmaInitWindowMailboxKey: invalid key");
        return NULL;
    }

    RVMA_Win *windowPtr;
    windowPtr = (RVMA_Win *) malloc(sizeof(RVMA_Win));
    if (!windowPtr) {
        print_error("rvmaInitWindowMailboxKey: Failed to allocate memory for windowPtr...");
        return NULL;
    }

    Mailbox_HashMap *hashMapPtr;
    hashMapPtr = initMailboxHashmap();
    if (!hashMapPtr) {
        free(windowPtr);
        print_error("rvmaInitWindowMailboxKey: Failure creating mailbox hashmap...");
        return NULL;
    }

    RVMA_Status res = newMailboxIntoHashmap(hashMapPtr, virtualAddress);
    if (res != RVMA_SUCCESS) {
        freeHashmap(&hashMapPtr);
        free(windowPtr);
        print_error("rvmaInitWindowMailboxKey: Failure creating mailbox in the hashmap...");
        return NULL;
    }

    windowPtr->hashMapPtr = hashMapPtr;
    windowPtr->key = key;

    return windowPtr;
}

RVMA_Win *rvmaInitWindowMailbox(uint64_t virtualAddress) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end, cycles;
    start = rdtsc();
    if (!virtualAddress){
        print_error("rvmaInitWindowMailbox: Virtual address is null");
        return NULL;
    }

    RVMA_Win *windowPtr;
    windowPtr = (RVMA_Win *) malloc(sizeof(RVMA_Win));
    if (!windowPtr) {
        print_error("rvmaInitWindowMailbox: Failed to allocate memory for windowPtr...");
        return NULL;
    }

    Mailbox_HashMap *hashMapPtr;
    hashMapPtr = initMailboxHashmap();
    if (!hashMapPtr) {
        free(windowPtr);
        print_error("rvmaInitWindowMailbox: Failure creating mailbox hashmap...");
        return NULL;
    }
    /* 
    RVMA_Status res = newMailboxIntoHashmap(hashMapPtr, virtualAddress);
    if (res != RVMA_SUCCESS) {
        freeHashmap(&hashMapPtr);
        free(windowPtr);
        print_error("rvmaInitWindowMailbox: Failure creating mailbox in the hashmap...");
        return NULL;
    }
    */

    windowPtr->hashMapPtr = hashMapPtr;
    windowPtr->key = -1;

    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("Window init setup time: %.3f µs\n", elapsed_us);

    return windowPtr;
}

RVMA_Win *rvmaInitWindow() {
    RVMA_Win *windowPtr;
    windowPtr = (RVMA_Win *) malloc(sizeof(RVMA_Win));
    if (!windowPtr) {
        print_error("rvmaInitWindow: Failed to allocate memory for windowPtr...");
        return NULL;
    }

    Mailbox_HashMap *hashMapPtr;
    hashMapPtr = initMailboxHashmap();
    if (!hashMapPtr) {
        free(windowPtr);
        print_error("rvmaInitWindow: Failure creating mailbox hashmap...");
        return NULL;
    }

    windowPtr->hashMapPtr = hashMapPtr;
    windowPtr->key = -1;

    return windowPtr;
}

RVMA_Status rvmaSetKey(RVMA_Win *win, key_t key) {
    if (win == NULL){
        print_error("rvmaSetKey: window is null");
        return RVMA_ERROR;
    }
    if (win->key != -1) {
        print_error("rvmaSetKey: key already set, can not update key");
        return RVMA_FAILURE;
    }

    win->key = key;

    return RVMA_SUCCESS;
}


RVMA_Status rvmaAddMailboxtoWindow(RVMA_Win *window, uint64_t virtualAddress, key_t key) {
    if (window == NULL){
        print_error("rvmaAddMailboxtoWindow: window is null");
        return RVMA_ERROR;
    }
    if (key <= 0){
        print_error("rvmaAddMailboxtoWindow: invalid key");
        return RVMA_ERROR;
    }
    if (!virtualAddress){
        print_error("rvmaAddMailboxtoWindow: virtual address is null");
        return RVMA_ERROR;
    }

    if (window->key != key) {
        print_error("rvmaAddMailboxtoWindow: passed key doesnt match window key...");
        return RVMA_FAILURE;
    }

    RVMA_Status res = newMailboxIntoHashmap(window->hashMapPtr, virtualAddress);
    if (res != RVMA_SUCCESS) {
        print_error("rvmaAddMailboxtoWindow: Failure creating new mailbox in the hashmap...");
        return RVMA_FAILURE;
    }

    return RVMA_SUCCESS;
}

RVMA_Status rvmaCloseWin(RVMA_Win *window) {
    if (window == NULL){
        print_error("rvmaCloseWin: window is null");
        return RVMA_ERROR;
    }

    freeHashmap(&window->hashMapPtr);
    window->hashMapPtr = NULL;
    free(window);
    window = NULL;

    return RVMA_SUCCESS;
}

RVMA_Status postSendPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType) {
    size_t total_size = MAX_BYTES;
    size_t buffer_size = total_size / num_bufs;
    if (buffer_size > MAX_RECV_SIZE) {
        buffer_size = MAX_RECV_SIZE;
    }
    total_size = buffer_size * num_bufs;
    
    mailbox->send_pool = malloc(total_size);
    if (!mailbox->send_pool) {
        print_error("postSendPool: malloc failed");
        return RVMA_ERROR;
    }

    // Register memory region as one large chunk
    mailbox->send_mr = ibv_reg_mr(mailbox->pd, mailbox->send_pool, total_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!mailbox->send_mr) {
        perror("ibv_reg_mr send_pool failed");
        return RVMA_ERROR;
    }

    printf("Posting send pool of %d buffers\n", num_bufs);

    for (int i = 0; i < num_bufs; i++) {
        // Define buffer address for specific entry
        char *send_buf = (char *)mailbox->send_pool + i * buffer_size;
        void *send_ptr = send_buf;

        void **notifBuffPtr = malloc(sizeof(void *));
        int *notifLenPtr = malloc(sizeof(int));
        
        int64_t threshold;
        if (epochType == EPOCH_OPS) {
            // Ceiling divide
            threshold = (buffer_size + RS_MAX_TRANSFER - 1) / RS_MAX_TRANSFER;
        }
        else if (epochType == EPOCH_BYTES) {
            threshold = buffer_size;
        }
        else {
            print_error("postSendPool: invalid epoch type");
            return RVMA_ERROR;
        }

        RVMA_Buffer_Entry *entry = createBufferEntry(send_ptr, buffer_size, notifBuffPtr, (void *)notifLenPtr, threshold, epochType);
        if (!entry) {
            print_error("postSendPool: rvmaPostBuffer failed");
            return RVMA_ERROR;
        }

        entry->mr = mailbox->send_mr;
        enqueue(mailbox->sendBufferQueue, entry);
    }
    return RVMA_SUCCESS;
}

RVMA_Status postRecvPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType) {
    size_t total_size = MAX_BYTES;
    size_t buffer_size = total_size / num_bufs; // Divide buffers into chunks
    if (buffer_size > MAX_RECV_SIZE) {
        buffer_size = MAX_RECV_SIZE;
    }
    total_size = buffer_size * num_bufs;

    mailbox->recv_pool = malloc(total_size);
    if (!mailbox->recv_pool) {
        print_error("postRecvPool: malloc failed");
        return RVMA_ERROR;
    }
    
    mailbox->recv_mr = ibv_reg_mr(mailbox->pd, mailbox->recv_pool, total_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!mailbox->recv_mr) {
        perror("ibv_reg_mr recv_pool failed");
        return RVMA_ERROR;
    }

    for (int i = 0; i < num_bufs; i++) {
        char *recv_buf = (char *)mailbox->recv_pool + i * buffer_size;
        void *recv_ptr = recv_buf;

        // Setup notification pointers (per-buffer notification)
        void **notifBuffPtr = malloc(sizeof(void *));
        int *notifLenPtr = malloc(sizeof(int));

        int64_t threshold;
        if (epochType == EPOCH_OPS) {
            threshold = (buffer_size + RS_MAX_TRANSFER - 1) / RS_MAX_TRANSFER;
        }
        else if (epochType == EPOCH_BYTES) {
            threshold = buffer_size;
        }
        else {
            print_error("postRecvPool: invalid epoch type");
            return RVMA_ERROR;
        }

        RVMA_Buffer_Entry *entry = createBufferEntry(recv_ptr, buffer_size, notifBuffPtr, (void *)notifLenPtr, threshold, epochType);
        if (!entry) {
            print_error("postRecvPool: rvmaPostBuffer failed");
            return RVMA_ERROR;
        }

        entry->mr = mailbox->recv_mr;

        enqueue(mailbox->recvBufferQueue, entry);

        // Build sge and wr, then post recv
        struct ibv_sge sge = {
            .addr = (uintptr_t)recv_ptr,
            .length = buffer_size,
            .lkey = entry->mr->lkey
        };

        struct ibv_recv_wr recv_wr = {
            .wr_id = (uintptr_t)entry,
            .sg_list = &sge,
            .num_sge = 1,
            .next = NULL
        };

        struct ibv_recv_wr *bad_wr = NULL;
        if (ibv_post_recv(mailbox->qp, &recv_wr, &bad_wr)) {
            perror("postRecvPool: ibv_post_recv failed");
            return RVMA_ERROR;
        }
    }
    mailbox->posted_recvs += num_bufs;
    return RVMA_SUCCESS;
}

RVMA_Status rvmaSend(void *buf, int64_t size, uint64_t vaddr, RVMA_Mailbox *mailbox) {
    // Pull a buffer from the mailbox's buffer queue
    RVMA_Buffer_Entry *entry = dequeue(mailbox->sendBufferQueue);
    if (!entry) {
        return RVMA_RETRY;
    }

    // Fill the buffer with data to send
    memcpy(entry->realBuff, buf, size);
    void *data = entry->realBuff;
    int64_t dataSize = size;

    unsigned int flags = 0;
    if (mailbox->sendCount % SIGNAL_INTERVAL == 0) {
        flags |= IBV_SEND_SIGNALED;
    }

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)data,
        .length = dataSize,
        .lkey = entry->mr->lkey
    };

    // Build wr
    struct ibv_send_wr send_wr = {
        .wr_id = (uintptr_t)entry,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED // TODO: signal every N sends
    };
    // printf("Posting send: buffer addr=%p, size=%ld\n", data, dataSize);

    if (mailbox->outstanding_sends >= mailbox->max_outstanding_sends - 1) {
        return RVMA_RETRY;
    }
    struct ibv_send_wr *bad_wr = NULL;
    if (ibv_post_send(mailbox->qp, &send_wr, &bad_wr)) {
        perror("rvmaSend: ibv_post_send failed");
        return RVMA_ERROR;
    }
    
    mailbox->sendCount++;

    // RVMA hardware counter check after posting (by bytes)
    int64_t hardware_counter = dataSize;
    if (hardware_counter == entry->epochThreshold) {
        // Write address of head of buffer to notification pointer
        entry->notifBuffPtrAddr = data;
        // Write length of buffer to notifLenPtr in case buffer is reused
        entry->notifLenPtrAddr = dataSize;
    }

    mailbox->outstanding_sends++;
    return RVMA_SUCCESS;
}

// Check on completions with a progress engine
void rvmaProgress(RVMA_Mailbox *mailbox) {
    int num_wc = 128;
    struct ibv_wc send_wc[num_wc];
    int sn = ibv_poll_cq(mailbox->send_cq, num_wc, send_wc);
    if (sn < 0) {
        fprintf(stderr, "Send CQ error: %s (%d)\n", ibv_wc_status_str(send_wc[0].status), send_wc[0].status);
        return;
    }

    for (int i = 0; i < sn; i++) {
        if (send_wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Completion error: %s (%d)\n", ibv_wc_status_str(send_wc[i].status), send_wc[i].status);
            continue;
        }
        // Check opcode and handle completion
        if (send_wc[i].opcode != IBV_WC_SEND) {
            fprintf(stderr, "Unexpected completion opcode: %d\n", send_wc[i].opcode);
            continue;
        }
        RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)send_wc[i].wr_id;
        enqueue(mailbox->sendBufferQueue, entry);
        mailbox->outstanding_sends--;
    }

    struct ibv_wc recv_wc[num_wc];
    int rn = ibv_poll_cq(mailbox->recv_cq, num_wc, recv_wc);
    if (rn < 0) {
        fprintf(stderr, "Recv CQ error: %s (%d)\n", ibv_wc_status_str(recv_wc[0].status), recv_wc[0].status);
        return;
    }

    for (int i = 0; i < rn; i++) {
        if (recv_wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Recv CQ error: %s (%d)\n",
                    ibv_wc_status_str(recv_wc[i].status), recv_wc[i].status);
            continue;
        }
        if (recv_wc[i].opcode != IBV_WC_RECV) {
            fprintf(stderr, "Unexpected completion opcode: %d\n", recv_wc[i].opcode);
            continue;
        }

        RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)recv_wc[i].wr_id;
        int len = recv_wc[i].byte_len;
        mailbox->posted_recvs--;
        // printf("Received message: %.*s\n", len, (char *)entry->realBuff);
        enqueue(mailbox->recvBufferQueue, entry);
    }
    
    while (mailbox->posted_recvs < mailbox->max_recvs) {
        RVMA_Buffer_Entry *entry = dequeue(mailbox->recvBufferQueue);
        if (!entry) break;

        struct ibv_sge sge = {
            .addr = (uintptr_t)entry->realBuff,
            .length = MAX_RECV_SIZE,
            .lkey = entry->mr->lkey
        };

        struct ibv_recv_wr wr = {
            .wr_id = (uintptr_t)entry,
            .sg_list = &sge,
            .num_sge = 1
        };

        // printf("Posting recv with buffer addr=%p, size=%d\n", entry->realBuff, MAX_RECV_SIZE);
        if(ibv_post_recv(mailbox->qp, &wr, NULL)) {
            // If posting fails, put entry back and break
            enqueue(mailbox->recvBufferQueue, entry);
            break;
        }

        mailbox->posted_recvs++;
    }
}

void rvmaProgressUD(RVMA_Mailbox *mailbox) {
    // Send path
    int num_wc = 128;
    struct ibv_wc send_wc[num_wc];
    int sn = ibv_poll_cq(mailbox->send_cq, num_wc, send_wc);
    if (sn < 0) {
        fprintf(stderr, "Send CQ error: %s (%d)\n", ibv_wc_status_str(send_wc[0].status), send_wc[0].status);
        return;
    }

    for (int i = 0; i < sn; i++) {
        if (send_wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Completion error: %s (%d)\n", ibv_wc_status_str(send_wc[i].status), send_wc[i].status);
            continue;
        }
        // Check opcode and handle completion
        if (send_wc[i].opcode != IBV_WC_SEND) {
            fprintf(stderr, "Unexpected completion opcode: %d\n", send_wc[i].opcode);
            continue;
        }
        RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)send_wc[i].wr_id;
        enqueue(mailbox->sendBufferQueue, entry);
        mailbox->outstanding_sends--;
    }

    // Recv path
    struct ibv_wc recv_wc[num_wc];
    int rn;
    struct ibv_wc *wc;
    while ((rn = ibv_poll_cq(mailbox->recv_cq, num_wc, recv_wc)) > 0) {
        for (int i = 0; i < rn; i++) {
            wc = &recv_wc[i];
            if (wc->status != IBV_WC_SUCCESS) continue;

            RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)wc->wr_id;
            char *recv_buf = (char *)entry->realBuff;

            int grh = (wc->wc_flags & IBV_WC_GRH) ? 40 : 0;
            char *data = recv_buf + grh;
            int data_len = wc->byte_len - grh;

            struct dgram_frag_header *hdr = (struct dgram_frag_header *)data;

            char *payload = data + sizeof(*hdr);
            int payload_len = data_len - sizeof(*hdr);

            // Process reassembly here

            if (hdr->frag_num == 1) {
                // set total frags, received_frags & length = 0, allocate buffer
            }

            // Adjust offset and prepare next buffer space

            // Increment received frags and length
            // Check for completion, if complete enqueue it to completed recv queue
            
            // Repost recv buffer
        }
    }
}

// Redundant now with rvmaProgress
RVMA_Status rvmaRecv(uint64_t vaddr, void *buf, size_t len, int flags, RVMA_Mailbox *mailbox) {
    struct ibv_wc wc;
    int num_wc;
    do {
        num_wc = ibv_poll_cq(mailbox->recv_cq, 1, &wc);
    } while (num_wc == 0);

    if (num_wc < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "recv completion error: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        return RVMA_ERROR;
    }

    RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)wc.wr_id;
    buf = (char *)entry->realBuff;
    // printf("Received Message: %.*s\n", wc.byte_len, buf);

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = len,
        .lkey = entry->mr->lkey
    };

    // Build recv_wr
    struct ibv_recv_wr recv_wr = {
        .wr_id = (uintptr_t)entry,
        .sg_list = &sge,
        .num_sge = 1,
        .next = NULL
    };
    struct ibv_recv_wr *bad_wr = NULL;

    // Post recv
    if (ibv_post_recv(mailbox->qp, &recv_wr, &bad_wr)) {
        perror("ibv_post_recv failed");
        return RVMA_ERROR;
    }
    
    return RVMA_SUCCESS;
}