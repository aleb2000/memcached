#include "mcrdma.h"
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "mcrdma_utils.h"
#include <poll.h>

// State of dispatcher connection
static struct mcrdma_state disp;

int mcrdma_init(const char *interface, int port) {
    // safety check. With the appropriate measures could be removed to allow for rdma servers on multiple interfaces/ports
    static bool init = false;
    if(init) {
        mcrdma_log("rdma_init should only be called once");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in sockaddr = {0};
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
    
    mcrdma_log("rdma server will listen for connections on %s:%d\n", inet_ntoa(sockaddr.sin_addr), port);

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
        mcrdma_log("client establishing a connection\n");


        conn *c = mcrdma_conn_new(conn_new_cmd, rdma_transport);

        c->rdma->id = cm_event->id;

        if (rdma_ack_cm_event(cm_event)) {
		    mcrdma_error("Failed to acknowledge cm event\n");
		    return 0;
	    }

        pthread_t tid;
        pthread_create(&tid, NULL, mcrdma_worker, c);
        //sleep(1);
        //void* r;
        //pthread_join(tid, &r);
        //exit(1);

    }
    return res;
}

// Prepare a connection state and resources prior to accepting an incoming client
static int prepare_connection(struct mcrdma_state *s) {
    s->buf = malloc(MCRDMA_BUF_SIZE);
    s->buf_size = MCRDMA_BUF_SIZE;
    if(!s->buf) {
        mcrdma_error("Failed to allocate rdma receive buffer");
        return -1;
    }

    // Create a Protection Domain
    s->pd = ibv_alloc_pd(s->id->verbs);
	if (!s->pd) {
		mcrdma_error("Failed to allocate PD");
		return -1;
	}

    // Register Memory Region
    s->buf_mr = ibv_reg_mr(s->pd, s->buf, s->buf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!s->buf_mr) {
        mcrdma_error("Failed to register buffer memory region");
        return -1;
    }

    // Create Completion Queue
	s->comp_channel = ibv_create_comp_channel(s->id->verbs);
	if (!s->comp_channel) {
		mcrdma_error("Failed to create IO completion event channel");
	    return -1;
	}

    s->client_cq = ibv_create_cq(s->id->verbs, 16, NULL, s->comp_channel, 0);
	if (!s->client_cq) {
		mcrdma_error("Failed to create CQ");
		return -1;
	}

	if (ibv_req_notify_cq(s->client_cq, 0)) {
		mcrdma_error("Failed to request notifications");
		return -1;
	}

    // Create Queue Pair
    struct ibv_qp_init_attr qp_init_attr = {0};
    qp_init_attr.cap.max_recv_sge = 8;
    qp_init_attr.cap.max_send_sge = 8;
    qp_init_attr.cap.max_recv_wr = 16;
    qp_init_attr.cap.max_send_wr = 16;
    qp_init_attr.qp_type = IBV_QPT_RC;

    qp_init_attr.recv_cq = s->client_cq;
    qp_init_attr.send_cq = s->client_cq;

	if (rdma_create_qp(s->id, s->pd, &qp_init_attr)) {
		mcrdma_error("Failed to create QP");
	    return -1;
	}

    // Create new Event Channel and migrate
    struct rdma_event_channel *echannel = rdma_create_event_channel();
    if(!echannel) {
        mcrdma_error("Failed to create new event channel to migrate to");
        return -1;
    }

    if(rdma_migrate_id(s->id, echannel)) {
        mcrdma_error("Failed to migrate to new event channel");
        return -1;
    }

    s->echannel = echannel;

    return 0;
}

static void resources_destroy(struct mcrdma_state* s) {
    // TODO: cleanup
}

void *mcrdma_worker(void* arg) {
    mcrdma_log("Started worker thread.\n");
    conn *c = (conn*) arg;
    struct mcrdma_state *s = c->rdma;

    if(prepare_connection(s)) {
        mcrdma_log("Connection preparation failed\n");
        return (void*) -1;
    }

    struct rdma_conn_param conn_param = {0};
	conn_param.initiator_depth = 1;
	conn_param.responder_resources = 1;

    mcrdma_log("Accepting connection.\n");

    if(rdma_accept(s->id, &conn_param) == -1) {
        mcrdma_error("Cannot accept client");
        return (void*) -1;
    }

    struct rdma_cm_event *cm_event = NULL;
    if(mcrdma_process_event(s->echannel, 
            RDMA_CM_EVENT_ESTABLISHED, &cm_event)) {
        mcrdma_error("Failed to process cm event");
        return (void*) -1;
    }

    mcrdma_log("Connection established!\n");

    struct ibv_wc wc = {0};

    while(1) {

    if(rdma_post_recv(s->id, NULL, s->buf, s->buf_size, s->buf_mr)) {
        mcrdma_error("Failed to post recv");
        return (void*) -1;
    }
    
    int num_wc = process_work_completion_events(s->comp_channel, &wc, 1);

    mcrdma_log("Received %d WCs\n", num_wc);

    mcrdma_log("buf content: %s\n", s->buf);

    }

    mcrdma_conn_destroy(c);
    return NULL;
}

conn *mcrdma_conn_new(enum conn_states init_state, enum network_transport transport) {
    conn *c = calloc(1, sizeof(conn));
    if(!c) {
        STATS_LOCK();
        stats.malloc_fails++;
        STATS_UNLOCK();
        mcrdma_log("Failed to allocate connection object\n");
        return NULL;
    }
    MEMCACHED_CONN_CREATE(c);
    
    STATS_LOCK();
    stats_state.conn_structs++;
    STATS_UNLOCK();

    c->transport = transport;
    c->protocol = rdma_prot;

    if (settings.verbose > 1) {
        mcrdma_log("<new rdma client connection.\n")
    }

    c->state = init_state;
    c->cmd = -1;
    c->last_cmd_time = current_time;
    
    c->authenticated = true;

    c->rdma = malloc(sizeof(struct mcrdma_state));

    STATS_LOCK();
    stats_state.curr_conns++;
    stats.total_conns++;
    STATS_UNLOCK();

    MEMCACHED_CONN_ALLOCATE(c->sfd);

    return c;
}

void mcrdma_conn_destroy(conn* c) {
    if (c) {
        MEMCACHED_CONN_DESTROY(c);
        //if (c->rbuf)
        //    free(c->rbuf);
        if(c->rdma) {
            resources_destroy(c->rdma);
            free(c->rdma);
        }

        free(c);
    }
}

void mcrdma_destroy() {
    rdma_destroy_event_channel(disp.echannel);
    rdma_destroy_id(disp.id);
}