#ifndef MCRDMA_CLIENT_H
#define MCRDMA_CLIENT_H

#include <netinet/in.h>
#include <stdbool.h>

struct mcrdma_client {
    struct sockaddr_in addr;

    // Connection management
    struct rdma_event_channel *echannel;
    struct rdma_cm_id *id;

    // Resources
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;

    char* rbuf;
    size_t rbuf_size;
    struct ibv_mr* rbuf_mr;
    bool rbuf_posted;

    char* sbuf;
    size_t sbuf_size;
    struct ibv_mr* sbuf_mr;
};

#define CQ_CAPACITY (16)
#define MAX_SGE (8)
#define MAX_WR (16)

int mcrdma_client_init(struct mcrdma_client *client);

int mcrdma_client_alloc_resources(struct mcrdma_client* client);

int mcrdma_client_connect(struct mcrdma_client* client);
int mcrdma_client_disconnect(struct mcrdma_client* client);

int mcrdma_client_ascii_send(struct mcrdma_client* client, size_t len);
int mcrdma_client_ascii_send_buf(struct mcrdma_client* client, char* buf, size_t buf_size);

int mcrdma_client_ascii_recv(struct mcrdma_client* client);

#endif // MCRDMA_CLIENT_H