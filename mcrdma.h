#ifndef MCRDMA_H
#define MCRDMA_H

// backlog of incoming connection requests, used during rdma_listen
#define MCRDMA_BACKLOG 16
#define MCRDMA_DISPATCHER_POLL_TIMEOUT_MS 1000

#define mcrdma_log(...) fprintf(stderr, __VA_ARGS__);

#define mcrdma_error(...) do {            \
        fprintf(stderr, "MCRDMA ERROR: ");\
        fprintf(stderr, __VA_ARGS__);     \
        fprintf(stderr, " (%d)\n", errno);\
    } while(1);

// Return 0 on success, 1 on failure
int mcrdma_init(const char *interface, int port);

void mcrdma_destroy(void);

int mcrdma_listen(void);

#endif // MCRDMA_H