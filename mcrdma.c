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
#include "proto_text.h"
#include <fcntl.h>

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
    
    fprintf(stderr, "rdma server will listen for connections on %s:%d\n", inet_ntoa(sockaddr.sin_addr), port);

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


        conn *c = mcrdma_conn_new(conn_read, MCRDMA_BUF_SIZE);
        c->thread = calloc(1, sizeof(LIBEVENT_THREAD));
        mcrdma_libevent_thread_init(c->thread);
        c->rdma->id = cm_event->id;

        if (rdma_ack_cm_event(cm_event)) {
		    mcrdma_error("Failed to acknowledge cm event\n");
		    return 0;
	    }

        pthread_create(&c->thread->thread_id, NULL, mcrdma_worker, c);
    }
    return res;
}

// Prepare a connection state and resources prior to accepting an incoming client
static int prepare_connection(conn* c) {
    struct mcrdma_state* s = c->rdma;
    s->pinged = false;

    // Create a Protection Domain
    s->pd = ibv_alloc_pd(s->id->verbs);
	if (!s->pd) {
		mcrdma_error("Failed to allocate PD");
		return -1;
	}

    // Allocate send buffer
    s->sbuf = malloc(MCRDMA_BUF_SIZE);
    if(!s->sbuf) {
        mcrdma_error("Failed to allocate rdma send buffer");
        return -1;
    }
    s->sbuf_size = MCRDMA_BUF_SIZE;
    s->sbuf_mr = ibv_reg_mr(s->pd, s->sbuf, s->sbuf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!s->sbuf_mr) {
        mcrdma_error("Failed to register send buffer memory region");
        return -1;
    }

    // Allocate receive buffer
    s->rbuf_posted = false;
    s->rbuf_size = MCRDMA_BUF_SIZE;
    s->rbuf = malloc(MCRDMA_BUF_SIZE);
    if(!s->rbuf) {
        mcrdma_error("Failed to allocate rdma receive buffer");
        return -1;
    }
    s->rbuf_mr = ibv_reg_mr(s->pd, s->rbuf, s->rbuf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!s->rbuf_mr) {
        mcrdma_error("Failed to register receive buffer memory region");
        return -1;
    }

    // Create Completion Queue
	s->comp_channel = ibv_create_comp_channel(s->id->verbs);
	if (!s->comp_channel) {
		mcrdma_error("Failed to create IO completion event channel");
	    return -1;
	}
    
    int flags = fcntl(s->comp_channel->fd, F_GETFL, 0);
    fcntl(s->comp_channel->fd, F_SETFL, flags | O_NONBLOCK);

    s->client_cq = ibv_create_cq(s->id->verbs, 16, NULL, s->comp_channel, 0);
	if (!s->client_cq) {
		mcrdma_error("Failed to create CQ");
		return -1;
	}

	// if (ibv_req_notify_cq(s->client_cq, 0)) {
	// 	mcrdma_error("Failed to request notifications");
	// 	return -1;
	// }

    // Create Queue Pair
    struct ibv_qp_init_attr qp_init_attr = {0};
    qp_init_attr.cap.max_recv_sge = 16;
    qp_init_attr.cap.max_send_sge = 16;
    qp_init_attr.cap.max_recv_wr = 32;
    qp_init_attr.cap.max_send_wr = 32;
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
    flags = fcntl(echannel->fd, F_GETFL, 0);
    fcntl(echannel->fd, F_SETFL, flags | O_NONBLOCK);


    if(rdma_migrate_id(s->id, echannel)) {
        mcrdma_error("Failed to migrate to new event channel");
        return -1;
    }

    s->echannel = echannel;


    return 0;
}

static void resources_destroy(struct mcrdma_state* s) {
    mcrdma_log("Destroying resources\n");
    rdma_destroy_qp(s->id);
    ibv_destroy_cq(s->client_cq);
    ibv_destroy_comp_channel(s->comp_channel);
    ibv_dereg_mr(s->rbuf_mr);
    free(s->rbuf);
    ibv_dereg_mr(s->sbuf_mr);
    free(s->sbuf);
    ibv_dealloc_pd(s->pd);
    rdma_destroy_event_channel(s->echannel);
    rdma_destroy_id(s->id);
}

static inline int post_rbuf(conn *c, size_t count) {
    return rdma_post_recv(c->rdma->id, NULL, c->rdma->rbuf, count, c->rdma->rbuf_mr);
}

