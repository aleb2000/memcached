#include "bench_tcp.h"
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include "bench_utils.h"

int bench_tcp_init(union client_handle *handle, int tid, struct sockaddr_in addr) {
    handle->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(handle->sockfd == -1) {
        cprintf("Failed to create socket\n");
        return 1;
    }

    if(connect(handle->sockfd, (struct sockaddr*) &addr, sizeof(addr))) {
        cprintf("Failed to connect\n");
        return 1;
    }
    return 0;
}

static void work_set_get(struct client_result* res, int sockfd, int tid) {
    res->set_time = 0;
    res->max_set_time = 0;
    res->min_set_time = 0;
    res->get_time = 0;
    res->max_get_time = 0;
    res->min_get_time = 0;

    // Should be enough for anything
    size_t buf_size = 100 + get_payload_len();

    char* set_req = calloc(buf_size, 1);
    char* get_req = calloc(buf_size, 1);
    char* expected_get_resp = calloc(buf_size, 1);

    char* recv_buf = calloc(SAFE_BUF_SIZE, 1);

    for(int i = 0; i < iterations_per_client; i++) {
        //cprintf("Iter %d\n", i);
        // SET REQUEST
        int set_len = fill_set_key_value(set_req, tid, i);
        if(set_len < 0) {
            cprintf("Failed sprintf at iteration %d\n", i);
            res->status = CLIENT_ERR;
            return;
        }

        // GET REQUEST
        int get_len = fill_get_key_value(get_req, tid, i);
        if(get_len < 0) {
            cprintf("Failed sprintf at iteration %d\n", i);
            res->status = CLIENT_ERR;
            return;
        }

        // EXPECTED GET RESPONSE
        int expected_get_len = fill_expected_get_resp(expected_get_resp, tid, i);
        if(expected_get_len < 0) {
            cprintf("Failed sprintf at iteration %d\n", i);
            res->status = CLIENT_ERR;
            return;
        }

        struct timespec set_start, set_end, get_start, get_end;

        // Perform SET
        clock_gettime(CLOCK_MONOTONIC, &set_start);
        send(sockfd, set_req, set_len, 0);

        // SET response
        int resp_len = recv(sockfd, recv_buf, 65536, 0);
        clock_gettime(CLOCK_MONOTONIC, &set_end);

        // Verify SET
        CMP(recv_buf, "STORED\r\n", resp_len);

        // Perform GET
        clock_gettime(CLOCK_MONOTONIC, &get_start);
        send(sockfd, get_req, get_len, 0);

        // GET response
        resp_len = 0;
        while(resp_len < expected_get_len) {
            resp_len += recv(sockfd, recv_buf + resp_len, expected_get_len - resp_len, 0);
        }
        clock_gettime(CLOCK_MONOTONIC, &get_end);

        // Verify GET
        CMP(recv_buf, expected_get_resp, resp_len);

        // Sum time taken (with overflow check)
        unsigned long long int new_set_time = res->set_time;
        unsigned long long int new_get_time = res->get_time;
        unsigned long long int curr_set_time = ((set_end.tv_sec - set_start.tv_sec) * 1000000000ull) + (set_end.tv_nsec - set_start.tv_nsec);
        unsigned long long int curr_get_time = ((get_end.tv_sec - get_start.tv_sec) * 1000000000ull) + (get_end.tv_nsec - get_start.tv_nsec);
        new_set_time += curr_set_time;
        new_get_time += curr_get_time;

        assert(new_set_time > res->set_time);
        assert(new_get_time > res->get_time);

        res->set_time = new_set_time;
        res->get_time = new_get_time;

        // Update max times
        res->max_set_time = res->max_set_time < curr_set_time ? curr_set_time : res->max_set_time;
        res->max_get_time = res->max_get_time < curr_get_time ? curr_get_time : res->max_get_time;

        // Update min times
        if(res->min_set_time == 0) {
            res->min_set_time = curr_set_time;
        } else {
            res->min_set_time = res->min_set_time > curr_set_time ? curr_set_time : res->min_set_time;
        }
        if(res->min_get_time == 0) {
            res->min_get_time = curr_get_time;
        } else {
            res->min_get_time = res->min_get_time > curr_get_time ? curr_get_time : res->min_get_time;
        }
    }

    free(set_req);
    free(get_req);
    free(expected_get_resp);
    free(recv_buf);

    res->status = CLIENT_OK;
}

static void work_ping_pong(struct client_result* res, int sockfd, int tid) {
    res->ping_time = 0;
    res->max_ping_time = 0;
    res->min_ping_time = 0;

    char* msg = "PING\r\n";
    int msg_len = 6;
    char* recv_buf = calloc(SAFE_BUF_SIZE, 1);

    for(int i = 0; i < iterations_per_client; i++) {
        // Perform PING
        struct timespec ping_start, ping_end;

        clock_gettime(CLOCK_MONOTONIC, &ping_start);
        send(sockfd, msg, msg_len, 0);

        int resp_len = recv(sockfd, recv_buf, 65536, 0);
        clock_gettime(CLOCK_MONOTONIC, &ping_end);

        CMP(recv_buf, "PONG\r\n", resp_len);
        
        // Sum time taken (with overflow check)
        unsigned long long int new_ping_time = res->ping_time;
        unsigned long long int curr_ping_time = ((ping_end.tv_sec - ping_start.tv_sec) * 1000000000ull) + (ping_end.tv_nsec - ping_start.tv_nsec);
        new_ping_time += curr_ping_time;

        assert(new_ping_time > res->ping_time);

        res->ping_time = new_ping_time;

        // Update max times
        res->max_ping_time = res->max_ping_time < curr_ping_time ? curr_ping_time : res->max_ping_time;

        // Update min times
        if(res->min_ping_time == 0) {
            res->min_ping_time = curr_ping_time;
        } else {
            res->min_ping_time = res->min_ping_time > curr_ping_time ? curr_ping_time : res->min_ping_time;
        }
<<<<<<< HEAD
=======

        if(print_all) {
            cprintf("ping time: %llu\n", curr_ping_time);
        }
>>>>>>> be1279740cc73bf58cff91e542b80f47e2e2e34a
    }

    free(recv_buf);

    res->status = CLIENT_OK;
}


void bench_tcp_worker(struct client_result* res, union client_handle *handle, int tid) {
    switch(ttype) {
        case set_get:
            work_set_get(res, handle->sockfd, tid);
            break;
        case ping_pong:
            work_ping_pong(res, handle->sockfd, tid);
            break;
        default:
            cprintf("Unknown test type\n");
            exit(1);
    }
}