#ifndef _BENCH_MAIN_H
#define _BENCH_MAIN_H

#include "../mcrdma_client/mcrdma_client.h"
#include <stdio.h>
#include <string.h>

#define cprintf(format, ...) do {                       \
        printf("[%02d] " format, tid, ##__VA_ARGS__);   \
    } while(0)

#define CMP(str, expected, len) do {                                                \
    if(memcmp(str, expected, len)) {                                                \
        cprintf("Failed comparison.\nFound: '%s'\nExpected: '%s'\n", str, expected);\
        res->status = CLIENT_FAILED_CMP;                                            \
        return;                                                                     \
    }                                                                               \
} while(0)

#define CLIENT_OK (0)
#define CLIENT_ERR (1)
#define CLIENT_FAILED_CMP (2)
#define CLIENT_UNKNOWN (3)

enum test_type {
    set_get,
    ping_pong
};

struct client_args {
    int tid;
    struct sockaddr_in addr;
};

union client_handle {
    struct mcrdma_client client;    // For RDMA
    int sockfd;                     // For TCP
};

struct client_result {
    int status;

    unsigned long long int set_time;
    unsigned long long int max_set_time;
    unsigned long long int min_set_time;
    unsigned long long int get_time;
    unsigned long long int max_get_time;
    unsigned long long int min_get_time;


    unsigned long long int ping_time;
    unsigned long long int max_ping_time;
    unsigned long long int min_ping_time;

};


extern bool tcp;
extern enum test_type ttype;

extern int clients;
extern int iterations_per_client;
extern char* host;
extern int port;

#endif // _BENCH_MAIN_H