#ifndef BENCH_TCP_H
#define BENCH_TCP_H

#include "main.h"
#include <netinet/in.h>

int bench_tcp_init(union client_handle *handle, int tid, struct sockaddr_in addr);

void bench_tcp_worker(struct client_result* res, union client_handle *handle, int tid);

#endif // BENCH_TCP_H