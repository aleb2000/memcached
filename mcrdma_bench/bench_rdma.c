#include "bench_rdma.h"
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include "bench_utils.h"
#include <unistd.h>

int bench_rdma_init(union client_handle *handle, int tid, struct sockaddr_in addr) {
    handle->client.addr = addr;
    if(mcrdma_client_init(&handle->client)) {
        cprintf("Cannot initialize client\n");
        return 1;
    }

    cprintf("Client initialized\n");

    if(mcrdma_client_alloc_resources(&handle->client)) {
        cprintf("Cannot allocate resources\n");
        return 1;
    }

    cprintf("Resources allocated\n");

    if(mcrdma_client_connect(&handle->client)) {
        cprintf("Cannot connect to server\n");
        return 1;
    }

    return 0;
}

void bench_rdma_destroy(union client_handle *handle, int tid) {
    //TODO: destroy resources
    if(mcrdma_client_disconnect(&handle->client)) {
        cprintf("Cannot disconnect from server\n");
    }
}

static void work_set_get(struct client_result* res, struct mcrdma_client* client, int tid) {
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
        mcrdma_client_ascii_send_buf(client, set_req, set_len);

        // SET response
        int resp_len = mcrdma_client_ascii_recv(client);
        clock_gettime(CLOCK_MONOTONIC, &set_end);

        // Verify SET
        CMP(client->rbuf, "STORED\r\n", resp_len);

        // Perform GET
        clock_gettime(CLOCK_MONOTONIC, &get_start);
        mcrdma_client_ascii_send_buf(client, get_req, get_len);

        // GET response
        resp_len = mcrdma_client_ascii_recv(client);
        clock_gettime(CLOCK_MONOTONIC, &get_end);

        // Verify GET
        CMP(client->rbuf, expected_get_resp, resp_len);

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
<<<<<<< HEAD
=======

        if(print_all) {
            cprintf("set time: %llu\n", curr_set_time);
            cprintf("get time: %llu\n", curr_get_time);
        }
>>>>>>> be1279740cc73bf58cff91e542b80f47e2e2e34a
    }

    free(set_req);
    free(get_req);
    free(expected_get_resp);

    res->status = CLIENT_OK;
}

static void work_ping_pong(struct client_result* res, struct mcrdma_client* client, int tid) {
    res->ping_time = 0;
    res->max_ping_time = 0;
    res->min_ping_time = 0;

    char* msg = "PING\r\n";
    int msg_len = 6;
    for(int i = 0; i < iterations_per_client; i++) {
        
        // Perform PING
        struct timespec ping_start, ping_end;

        clock_gettime(CLOCK_MONOTONIC, &ping_start);
        mcrdma_client_ascii_send_buf(client, msg, msg_len);

        int resp_len = mcrdma_client_ascii_recv(client);
        clock_gettime(CLOCK_MONOTONIC, &ping_end);

        CMP(client->rbuf, "PONG\r\n", resp_len);
        
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
    }
    res->status = CLIENT_OK;
}

void bench_rdma_worker(struct client_result* res, union client_handle *handle, int tid) {
    switch(ttype) {
        case set_get:
            work_set_get(res, &handle->client, tid);
            break;
        case ping_pong:
            work_ping_pong(res, &handle->client, tid);
            break;
        default:
            cprintf("Unknown test type\n");
            exit(1);
    }
}