#include "mcrdma_client.h"
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include "mcrdma_client_utils.h"

int mcrdma_client_init(struct mcrdma_client *client) {
    client->echannel = rdma_create_event_channel();
    if(!client->echannel) {
        mcrdma_error("CM channel creation error");
        return -1;
    }

    if(rdma_create_id(client->echannel, &client->id, NULL, RDMA_PS_TCP)) {
        mcrdma_error("RDMA id creation error");
        return -1;
    }

    if (rdma_resolve_addr(client->id, NULL, (struct sockaddr*) &client->addr, 2000)) {
		mcrdma_error("Failed to resolve address");
		return -1;
	}

    struct rdma_cm_event *cm_event = NULL;

    if(mcrdma_process_event(client->echannel, 
			RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event)) {
        mcrdma_error("Failed to process cm event");
        return -1;
    }

    if(rdma_ack_cm_event(cm_event)) {
        mcrdma_error("Failed to ack cm event");
        return -1;
    }

	if (rdma_resolve_route(client->id, 2000)) {
        mcrdma_error("Failed to resolve route");
	    return -1;
    }

    if(mcrdma_process_event(client->echannel, 
			RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event)) {
        mcrdma_error("Failed to process cm event");
        return -1;
    }

    if(rdma_ack_cm_event(cm_event)) {
        mcrdma_error("Failed to ack cm event");
        return -1;
    }
    
    return 0;
}

int mcrdma_client_alloc_resources(struct mcrdma_client* client) {
    client->pd = ibv_alloc_pd(client->id->verbs);
	if (!client->pd) {
		mcrdma_error("Failed to alloc pd");
		return -1;
	}

    // Allocate and register recv buffer
    client->rbuf = malloc(MCRDMA_BUF_SIZE);
    if(!client->rbuf) {
        mcrdma_error("Failed to allocate rdma receive buffer");
        return -1;
    }
    client->rbuf_size = MCRDMA_BUF_SIZE;

    client->rbuf_mr = ibv_reg_mr(client->pd, client->rbuf, client->rbuf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!client->rbuf_mr) {
        mcrdma_error("Failed to register buffer memory region");
        return -1;
    }
    client->rbuf_posted = false;

    // Allocate and register send buffer
    client->sbuf = malloc(MCRDMA_BUF_SIZE);
    if(!client->sbuf) {
        mcrdma_error("Failed to allocate rdma receive buffer");
        return -1;
    }
    client->sbuf_size = MCRDMA_BUF_SIZE;

    client->sbuf_mr = ibv_reg_mr(client->pd, client->sbuf, client->sbuf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!client->sbuf_mr) {
        mcrdma_error("Failed to register buffer memory region");
        return -1;
    }

	client->comp_channel = ibv_create_comp_channel(client->id->verbs);
	if (!client->comp_channel) {
		mcrdma_error("Failed to create IO completion event channel");
	    return -1;
	}

    client->cq = ibv_create_cq(client->id->verbs, 
			CQ_CAPACITY, 
			NULL /* user context, not used here */,
			client->comp_channel, 
			0 /* signaling vector, not used here*/);
	if (!client->cq) {
		mcrdma_error("Failed to create CQ");
		return -1;
	}

	/*if (ibv_req_notify_cq(client->cq, 0)) {
		mcrdma_error("Failed to request notifications");
		return -1;
	}*/

    static struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof qp_init_attr);
    qp_init_attr.cap.max_recv_sge = MAX_SGE;
    qp_init_attr.cap.max_send_sge = MAX_SGE;
    qp_init_attr.cap.max_recv_wr = MAX_WR;
    qp_init_attr.cap.max_send_wr = MAX_WR;
    qp_init_attr.qp_type = IBV_QPT_RC;

    qp_init_attr.recv_cq = client->cq;
    qp_init_attr.send_cq = client->cq;

	if (rdma_create_qp(client->id,
            client->pd,
            &qp_init_attr)) {
		mcrdma_error("Failed to create QP");
	       return -1;
	}

    return 0;
}

int mcrdma_client_connect(struct mcrdma_client* client) {
    struct rdma_conn_param conn_param;

	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3; // if fail, then how many times to retry

    if (rdma_connect(client->id, &conn_param)) {
		mcrdma_error("Failed to connect to remote host");
		return -1;
	}

    struct rdma_cm_event *cm_event;

    if(mcrdma_process_event(client->echannel, 
			RDMA_CM_EVENT_ESTABLISHED, &cm_event)) {
        mcrdma_error("Failed to process cm event");
        return -1;
    }

    if(rdma_ack_cm_event(cm_event)) {
        mcrdma_error("Failed to ack cm event");
        return -1;
    }

    return 0;
}

int mcrdma_client_disconnect(struct mcrdma_client* client) {
    if(rdma_disconnect(client->id)) {
        mcrdma_error("Failed to disconnect from remote host");
		return -1;
    }

    struct rdma_cm_event *cm_event;

    if(mcrdma_process_event(client->echannel, 
			RDMA_CM_EVENT_DISCONNECTED, &cm_event)) {
        mcrdma_error("Failed to process cm event");
        return -1;
    }

    if(rdma_ack_cm_event(cm_event)) {
        mcrdma_error("Failed to ack cm event");
        return -1;
    }

    return 0;
}

static int post_rbuf(struct mcrdma_client* client) {
    if(!client->rbuf_posted) {
        if(rdma_post_recv(client->id, NULL, client->rbuf, client->rbuf_size, client->rbuf_mr)) {
            printf("post recv FAILED\n");
            return -1;
        }
    }
    return 0;
}

int mcrdma_client_ascii_send(struct mcrdma_client* client, size_t len) {
    // Pre-post rbuf
    if(post_rbuf(client)) {
        mcrdma_error("Failed to pre-post rbuf");
        return -1;
    }

    client->rbuf_posted = true;

    // This send could be made unsignaled
    if(rdma_post_send(client->id, NULL, client->sbuf, len, client->sbuf_mr, IBV_SEND_SIGNALED)) {
        mcrdma_error("Failed posting send");
        return -1;
    }

    struct ibv_wc wc = {0};

    //int num_wc = process_work_completion_events(client->comp_channel, &wc, 1);
    int num_wc = poll_cq_for_wc(client->cq, &wc, 1);
    if(num_wc < 0) {
        mcrdma_log("Failed to process work completion events. Returned with value %d\n", num_wc);
        return -1;
    }

    mcrdma_log("Received %d WCs\n", num_wc);
    return 0;
}

int mcrdma_client_ascii_send_buf(struct mcrdma_client* client, char* buf, size_t buf_size) {
    if(buf_size > client->sbuf_size || buf_size == 0) {
        mcrdma_log("Invalid data buffer size\n");
        return -1;
    }

    // Copy data buffer
    memcpy(client->sbuf, buf, buf_size);

    return mcrdma_client_ascii_send(client, buf_size);
}

int mcrdma_client_ascii_recv(struct mcrdma_client* client) {
    mcrdma_log("started receive\n");
    struct ibv_wc wc = {0};
    if(post_rbuf(client)) {
        mcrdma_error("Failed to post recv");
        return -1;
    }
    
    //process_work_completion_events(client->comp_channel, &wc, 1);
    int num_wc = poll_cq_for_wc(client->cq, &wc, 1);
    if(num_wc < 0) {
        mcrdma_log("Failed to process work completion events. Returned with value %d\n", num_wc);
        return -1;
    }


    client->rbuf_posted = false;
    return wc.byte_len;
}