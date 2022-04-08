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
};

typedef struct conn conn;

// Return 0 on success, 1 on failure
int mcrdma_init(const char *interface, int port);

void mcrdma_destroy(void);

int mcrdma_listen(void);

void *mcrdma_worker(void*);

conn* mcrdma_conn_new(enum conn_states init_state, enum network_transport transport);

void mcrdma_conn_destroy(conn* c);

#endif // MCRDMA_H