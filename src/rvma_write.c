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

// Buffer type 0 for send buffer, 1 for recv buffer
RVMA_Buffer_Entry* rvmaPostBuffer(void *buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, uint64_t virtualAddress,
               RVMA_Mailbox *mailbox, int64_t epochThreshold, epoch_type epochType, int bufferType) {

    RVMA_Buffer_Entry *entry = createBufferEntry(buffer, size, notificationPtr, notificationLenPtr, epochThreshold, epochType);
    if (entry == NULL) {
        print_error("rvmaPostBuffer: error while malloc new buffer entry");
        return NULL;
    }

    uint64_t start = rdtsc();
    // Define memory region for buffer
    struct ibv_mr *mr = ibv_reg_mr(mailbox->pd, buffer, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if(!mr) {
        fprintf(stderr, "ibv_reg_mr failed: %s (%d)\n", strerror(errno), errno);
        free(entry);
        return NULL;
    }
    entry->mr = mr;

    // send buffer
    if (bufferType == 0) {
        RVMA_Status res = enqueue(mailbox->sendBufferQueue, entry);
        if (res != RVMA_SUCCESS) {
            print_error("rvmaPostBuffer: buffer entry failed to be added to mailbox send queue");
            free(entry);
            ibv_dereg_mr(mr);
            return NULL;
        }
    }
    // recv buffer
    else if (bufferType == 1) {
        RVMA_Status res = enqueue(mailbox->recvBufferQueue, entry);
        if (res != RVMA_SUCCESS) {
            print_error("rvmaPostBuffer: buffer entry failed to be added to mailbox recv queue");
            free(entry);
            ibv_dereg_mr(mr);
            return NULL;
        }
    }
    else {
        print_error("rvmaPostBuffer: invalid buffer type");
        free(entry);
        ibv_dereg_mr(mr);
        return NULL;
    }
    return entry;
}

RVMA_Status postSendPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType) {
    for (int i = 0; i < num_bufs; i++) {
        char *send_buf = malloc(MAX_SEND_SIZE);
        if (!send_buf) {
            print_error("postSendPool: malloc failed");
            return RVMA_ERROR;
        }
        void *send_ptr = send_buf;

        void **notifBuffPtr = malloc(sizeof(void *));
        int *notifLenPtr = malloc(sizeof(int));
        
        int64_t threshold;
        if (epochType == EPOCH_OPS) {
            threshold = MAX_RECV_SIZE / RS_MAX_TRANSFER;
        }
        else if (epochType == EPOCH_BYTES) {
            threshold = MAX_RECV_SIZE;
        }
        else {
            print_error("postSendPool: invalid epoch type");
            return RVMA_ERROR;
        }

        RVMA_Buffer_Entry *entry = rvmaPostBuffer(send_ptr, MAX_SEND_SIZE, notifBuffPtr, (void *)notifLenPtr, vaddr, mailbox, threshold, epochType, 0);
        if (!entry) {
            print_error("postSendPool: rvmaPostBuffer failed");
            return RVMA_ERROR;
        }
    }
    return RVMA_SUCCESS;
}

RVMA_Status postRecvPool(RVMA_Mailbox *mailbox, int num_bufs, uint64_t vaddr, epoch_type epochType) {
    uint64_t start, end;
    start = rdtsc();
    for (int i = 0; i < num_bufs; i++) {
        char *recv_buf = malloc(MAX_RECV_SIZE);
        if (!recv_buf) {
            print_error("postRecvPool: malloc failed");
            return RVMA_ERROR;
        }
        void *recv_ptr = recv_buf;

        // Setup notification pointers (per-buffer notification)
        void **notifBuffPtr = malloc(sizeof(void *));
        int *notifLenPtr = malloc(sizeof(int));

        int64_t threshold;
        if (epochType == EPOCH_OPS) {
            threshold = MAX_RECV_SIZE / RS_MAX_TRANSFER;
        }
        else if (epochType == EPOCH_BYTES) {
            threshold = MAX_RECV_SIZE;
        }
        else {
            print_error("postRecvPool: invalid epoch type");
            return RVMA_ERROR;
        }

        RVMA_Buffer_Entry *entry = rvmaPostBuffer(recv_ptr, MAX_RECV_SIZE, notifBuffPtr, (void *)notifLenPtr,
                                    vaddr, mailbox, threshold, epochType, 1);
        if (!entry) {
            print_error("postRecvPool: rvmaPostBuffer failed");
            return RVMA_ERROR;
        }

        // Build sge and wr, then post recv
        struct ibv_sge sge = {
            .addr = (uintptr_t)recv_ptr,
            .length = MAX_RECV_SIZE,
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
    return RVMA_SUCCESS;
}

/*
RVMA PUT STEPS
    1. Post receive buffer to target mailbox
    2. Address translation
    3. Prepare payload to be written into memory
    4. Perform completion check
        - Increment counter and check it against threshold (Counter is hardware only)
        - If buffer is complete, write address of buffer to completion pointer address
    5. Post send
*/
RVMA_Status rvmaSend(void *buf, int64_t size, uint64_t vaddr, RVMA_Mailbox *mailbox) {
    // Pull a buffer from the mailbox's buffer queue
    RVMA_Buffer_Entry *entry = dequeue(mailbox->sendBufferQueue);
    if (!entry) {
        print_error("rvmaSend: No available send buffers in mailbox queue");
        return RVMA_FAILURE;
    }

    // Fill the buffer with data to send
    memcpy(entry->realBuff, buf, size);
    void *data = entry->realBuff;
    int64_t dataSize = size;

    struct ibv_send_wr *bad_wr = NULL;

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
        .send_flags = IBV_SEND_SIGNALED // Signaled for completion
    };

    // Send function
    if (ibv_post_send(mailbox->qp, &send_wr, &bad_wr)) {
        perror("rvmaSend: ibv_post_send failed");
        return RVMA_ERROR;
    }

    // Increment hardware counter after posting (by bytes)
    int64_t hardware_counter = dataSize;
    if (hardware_counter == entry->epochThreshold) {
        // Write address of head of buffer to notification pointer
        entry->notifBuffPtrAddr = data;
        // Write length of buffer to notifLenPtr in case buffer is reused
        entry->notifLenPtrAddr = dataSize;
    }

    // Poll cq
    struct ibv_wc wc;
    int res;
    do {
        res = ibv_poll_cq(mailbox->cq, 1, &wc);
    } while (res == 0);
    if (res < 0) {
        perror("rvmaSend: ibv_poll_cq failed");
        return RVMA_ERROR;
    }
    if (wc.status!= IBV_WC_SUCCESS) {
        perror("rvmaSend: ibv_poll_cq failed");
        return RVMA_ERROR;
    }

    // Maybe TODO: Add in buffer status field to track in-flight buffers
    // Repost the buffer back to the send queue
    RVMA_Buffer_Entry *completed_entry = (RVMA_Buffer_Entry *)wc.wr_id;
    enqueue(mailbox->sendBufferQueue, completed_entry);

    return RVMA_SUCCESS;
}


// Recv buffer pool should be preposted, so just poll cq for completions
RVMA_Status rvmaRecv(uint64_t vaddr, void *buf, size_t len, int flags, RVMA_Mailbox *mailbox) {
    struct ibv_wc wc;
    int num_wc;
    do {
        num_wc = ibv_poll_cq(mailbox->cq, 1, &wc);
    } while (num_wc == 0);

    if (num_wc < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "recv completion error: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        return RVMA_ERROR;
    }

    RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)wc.wr_id;
    buf = (char *)entry->realBuff;
    // printf("Received Message: %.*s\n", wc.byte_len, recv_buf);

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