#ifndef MCRDMA_CLIENT_H
#define MCRDMA_CLIENT_H

#include <netinet/in.h>

struct mcrdma_client {
    struct sockaddr_in addr;

    // Connection management
    struct rdma_event_channel *echannel;
    struct rdma_cm_id *id;

    // Resources
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;

    char* buf;
    size_t buf_size;
    struct ibv_mr* buf_mr;
};

#define CQ_CAPACITY (16)
#define MAX_SGE (8)
#define MAX_WR (16)

int mcrdma_client_init(struct mcrdma_client *client);

int mcrdma_client_alloc_resources(struct mcrdma_client* client);

int mcrdma_client_connect(struct mcrdma_client* client);

int mcrdma_client_ascii_send(struct mcrdma_client* client, char* buf, size_t buf_size);

#endif // MCRDMA_CLIENT_H