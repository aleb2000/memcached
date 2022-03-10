#include "mcrdma.h"
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "mcrdma_utils.h"
#include <poll.h>

struct mcrdma_state {
    struct rdma_event_channel *echannel;
    struct rdma_cm_id *id;
};

// State of dispatcher connection
static struct mcrdma_state disp;

int mcrdma_init(const char *interface, int port) {
    // safety check. With the appropriate measures could be removed to allow for rdma servers on multiple interfaces/ports
    static bool init = false;
    if(init) {
        mcrdma_log("rdma_init should only be called once");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(struct sockaddr_in));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);

    // Select the interface
    if(interface) {
        if(!inet_pton(AF_INET, interface, &sockaddr.sin_addr)) {
            mcrdma_log("Invalid interface address: %s\n", interface);
            return -1;
        }
    } else {
        sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    
    disp.echannel = rdma_create_event_channel();
    if(!disp.echannel) {
        mcrdma_error("cm channel creation error");
        return -1;
    }

    // Set echannel to non-blocking
    /*int opt = fcntl(disp.echannel->fd, F_GETFL);
    if (opt < 0) {
        fprintf(stderr, "can't get fd flags");
    }
    if (fcntl(disp.echannel->fd, F_SETFL, opt | O_NONBLOCK) < 0) {
        fprintf(stderr, "can't set non-blocking fd flag");
    }*/

    if(rdma_create_id(disp.echannel, &disp.id, NULL, RDMA_PS_TCP)) {
        mcrdma_error("rdma id creationg error");
        return -1;
    }

    if(rdma_bind_addr(disp.id, (struct sockaddr*) &sockaddr)) {
        mcrdma_error("cannot bind address");
        return -1;
    }

    if(rdma_listen(disp.id, MCRDMA_BACKLOG)) {
        mcrdma_error("rdma_listen failed");
        return -1;
    }
    
    mcrdma_log("rdma server will listen for connections on %s:%d\n", inet_ntoa(sockaddr.sin_addr), ntohs(port));

    
    return 0;
}

int mcrdma_listen() {
    struct pollfd pfd;
    pfd.fd = disp.echannel->fd;
    pfd.events = POLLIN;

    int res = poll(&pfd, 1, MCRDMA_DISPATCHER_POLL_TIMEOUT_MS);
    if(res > 0) {
        struct rdma_cm_event *cm_event = NULL;
        if(mcrdma_process_event(disp.echannel, 
                RDMA_CM_EVENT_CONNECT_REQUEST, &cm_event)) {
            mcrdma_error("failed to process cm event");
            return -errno;
        }

        mcrdma_log("Got a cm event!\n");
    }
    return res;
}

void mcrdma_destroy() {
    rdma_destroy_event_channel(disp.echannel);
    rdma_destroy_id(disp.id);
}