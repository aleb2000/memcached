#include "mcrdma_client.h"
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include "../mcrdma_utils.h"

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
    client->buf = malloc(MCRDMA_BUF_SIZE);
    client->buf_size = MCRDMA_BUF_SIZE;
    if(!client->buf) {
        mcrdma_error("Failed to allocate rdma receive buffer");
        return -1;
    }

    client->pd = ibv_alloc_pd(client->id->verbs);
	if (!client->pd) {
		mcrdma_error("Failed to alloc pd");
		return -1;
	}

    // Register Memory Region
    client->buf_mr = ibv_reg_mr(client->pd, client->buf, client->buf_size, MCRDMA_BUF_ACCESS_FLAGS);
    if(!client->buf_mr) {
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

	if (ibv_req_notify_cq(client->cq, 0)) {
		mcrdma_error("Failed to request notifications");
		return -1;
	}

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

int mcrdma_client_ascii_send(struct mcrdma_client* client, char* buf, size_t buf_size) {
    if(buf_size > client->buf_size || buf_size == 0) {
        mcrdma_log("Invalid data buffer size\n");
        return -1;
    }

    // Copy data buffer and zero out the rest of it
    memcpy(client->buf, buf, buf_size);
    bzero(client->buf + buf_size, client->buf_size - buf_size);

    // This send could be made unsignaled
    if(rdma_post_send(client->id, NULL, client->buf, client->buf_size, client->buf_mr, IBV_SEND_SIGNALED)) {
        mcrdma_error("Failed posting send");
        return -1;
    }

    struct ibv_wc wc = {0};

    int num_wc = process_work_completion_events(client->comp_channel, &wc, 1);
    if(num_wc < 0) {
        mcrdma_log("Failed to process work completion events. Returned with value %d\n", num_wc);
        return -1;
    }

    mcrdma_log("Recieved %d WCs\n", num_wc);
    return 0;
}