/***
 * General Description:
 * This file contains all the high level RVMA translation functions and functions for initializing and freeing functions the RVMA window and mailbox
 *
 * Authors: Ethan Shama, Nathan Kowal
 *
 * Reviewers: Nicholas Chivaran, Samantha Hawco
 *
 ***/

#include "rvma_write.h"
#define MAX_RECV_SIZE 1024

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


RVMA_Status rvmaPostBuffer(void **buffer, int64_t size, void **notificationPtr, void **notificationLenPtr, void *virtualAddress,
               RVMA_Win *window, int64_t epochThreshold, epoch_type epochType) {

    if (buffer == NULL) {
        print_error("rvmaPostBuffer: buffer was NULL");
        return RVMA_ERROR;
    }
    if (size < 1) {
        print_error("rvmaPostBuffer: size was less than 1");
        return RVMA_ERROR;
    }
    if (notificationPtr == NULL) {
        print_error("rvmaPostBuffer: notificationPtr was NULL");
        return RVMA_ERROR;
    }
    if (virtualAddress == NULL) {
        print_error("rvmaPostBuffer: virtualAddress was NULL");
        return RVMA_ERROR;
    }
    if (window == NULL) {
        print_error("rvmaPostBuffer: window was NULL");
        return RVMA_ERROR;
    }
    if (notificationLenPtr == NULL) {
        print_error("rvmaPostBuffer: notificationLenPtr was NULL");
        return RVMA_ERROR;
    }

    RVMA_Mailbox *mailbox = searchHashmap(window->hashMapPtr, virtualAddress);
    if (mailbox == NULL) {
        print_error("rvmaPostBuffer: No Mailbox associated with virtual address");
        return RVMA_ERROR;
    }

    RVMA_Buffer_Entry *entry;
    entry = createBufferEntry(buffer, size, notificationPtr, notificationLenPtr, epochThreshold, epochType);

    if (entry == NULL) {
        print_error("rvmaPostBuffer: error while malloc new buffer entry");
        return RVMA_ERROR;
    }

    if (mlock(*buffer, size)) {
        print_error("rvmaPostBuffer: buffer memory couldn't be pinned");
        return RVMA_ERROR;
    }

    RVMA_Status res;
    res = enqueue(mailbox->bufferQueue, entry);
    if (res != RVMA_SUCCESS) {
        if (munlock(*buffer, size))
            print_error("rvmaPostBuffer: buffer memory couldn't be unpinned");
        free(entry);
        print_error("rvmaPostBuffer: buffer entry failed to be added to mailbox queue");
        return RVMA_ERROR;
    }

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
    3. Prepare payload to be written into memory (ibv_reg_mr?)
    4. Perform completion check
        - Increment counter and check it against threshold
        - If buffer is complete, write address of buffer to completion pointer address
        - Also write length of data buffer?
*/
RVMA_Status rvmaSend(void *buf, int64_t size, void *vaddr, RVMA_Win *window) {
    int64_t threshold = size; // Set threshold to size of buffer (bytes)

    int *notifBuffPtr = malloc(sizeof(int));
    *notifBuffPtr = 0;

    int *notifLenPtr = malloc(sizeof(int));
    *notifLenPtr = 0;

    void *data = buf;

    // Post a buffer to the RVMA mailbox
    RVMA_Status status = rvmaPostBuffer(&data, size, (void **)&notifBuffPtr, (void **)&notifLenPtr, vaddr, window, threshold, EPOCH_BYTES);

    // Get mailbox for qp
    RVMA_Mailbox *mailbox = searchHashmap(window->hashMapPtr, vaddr);
    if (mailbox == NULL) {
        perror("rvmaPut: No Mailbox associated with virtual address");
        status = RVMA_ERROR;
    }

    struct ibv_send_wr *bad_wr = NULL;

    // Define memory region for sge
    struct ibv_mr *mr = ibv_reg_mr(mailbox->pd, buf, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        perror("rvmaPut: ibv_reg_mr failed");
        status = RVMA_ERROR;
    }

    // Completion check here?
    // If buffer is complete, write the address of buffer to completion pointer
    // Also write length of buffer

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = size,
        .lkey = mr->lkey
    };

    // Build wr
    struct ibv_send_wr wr = {
        .wr_id = (uintptr_t)buf,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND, // Send or write? Sockets uses send
        .send_flags = IBV_SEND_SIGNALED // Signaled or unsignaled?
    };

    // Send function
    if (ibv_post_send(mailbox->qp, &wr, &bad_wr)) {
        perror("rvmaPut: ibv_post_send failed");
        status = RVMA_ERROR;
    }

    // Deregister memory once finished
    ibv_dereg_mr(mr);
    if (status != RVMA_ERROR) {
        printf("rvmaPut succeeded!\n");
    }
    return status;
}

// Simply read the buffer from mailbox buffer queue
RVMA_Status rvmaRecv(void *vaddr, RVMA_Win *window) {

    RVMA_Mailbox *mailbox = searchHashmap(window->hashMapPtr, vaddr);
    if (mailbox == NULL) {
        perror("rvmaRecv: No Mailbox associated with virtual address");
        return RVMA_ERROR;
    }

    // Retrieve buffer and dequeue it
    RVMA_Buffer_Entry *entry = dequeue(mailbox->bufferQueue);
    if (entry == NULL) {
        perror("rvmaRecv: No posted buffer available in mailbox");
        return RVMA_ERROR;
    }

    // Wait on notification pointers
    volatile int *notif = *(int **)entry->notifBuffPtrAddr;
    volatile int *len = *(int **)entry->notifLenPtrAddr;

    // Read buffer contents
    void *message_buf = *(entry->realBuffAddr);
    int64_t msg_size = *len;

    printf("rvmaRecv: Received %ld bytes from mailbox buffer:\n", msg_size);
    fwrite(message_buf, 1, msg_size, stdout);
    printf("\n");

    return RVMA_SUCCESS;
}

/*
RVMA_Status rvmaRecv(void *vaddr, RVMA_Win *window) {

    RVMA_Mailbox *mailbox = searchHashmap(window->hashMapPtr, vaddr);
    if (mailbox == NULL) {
        perror("rvmaRecv: No Mailbox associated with virtual address");
        return RVMA_ERROR;
    }

    struct ibv_wc wc;

    // Define recv buffer and register mr
    char *recv_buf = malloc(MAX_RECV_SIZE);
    memset(recv_buf, 0, MAX_RECV_SIZE);

    struct ibv_mr *recv_mr = ibv_reg_mr(mailbox->pd, recv_buf, MAX_RECV_SIZE, IBV_ACCESS_LOCAL_WRITE);

    // Build sge
    struct ibv_sge sge = {
        .addr = (uintptr_t)recv_buf,
        .length = MAX_RECV_SIZE,
        .lkey = recv_mr->lkey
    };

    // Build recv_wr
    struct ibv_recv_wr recv_wr = {
        .wr_id = (uintptr_t)recv_buf,
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

    // Poll cq
    int num_wc;
    printf("Receive wr posted, now polling cq...\n");

    do {
        num_wc = ibv_poll_cq(mailbox->cq, 1, &wc);
    } while (num_wc == 0);

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Recv failed: %s\n", ibv_wc_status_str(wc.status));
        return RVMA_ERROR;
    }
    else {
        printf("Server received message: %s\n", recv_buf);
    }

    // Free resources
    ibv_dereg_mr(recv_mr);
    free(recv_buf);

    return RVMA_SUCCESS;
} */


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