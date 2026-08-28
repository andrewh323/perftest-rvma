#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <rdma/rsocket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "rvma_socket.h"
#include "rvma_write.h"

#define PORT 7471

// Caps how many clients can have their RDMA setup in flight at once
#define NUM_WORKERS 32

typedef struct {
    int tcp_fd;
    int id;
} pending_client_t;

typedef struct {
    pending_client_t *items;
    int capacity;
    int head, tail, count;
    int done_pushing;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} work_queue_t;

static void queue_init(work_queue_t *q, int capacity) {
    q->items = malloc(capacity * sizeof(*q->items));
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->done_pushing = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

static void queue_push(work_queue_t *q, pending_client_t item) {
    pthread_mutex_lock(&q->lock);
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

// Returns 0 with *out filled in, or -1 once the queue is empty (signal to exit)
static int queue_pop(work_queue_t *q, pending_client_t *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->done_pushing) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static void queue_finish(work_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    q->done_pushing = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

typedef struct {
    work_queue_t *queue;
    int msg_size;
    int num_sends;
    RVMA_Win *window;
} worker_ctx_t;

uint32_t get_host_addr(const char *iface_name) {
    struct ifaddrs *ifaddr, *ifa;
    uint32_t ip = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if (strcmp(ifa->ifa_name, iface_name) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            ip = ntohl(sa->sin_addr.s_addr);
            break;
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}

// Finishes a client's RDMA setup and AH handshake, then runs its echo
// loop, before looping back to pull the next pending client off the queue
void *worker_thread(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    pending_client_t pc;

    while (queue_pop(ctx->queue, &pc) == 0) {
        int fd = rvaccept_dgram_finish(pc.tcp_fd, ctx->window);
        if (fd < 0) {
            fprintf(stderr, "Client %d: rvaccept_dgram_finish failed\n", pc.id + 1);
            continue;
        }
        printf("Client %d successfully connected!\n", pc.id + 1);

        char *recv_buf = malloc(ctx->msg_size);
        if (!recv_buf) {
            perror("malloc");
            continue;
        }

        for (int i = 0; i < ctx->num_sends; i++) {
            int ret = rvrecvfrom(fd, recv_buf, ctx->msg_size, 0, NULL, NULL);
            if (ret < 0) {
                perror("rvrecvfrom");
                break;
            }

            ret = rvsendto(fd, recv_buf, ctx->msg_size, NULL, 0, ctx->window);
            if (ret < 0) {
                perror("rvsendto");
                break;
            }
        }

        free(recv_buf);
    }

    return NULL;
}

int main(int argc, char **argv) {
    uint16_t reserved = 0x0001;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    int listen_fd;

    // Test parameters
    int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    int num_clients = 1;
    if (argc > 2) {
        num_clients = atoi(argv[2]);
    }

    int num_sends = 1000;

    uint32_t host_ip = get_host_addr("ib0");
    uint64_t vaddr = constructVaddr(reserved, host_ip, PORT);
    printf("Constructed virtual address: %" PRIu64 "\n", vaddr);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

    RVMA_Win *windowPtr = rvmaInitWindow();

    listen_fd = rvsocket(SOCK_DGRAM, vaddr, windowPtr);

    // Bind address to socket
    rvbind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("Server listening for datagram clients on port %d...\n", PORT);

    work_queue_t queue;
    queue_init(&queue, num_clients > 0 ? num_clients : 1);

    int num_workers = NUM_WORKERS < num_clients ? NUM_WORKERS : num_clients;
    if (num_workers < 1) num_workers = 1;

    worker_ctx_t wctx = { &queue, size, num_sends, windowPtr };
    pthread_t workers[num_workers];
    int workers_started = 0;

    for (int w = 0; w < num_workers; w++) {
        if (pthread_create(&workers[w], NULL, worker_thread, &wctx) != 0) {
            perror("pthread_create");
            break;
        }
        workers_started++;
    }

    if (workers_started == 0) {
        fprintf(stderr, "Failed to start any worker threads\n");
        return -1;
    }

    for (int i = 0; i < num_clients; i++) {
        int tcp_fd = rvaccept_dgram_begin(listen_fd, NULL, NULL);

        if (tcp_fd < 0) {
            fprintf(stderr, "rvaccept_dgram_begin failed for client %d, skipping\n", i + 1);
            continue;
        }

        queue_push(&queue, (pending_client_t){ .tcp_fd = tcp_fd, .id = i });
    }
    queue_finish(&queue);

    for (int w = 0; w < workers_started; w++) {
        pthread_join(workers[w], NULL);
    }

    if (freeHashmap(&windowPtr->hashMapPtr) != RVMA_SUCCESS) {
        print_error("Failed to free mailbox hashmap");
        return -1;
    }

    close(listen_fd);
    return 0;
}