void *mcrdma_worker(void* arg) {
    mcrdma_log("Started worker thread.\n");
    conn *c = (conn*) arg;
    struct mcrdma_state *s = c->rdma;

    // Final LIBEVENT_THREAD initialization
    c->thread->l = logger_create();
    c->thread->lru_bump_buf = item_lru_bump_buf_create();
    if (c->thread->l == NULL || c->thread->lru_bump_buf == NULL) {
        abort();
    }

    if(prepare_connection(c)) {
        mcrdma_log("Connection preparation failed\n");
        return (void*) -1;
    }

    mcrdma_log("Pre-posting recv buffer\n");
    if(post_rbuf(c, s->rbuf_size)) {
        mcrdma_error("Failed to pre-post recv buffer");
        return (void*) -1;
    }
    s->rbuf_posted = true;

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

    mcrdma_state_machine(c);

    mcrdma_conn_destroy(c);
    return NULL;
}

// This is a modified version of the drive_machine() function in memcached.c
void mcrdma_state_machine(conn* c) {
    //struct mcrdma_state* s = c->rdma;
    bool stop = false;
    //struct ibv_wc wc = {0};
    ssize_t res = 0;


    while(!stop) {
        mcrdma_log("state: %s\n", state_text(c->state));

        switch(c->state) {
            // States ignored: conn_listening

            case conn_waiting:
                conn_set_state(c, conn_read);
                //break;

            case conn_read:

                res = try_read_network(c);

                switch (res) {
                    case READ_NO_DATA_RECEIVED:
                        conn_set_state(c, conn_read);
                        break;
                    case READ_DATA_RECEIVED:
                        conn_set_state(c, conn_parse_cmd);
                        break;
                    case READ_ERROR:
                        conn_set_state(c, conn_closing);
                        break;
                    case READ_MEMORY_ERROR: /* Failed to allocate more memory */
                        /* State already set by try_read_network */
                        break;
                }
                break;

            case conn_parse_cmd:
                c->noreply = false;
                // Process the command
                if (c->try_read_command(c) == 0) {
                    // Not enough data!
                    mcrdma_log("Not enough data!\n");
                    if (c->resp_head) {
                        mcrdma_log("Buffered responses waiting\n");
                        conn_set_state(c, conn_mwrite);
                    } else {
                        mcrdma_log("Go read more\n");
                        conn_set_state(c, conn_read);
                    }
                }
                break;

            case conn_new_cmd:
                // We don't care about starving other connections or interfacing with Libevent,
                // we never yield
                reset_cmd_handler(c);
                break;
                
            case conn_nread:
                if (c->rlbytes == 0) {
                    complete_nread(c);
                    break;
                }

                /* Check if rbytes < 0, to prevent crash */
                if (c->rlbytes < 0) {
                    if (settings.verbose) {
                        fprintf(stderr, "Invalid rlbytes to read: len %d\n", c->rlbytes);
                    }
                    conn_set_state(c, conn_closing);
                    break;
                }

                if (c->item_malloced || ((((item *)c->item)->it_flags & ITEM_CHUNKED) == 0) ) {
                    /* first check if we have leftovers in the conn_read buffer */
                    if (c->rbytes > 0) {
                        int tocopy = c->rbytes > c->rlbytes ? c->rlbytes : c->rbytes;
                        memmove(c->ritem, c->rcurr, tocopy);
                        c->ritem += tocopy;
                        c->rlbytes -= tocopy;
                        c->rcurr += tocopy;
                        c->rbytes -= tocopy;
                        if (c->rlbytes == 0) {
                            break;
                        }
                    }

                    /*  now try reading from the socket */
                    res = c->read(c, c->ritem, c->rlbytes);
                    if (res > 0) {
                        pthread_mutex_lock(&c->thread->stats.mutex);
                        c->thread->stats.bytes_read += res;
                        pthread_mutex_unlock(&c->thread->stats.mutex);
                        if (c->rcurr == c->ritem) {
                            c->rcurr += res;
                        }
                        c->ritem += res;
                        c->rlbytes -= res;
                        break;
                    }
                } else {
                    res = read_into_chunked_item(c);
                    if (res > 0)
                        break;
                }

                if (res == 0) { /* end of stream */
                    c->close_reason = NORMAL_CLOSE;
                    conn_set_state(c, conn_closing);
                    break;
                }

                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    conn_set_state(c, conn_closing);
                    break;
                }

                /* Memory allocation failure */
                if (res == -2) {
                    out_of_memory(c, "SERVER_ERROR Out of memory during read");
                    c->sbytes = c->rlbytes;
                    conn_set_state(c, conn_swallow);
                    // Ensure this flag gets cleared. It gets killed on conn_new()
                    // so any conn_closing is fine, calling complete_nread is
                    // fine. This swallow semms to be the only other case.
                    c->set_stale = false;
                    c->mset_res = false;
                    break;
                }
                /* otherwise we have a real error, on which we close the connection */
                if (settings.verbose > 0) {
                    fprintf(stderr, "Failed to read, and not due to blocking:\n"
                            "errno: %d %s \n"
                            "rcurr=%p ritem=%p rbuf=%p rlbytes=%d rsize=%d\n",
                            errno, strerror(errno),
                            (void *)c->rcurr, (void *)c->ritem, (void *)c->rbuf,
                            (int)c->rlbytes, (int)c->rsize);
                }
                conn_set_state(c, conn_closing);
                break;

            case conn_swallow:
                /* we are reading sbytes and throwing them away */
                if (c->sbytes <= 0) {
                    conn_set_state(c, conn_new_cmd);
                    break;
                }

                /* first check if we have leftovers in the conn_read buffer */
                if (c->rbytes > 0) {
                    int tocopy = c->rbytes > c->sbytes ? c->sbytes : c->rbytes;
                    c->sbytes -= tocopy;
                    c->rcurr += tocopy;
                    c->rbytes -= tocopy;
                    break;
                }

                /*  now try reading from the socket */
                res = c->read(c, c->rbuf, c->rsize > c->sbytes ? c->sbytes : c->rsize);
                if (res > 0) {
                    pthread_mutex_lock(&c->thread->stats.mutex);
                    c->thread->stats.bytes_read += res;
                    pthread_mutex_unlock(&c->thread->stats.mutex);
                    c->sbytes -= res;
                    break;
                }
                if (res == 0) { /* end of stream */
                    c->close_reason = NORMAL_CLOSE;
                    conn_set_state(c, conn_closing);
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    conn_set_state(c, conn_closing);
                    break;
                }
                /* otherwise we have a real error, on which we close the connection */
                if (settings.verbose > 0)
                    fprintf(stderr, "Failed to read, and not due to blocking\n");
                conn_set_state(c, conn_closing);
                break;

            case conn_write:
            case conn_mwrite:
                /* have side IO's that must process before transmit() can run.
                * remove the connection from the worker thread and dispatch the
                * IO queue
                */
                assert(c->io_queues_submitted == 0);

                for (io_queue_t *q = c->io_queues; q->type != IO_QUEUE_NONE; q++) {
                    if (q->stack_ctx != NULL) {
                        io_queue_cb_t *qcb = thread_io_queue_get(c->thread, q->type);
                        qcb->submit_cb(q);
                        c->io_queues_submitted++;
                    }
                }
                if (c->io_queues_submitted != 0) {
                    conn_set_state(c, conn_io_queue);
                    stop = true;
                    break;
                }

                switch (transmit(c)) {
                    case TRANSMIT_COMPLETE:
                        if (c->state == conn_mwrite) {
                            // Free up IO wraps and any half-uploaded items.
                            conn_release_items(c);
                            conn_set_state(c, conn_new_cmd);
                            if (c->close_after_write) {
                                conn_set_state(c, conn_closing);
                            }
                        } else {
                            if (settings.verbose > 0)
                                fprintf(stderr, "Unexpected state %d\n", c->state);
                            conn_set_state(c, conn_closing);
                        }
                        break;

                    case TRANSMIT_INCOMPLETE:
                    case TRANSMIT_HARD_ERROR:
                        break;                   /* Continue in state machine. */

                    case TRANSMIT_SOFT_ERROR:
                        stop = true;
                        break;
                }
                break;

            case conn_closing:
                mcrdma_log("CLOSING\n");
                conn_set_state(c, conn_closed);
                stop = true;
                break;

            case conn_closed:
                /* This only happens if dormando is an idiot. */
                abort();
                break;

            case conn_watch:
                /* We handed off our connection to the logger thread. */
                stop = true;
                break;
            case conn_io_queue:
                /* Complete our queued IO's from within the worker thread. */
                conn_io_queue_complete(c);
                conn_set_state(c, conn_mwrite);
                break;
            case conn_max_state:
                assert(false);
                break;

            default:
                mcrdma_log("Unhandled case\n");
                assert(false);
        }
    }
}

