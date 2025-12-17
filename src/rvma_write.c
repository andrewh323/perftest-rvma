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

#define MAX_RECV_SIZE 1024*1024 // 1MB

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

RVMA_Win *rvmaInitWindowMailboxKey(void *virtualAddress, key_t key) {

    if (virtualAddress == NULL){
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

RVMA_Win *rvmaInitWindowMailbox(void *virtualAddress) {
    double cpu_ghz = get_cpu_ghz();
    uint64_t start, end, cycles;
    start = rdtsc();
    if (virtualAddress == NULL){
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

    RVMA_Status res = newMailboxIntoHashmap(hashMapPtr, virtualAddress);
    if (res != RVMA_SUCCESS) {
        freeHashmap(&hashMapPtr);
        free(windowPtr);
        print_error("rvmaInitWindowMailbox: Failure creating mailbox in the hashmap...");
        return NULL;
    }
    
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


RVMA_Status rvmaAddMailboxtoWindow(RVMA_Win *window, void *virtualAddress, key_t key) {
    if (window == NULL){
        print_error("rvmaAddMailboxtoWindow: window is null");
        return RVMA_ERROR;
    }
    if (key <= 0){
        print_error("rvmaAddMailboxtoWindow: invalid key");
        return RVMA_ERROR;
    }
    if (virtualAddress == NULL){
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


RVMA_Buffer_Entry* rvmaPostBuffer(void **buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, void *virtualAddress,
               RVMA_Mailbox *mailbox, int64_t epochThreshold, epoch_type epochType) {

    RVMA_Buffer_Entry *entry = createBufferEntry(buffer, size, notificationPtr, notificationLenPtr, epochThreshold, epochType);
    if (entry == NULL) {
        print_error("rvmaPostBuffer: error while malloc new buffer entry");
        return NULL;
    }

    if (mlock(*buffer, size)) {
        print_error("rvmaPostBuffer: buffer memory couldn't be pinned");
        return NULL;
    }

    uint64_t start = rdtsc();
    // Define memory region for buffer
    struct ibv_mr *mr = ibv_reg_mr(mailbox->pd, *buffer, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if(!mr) {
        fprintf(stderr, "rvmaPostBuffer: ibv_reg_mr failed (len=%ld): %s\n", (long)size, strerror(errno));
        free(entry);
        return NULL;
    }
    entry->mr = mr;
    uint64_t end = rdtsc();

    mailbox->regmrCycles = (end - start);

    RVMA_Status res = enqueue(mailbox->bufferQueue, entry);
    if (res != RVMA_SUCCESS) {
        if (munlock(*buffer, size))
            print_error("rvmaPostBuffer: buffer memory couldn't be unpinned");
        print_error("rvmaPostBuffer: buffer entry failed to be added to mailbox queue");
        return NULL;
    }
    return entry;
}


RVMA_Status postRecvPool(RVMA_Mailbox *mailbox, int num_bufs, void *vaddr, epoch_type epochType) {
    uint64_t start, end;
    double cpu_ghz = get_cpu_ghz();
    start = rdtsc();
    for (int i = 0; i < num_bufs; i++) {
        uint64_t singleRecvStart = rdtsc();
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

        RVMA_Buffer_Entry *entry = rvmaPostBuffer(&recv_ptr, MAX_RECV_SIZE, notifBuffPtr, (void *)notifLenPtr,
                                    vaddr, mailbox, threshold, epochType);
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
        uint64_t singleRecvEnd = rdtsc();
    }
    end = rdtsc();
    double elapsed_us = (end - start) / (cpu_ghz * 1e3);
    printf("postRecvPool time for %d buffers: %.3f µs\n", num_bufs, elapsed_us);
    printf("Average receive buffer posting time: %.3f µs\n", elapsed_us / num_bufs);
    return RVMA_SUCCESS;
}


int rvmaPutHybrid(struct ibv_qp *qp, int index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr) {
    if (!qp) {
        print_error("rvmaPutHybrid: qp was NULL");
        return 1;
    }

    if (!wr) {
        print_error("rvmaPutHybrid: wr was NULL");
        return 1;
    }

    if (!bad_wr) {
        print_error("rvmaPutHybrid: bad_wr was NULL");
        return 1;
    }

    if (index < 0) {
        print_error("rvmaPutHybrid: index was negative");
        return 1;
    }

    return ibv_post_send(qp, &wr[index], bad_wr);
}

/*
RVMA PUT STEPS
    1. Post receive buffer to target mailbox
    2. Address translation (this was done before the put in client code)
    3. Prepare payload to be written into memory (ibv_reg_mr)
    4. Perform completion check
        - Increment counter and check it against threshold (Counter is hardware only)
        - If buffer is complete, write address of buffer to completion pointer address
*/
RVMA_Status rvmaSend(void *buf, int64_t size, void *vaddr, RVMA_Mailbox *mailbox) {
    uint64_t start = rdtsc();

    int hardware_counter = 0; // Counter to compare with threshold (should only exist in hardware)

    int64_t threshold = size; // Set threshold to size of buffer (bytes)

    void **notifBuffPtr = malloc(sizeof(void *));
    int *notifLenPtr = malloc(sizeof(int));

    // Post a buffer to the RVMA mailbox
    RVMA_Buffer_Entry *entry = rvmaPostBuffer(&buf, size, notifBuffPtr, (void *)notifLenPtr,
                                            vaddr, mailbox, threshold, EPOCH_BYTES);
    if (!entry) {
        print_error("rvmaSend: rvmaPostBuffer failed");
        return RVMA_ERROR;
    }
    
    // Retrieve buffer entry info
    void *data = *(entry->realBuffAddr);
    int64_t dataSize = entry->realBuffSize;
    uint64_t bufferSetup = rdtsc();
    mailbox->bufferSetupCycles = bufferSetup - start;
    
    uint64_t wr_start = rdtsc();

    struct ibv_send_wr *bad_wr = NULL;

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)data,
        .length = dataSize,
        .lkey = entry->mr->lkey
    };

    // Build wr
    struct ibv_send_wr wr = {
        .wr_id = (uintptr_t)data,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED // Signaled for completion
    };

    uint64_t wr_end = rdtsc();
    mailbox->wrSetupCycles = wr_end - wr_start;

    // Send function
    if (ibv_post_send(mailbox->qp, &wr, &bad_wr)) {
        perror("rvmaSend: ibv_post_send failed");
        return RVMA_ERROR;
    }

    // End timer and update total elapsed time
    uint64_t end = rdtsc();
    uint64_t elapsed = end - start;
    mailbox->cycles = elapsed;

    // Increment hardware counter after posting (by bytes)
    hardware_counter += dataSize;
    if (hardware_counter == threshold) {
        // Write address of head of buffer to notification pointer
        *notifBuffPtr = data;
        // Write length of buffer to notifLenPtr in case buffer is reused
        *notifLenPtr = dataSize;
    }

    uint64_t start_poll = rdtsc();

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

    if (removeEntry(mailbox->bufferQueue, entry) != RVMA_SUCCESS) {
        print_error("rvmaSend: removeEntry failed");
        return RVMA_ERROR;
    }

    if (freeBufferEntry(entry) != RVMA_SUCCESS) {
        print_error("rvmaSend: freeBufferEntry failed");
        return RVMA_ERROR;
    }
    free(notifBuffPtr);
    free(notifLenPtr);

    uint64_t end_poll_cycles = rdtsc();
    mailbox->pollCycles = end_poll_cycles - start_poll;
    return RVMA_SUCCESS;
}


// Recv buffer pool should be preposted, so just poll cq for completions
RVMA_Status rvmaRecv(void *vaddr, RVMA_Mailbox *mailbox, uint64_t *recv_timestamp) {
    struct ibv_wc wc;
    int num_wc;
    do {
        num_wc = ibv_poll_cq(mailbox->cq, 1, &wc);
    } while (num_wc == 0);

    if (num_wc < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "recv completion error: %s (%d)\n", ibv_wc_status_str(wc.status), wc.status);
        return RVMA_ERROR;
    }

    uint64_t t2 = rdtsc();
    if (recv_timestamp) {
        *recv_timestamp = t2;
    }

    RVMA_Buffer_Entry *entry = (RVMA_Buffer_Entry *)wc.wr_id;
    char *recv_buf = (char *)entry->realBuff;
    int len = wc.byte_len;

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)recv_buf,
        .length = entry->realBuffSize,
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
    
    // printf("Received Message: %.*s\n", len, recv_buf);
    
    return RVMA_SUCCESS;
}


RVMA_Status eventCompleted(struct ibv_wc *wc, RVMA_Win *win, void *virtualAddress) {
    static int bufferStatus = 0; // default to active/being used

    if (bufferStatus == 1) { // if the buffer has been retired
        return RVMA_SUCCESS;
    }

    if (win == NULL) {
        print_error("eventCompleted: window was NULL");
        return RVMA_ERROR;
    }
    if (virtualAddress == NULL) {
        print_error("eventCompleted: virtualAddress was NULL");
        return RVMA_ERROR;
    }
    if (wc == NULL) {
        print_error("eventCompleted: wc was NULL");
        return RVMA_ERROR;
    }

    RVMA_Mailbox *mailbox;
    mailbox = searchHashmap(win->hashMapPtr, virtualAddress);

    if (mailbox == NULL) {
        print_error("eventCompleted: mailbox was NULL or did not exist");
        return RVMA_ERROR;
    }

    RVMA_Buffer_Entry *bufferEntry;
    bufferEntry = mailbox->bufferQueue->pBufferEntry[mailbox->bufferQueue->start];

    if (bufferEntry == NULL) {
        print_error("eventCompleted: bufferEntry was NULL or did not exist");
        return RVMA_ERROR;
    }

    int numBytes = wc->byte_len;

    // increment length of buffer
    int *int_ptr = (int *) (*bufferEntry->notifLenPtrAddr);
    if (int_ptr == NULL) {
        print_error("eventCompleted: int_ptr is NULL or did not exist");
        return RVMA_ERROR;
    }
    (*int_ptr) += numBytes;

    if (bufferEntry->epochType == EPOCH_OPS) {
        bufferEntry->epochCount = bufferEntry->epochCount + 1;
    } else if (bufferEntry->epochType == EPOCH_BYTES) {
        bufferEntry->epochCount = bufferEntry->epochCount + numBytes;
    }

    if (bufferEntry->epochCount == bufferEntry->epochThreshold) {
        //remove the buffer
        retireBuffer(mailbox, bufferEntry);

        // set the notification pointer
        (*(int *) (*bufferEntry->notifBuffPtrAddr)) = 1;

        bufferStatus = 1;
    }

    return RVMA_SUCCESS;
}

RVMA_Status rvmaCheckBufferQueue(RVMA_Buffer_Queue *bufferQueue, TestType type, int msgSize) {
    printf("Message Size: %d\n", msgSize);
    if (!bufferQueue) {
        printf("rvmaCheckBufferQueue: Buffer Queue is null\n");
        return RVMA_ERROR;
    }
    if (bufferQueue->size == 0) {
        printf("rvmaCheckBufferQueue: Buffer Queue is empty\n");
        return RVMA_ERROR;
    }

    // For the purposes of our tests we know that the buffer is at index bufferQueue->end
    // as we only post a single buffer in these tests
    RVMA_Buffer_Entry *bufferEntry = bufferQueue->pBufferEntry[bufferQueue->end];
    if (!bufferEntry) {
        printf("rvmaCheckBufferQueue: Buffer Entry is null\n");
        return RVMA_ERROR;
    }
    if (!bufferEntry->realBuffAddr) {
        printf("rvmaCheckBufferQueue: Buffer Entry real buffer is null\n");
        return RVMA_ERROR;
    }
    if (bufferEntry->realBuffSize <= 0) {
        printf("rvmaCheckBufferQueue: Buffer Entry real buffer size is invalid\n");
        return RVMA_ERROR;
    }

    // The RDMA buffer which we are used is actually split into 2 parts based on the way this benchmark works
    // the first half of the buffer is for the sending buffer and the second half is used for the receiving buffer
    // this can be seen in perftest_communication.c:set_up_connection:860-864.
    // So we only iterate from (bufferEntry->realBuffSize/2)+1 to bufferEntry->realBuffSize
    char currentValue;
    uintptr_t bufferAddr = (uintptr_t)bufferEntry->realBuffAddr[0];
    if(type == LAT){
        // For some reason buffer index 4097 is an SOH that can't be overwritten, let's just write to an idx before it
        int i = (bufferEntry->realBuffSize/2) - 1;
        currentValue = *((char *) bufferAddr + i);

        if(currentValue != 'Z'){
            printf("The first char in the receiving buffer at index %d is not Z, it is: %c\n", i, currentValue);
                return RVMA_FAILURE;
            }
    }else{
        for (int i = (bufferEntry->realBuffSize/2) + 1; i < (bufferEntry->realBuffSize/2) + msgSize; i += 1) {
            currentValue = *((char *) bufferAddr + i);
            // In perftest_resources.c:create_single_mr:1755-1760 we default the client buffer to contain all 'Z' chars
            // so we check to make sure all the chars in this buffer are 'Z'. If they are not this indicates an issue in
            // transmission.
            if(currentValue != 'Z'){
                printf("The char at index %d in the receiving buffer is not Z, it is: %c\n", i, currentValue);
                return RVMA_FAILURE;
            }
        }
    }

    return RVMA_SUCCESS;
}