#ifndef MCRDMA_H
#define MCRDMA_H

#include "memcached.h"

// backlog of incoming connection requests, used during rdma_listen
#define MCRDMA_BACKLOG 16
#define MCRDMA_DISPATCHER_POLL_TIMEOUT_MS 1000
    
struct mcrdma_state {
    // Connection Management
    struct rdma_event_channel *echannel;
    struct rdma_cm_id *id;

    // Resources
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *client_cq;


    // Send Buffer
    char* sbuf;
    size_t sbuf_size;
    struct ibv_mr* sbuf_mr;

    // Recv Buffer
    char* rbuf;
    size_t rbuf_size;
    struct ibv_mr* rbuf_mr;
    bool rbuf_posted;

    // Not important, used with the PING_PONG benchmark
    bool pinged;
};

// Do not remove
typedef struct conn conn;

// Return 0 on success, 1 on failure
int mcrdma_init(const char *interface, int port);

void mcrdma_destroy(void);

int mcrdma_listen(void);

void *mcrdma_worker(void*);

void mcrdma_state_machine(conn* c);

conn* mcrdma_conn_new(enum conn_states init_state, const int read_buffer_size);

void mcrdma_conn_destroy(conn* c);

#endif // MCRDMA_H