static ssize_t mcrdma_read(conn *c, void *buf, size_t count) {
    assert (c != NULL);
    struct ibv_wc wc = {0};
    if(!c->rdma->rbuf_posted) {
        if(post_rbuf(c, count)) {
            mcrdma_error("Failed to post non pre-posted recv\n");
            return -1;
        }
    }
    
    if(poll_wce(c->rdma, &wc, 1) == -1) {
        return 0;
    }
    

    // Check for PING PONG
    if(c->rdma->rbuf[0] == 'P') {
        // Pre-post for next request
        if(post_rbuf(c, count)) {
            mcrdma_error("Failed to pre-post recv\n");
            c->rdma->rbuf_posted = false;
            return -1;
        }

        struct iovec iov[1];
        iov[0].iov_base = "PONG\r\n";
        iov[0].iov_len = 6;
        struct msghdr msg;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;
        c->sendmsg(c, &msg, 0);

        c->rdma->pinged = true;

        return wc.byte_len;
    }

    // Put read data in destination
    memcpy(buf, c->rdma->rbuf, wc.byte_len);
    
    // Pre-post for next request
    if(post_rbuf(c, count)) {
        mcrdma_error("Failed to pre-post recv\n");
        c->rdma->rbuf_posted = false;
        return -1;
    }

    mcrdma_log("Received %d bytes\n", wc.byte_len);

    return wc.byte_len;
}

