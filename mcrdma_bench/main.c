#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "main.h"
#include "bench_rdma.h"
#include "bench_tcp.h"
#include "bench_utils.h"

bool tcp = false;
enum test_type ttype = set_get;

int clients = 0;
int iterations_per_client = 0;
char* host = NULL;
int port = 0;

pthread_barrier_t barrier;

static void* single_client(void* arg) {
    struct client_args* args = (struct client_args*) arg;
    int tid = args->tid;
    union client_handle client;

    struct client_result* res = malloc(sizeof(struct client_result));
    res->status = CLIENT_UNKNOWN;

    if(tcp) {
        if(bench_tcp_init(&client, tid, args->addr)) {
            res->status = CLIENT_ERR;
            return res;
        }
    } else {
        if(bench_rdma_init(&client, tid, args->addr)) {
            res->status = CLIENT_ERR;
            return res;
        }
    }

    cprintf("Connection established!\n");

    // Wait for all clients to connect before blasting all the requests at once
    pthread_barrier_wait(&barrier);

    if(tcp) {
        bench_tcp_worker(res, &client, tid);
    } else {
        bench_rdma_worker(res, &client, tid);
        bench_rdma_destroy(&client, tid);
    }

    free(args);

    return res;
}

int main(int argc, char** argv) {
    if(argc != 8) {
        printf("Usage: rdma_client NET TEST HOST PORT CLIENTS ITERS PAYLOAD_SIZE\n");
        printf("\nNET:\n");
        printf("\t-r --rdma : benchmark rdma\n");
        printf("\t-t --tcp  : benchmark tcp\n");
        printf("\nTEST:\n");
        printf("\t-s --set-get    : SET_GET test\n");
        printf("\t-p --ping-pong  : PING_PONG test\n");
        printf("\nPAYLOAD_SIZE: size of the payload in bytes as a number. The maximum value is %d\n", MAX_PAYLOAD_SIZE);
        return 0;
    } else {
        if(strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--tcp") == 0) {
            tcp = true;
        } else if(strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--rdma") == 0) {
            tcp = false;
        } else {
            printf("Unknown flag: %s\n", argv[1]);
            return 0;
        }

        if(strcmp(argv[2], "-s") == 0 || strcmp(argv[2], "--set-get") == 0) {
            ttype = set_get;
        } else if(strcmp(argv[2], "-p") == 0 || strcmp(argv[2], "--ping-pong") == 0) {
            ttype = ping_pong;
        } else {
            printf("Unknown flag: %s\n", argv[1]);
            return 0;
        }

        host = argv[3];
        port = atoi(argv[4]);
        clients = atoi(argv[5]);
        iterations_per_client = atoi(argv[6]);
        int payload_len = atoi(argv[7]);
        if(payload_len == 0 || payload_len > MAX_PAYLOAD_SIZE) {
            printf("Invalid payload size\n");
            return 0;
        }
        init_payload(payload_len);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);


    if(!inet_pton(AF_INET, host, &addr.sin_addr)) {
        printf("Invalid host address: %s\n", host);
        return 1;
    }

    if(pthread_barrier_init(&barrier, NULL, clients)) {
        printf("Failed to init the barrier\n");
        return 1;
    }

    printf("Starting %d clients, each client will perform %d iterations\n", clients, iterations_per_client);

    pthread_t threads[clients];
    bzero(&threads, sizeof(pthread_t) * clients);

    for(int i = 0; i < clients; i++) {
        struct client_args* args = malloc(sizeof(struct client_args));
        args->tid = i;
        args->addr = addr;

        if(pthread_create(&threads[i], NULL, single_client, args)) {
            printf("Failed to create thread number %d\n", i);
            return 1;
        }
    }

    int ok = 0;
    int err = 0;
    int failed_cmp = 0;

    struct client_result* results[clients];
    bzero(&results, sizeof(struct client_result*) * clients);


    for(int i = 0; i < clients; i++) {
        pthread_join(threads[i], (void**)&results[i]);

        switch(results[i]->status) {
            case CLIENT_UNKNOWN:
                printf("ERROR: thread finished in unknown state\n");
                return 1;

            case CLIENT_OK:
                ok++;
                switch(ttype) {
                    case set_get:
                        results[i]->set_time = results[i]->set_time / iterations_per_client;
                        results[i]->get_time = results[i]->get_time / iterations_per_client;
                        break;
                    case ping_pong:
                        results[i]->ping_time = results[i]->ping_time / iterations_per_client;
                        break;
                    default:
                        printf("Unknown test type\n");
                        exit(1);
                }
                break;

            case CLIENT_ERR:
                err++;
                break;
                
            case CLIENT_FAILED_CMP:
                failed_cmp++;
                break;
        }
    }

    printf("All threads have finished!\n");
    printf("Outcome OK: %d\n", ok);
    printf("Outcome ERR: %d\n", err);
    printf("Outcome FAILED_CMP: %d\n", failed_cmp);

    switch(ttype) {
        case set_get: {
            unsigned long long int sum_of_mean_times_set = 0;
            unsigned long long int sum_of_mean_times_get = 0;

            for(int i = 0; i < clients; i++) {
                if(results[i]->set_time != 0 && results[i]->get_time != 0) {
                    sum_of_mean_times_set += results[i]->set_time;
                    sum_of_mean_times_get += results[i]->get_time;
                    printf("[%02d] mean time set: %llu\n", i, results[i]->set_time);
                    printf("[%02d] max time set: %llu\n", i, results[i]->max_set_time);
                    printf("[%02d] min time set: %llu\n", i, results[i]->min_set_time);
                    printf("[%02d] mean time get: %llu\n", i, results[i]->get_time);
                    printf("[%02d] max time get: %llu\n", i, results[i]->max_get_time);
                    printf("[%02d] min time get: %llu\n", i, results[i]->min_get_time);
                }
            }

            unsigned long long int final_mean_time_set = sum_of_mean_times_set / ok;
            unsigned long long int final_mean_time_get = sum_of_mean_times_get / ok;

            printf("Final mean time set: %llu\n", final_mean_time_set);
            printf("Final mean time get: %llu\n", final_mean_time_get);
            break;
        }
        case ping_pong: {
            unsigned long long int sum_of_mean_times_ping = 0;

            for(int i = 0; i < clients; i++) {
                if(results[i]->ping_time != 0) {
                    sum_of_mean_times_ping += results[i]->ping_time;
                    printf("[%02d] mean time ping: %llu\n", i, results[i]->ping_time);
                    printf("[%02d] max time ping: %llu\n", i, results[i]->max_ping_time);
                    printf("[%02d] min time ping: %llu\n", i, results[i]->min_ping_time);
                }
            }

            unsigned long long int final_mean_time_ping = sum_of_mean_times_ping / ok;

            printf("Final mean time ping: %llu\n", final_mean_time_ping);
            break;
        }
        default:
            printf("Unknown test type\n");
            exit(1);
    }

    pthread_barrier_destroy(&barrier);
    return 0;
}