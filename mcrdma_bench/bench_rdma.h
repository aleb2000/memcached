#ifndef BENCH_RDMA_H
#define BENCH_RDMA_H

#include "main.h"
#include <netinet/in.h>

int bench_rdma_init(union client_handle *handle, int tid, struct sockaddr_in addr);
void bench_rdma_destroy(union client_handle *handle, int tid);

void bench_rdma_worker(struct client_result* res, union client_handle *handle, int tid);

#endif // BENCH_RDMA_H