static ssize_t mcrdma_sendmsg(conn *c, struct msghdr *msg, int flags) {
    ssize_t len = 0;
    for(int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec iov = msg->msg_iov[i];
        int off = len;
        len += iov.iov_len;


        if(len > c->rdma->sbuf_size) {
            fprintf(stderr, "1\n");
            mcrdma_log("Scatter-gather sendmsg does not fit the rdma send buffer!\n");
            return -1;
        }

        memcpy(c->rdma->sbuf + off, iov.iov_base, iov.iov_len);

        mcrdma_log("out: %.*s", (int)iov.iov_len, (char*)iov.iov_base);
    }
    
    if(rdma_post_send(c->rdma->id, NULL, c->rdma->sbuf, len, c->rdma->sbuf_mr, IBV_SEND_SIGNALED)) {
        fprintf(stderr, "2\n");
        mcrdma_error("Failed posting send");
        return -1;
    }

    struct ibv_wc wc = {0};

    int num_wc = poll_wce(c->rdma, &wc, 1);
    if(num_wc < 0) {
        fprintf(stderr, "3\n");
        mcrdma_error("Failed to process work completion events. Returned with value %d", num_wc);
        return -1;
    }

    mcrdma_log("Recieved %d WCs\n", num_wc);

    return len;
}


conn *mcrdma_conn_new(enum conn_states init_state, const int read_buffer_size) {
    conn *c = calloc(1, sizeof(conn));
    if(!c) {
        STATS_LOCK();
        stats.malloc_fails++;
        STATS_UNLOCK();
        mcrdma_log("Failed to allocate connection object\n");
        return NULL;
    }
    MEMCACHED_CONN_CREATE(c);

    c->rsize = read_buffer_size;
    c->rbytes = 0;

    if (c->rsize) {
        c->rbuf = (char *)malloc((size_t)c->rsize);
    }

    if (c->rsize && c->rbuf == NULL) {
        mcrdma_conn_destroy(c);
        STATS_LOCK();
        stats.malloc_fails++;
        STATS_UNLOCK();
        fprintf(stderr, "Failed to allocate buffers for connection\n");
        return NULL;
    }

    
    STATS_LOCK();
    stats_state.conn_structs++;
    STATS_UNLOCK();

    c->transport = rdma_transport;
    c->protocol = ascii_prot;

    if (settings.verbose > 1) {
        mcrdma_log("<new rdma client connection.\n")
    }

    c->read = mcrdma_read;
    c->sendmsg = mcrdma_sendmsg;

    c->state = init_state;
    c->cmd = -1;
    c->last_cmd_time = current_time;
    
    c->authenticated = true;
    c->try_read_command = try_read_command_ascii;

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
        if (c->rbuf)
            free(c->rbuf);
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