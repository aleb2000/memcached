#include <stdio.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <assert.h>
#include <arpa/inet.h>
#include "../rdma_common.h"

int main(int argc, char** argv) {
    if(argc != 3) {
        printf("Usage: rdma_client HOST PORT\n");
        return 0;
    }

    char* host = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(struct sockaddr_in));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);

    if(!inet_pton(AF_INET, host, &sockaddr.sin_addr)) {
        fprintf(stderr, "Invalid host address: %s\n", host);
        return 1;
    }

    struct rdma_cm_event *cm_event = NULL;

	struct rdma_event_channel *event_channel = rdma_create_event_channel();
	if (!event_channel) {
		fprintf(stderr, "cm channel creation error (%d)\n", errno);
		return 1;
	}

	struct rdma_cm_id *id;
    if(rdma_create_id(event_channel, &id, NULL, RDMA_PS_TCP)) {
        fprintf(stderr, "rdma id creationg error (%d)\n", errno);
        return 1;
    }
	

	if (rdma_resolve_addr(id, NULL, (struct sockaddr*) &sockaddr, 2000)) {
		rdma_error("failed to resolve address (%d)\n", -errno);
		return 1;
	}

    fprintf(stderr, "awaiting address resolution event...\n");
    if(process_rdma_cm_event(event_channel, 
			RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event)) {
        fprintf(stderr, "failed to process cm event (%d)\n", errno);
        return 1;
    }

    if(rdma_ack_cm_event(cm_event)) {
        fprintf(stderr, "failed to ack cm event (%d)\n", errno);
        return 1;
    }

    fprintf(stderr, "...resolved RDMA address\n");

	if (rdma_resolve_route(id, 2000)) {
		rdma_error("failed to resolve route (%d) \n", -errno);
	       return 1;
	}

    fprintf(stderr, "awaiting route resolution event...\n");
    if(process_rdma_cm_event(event_channel, 
			RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event)) {
        fprintf(stderr, "failed to process cm event (%d)\n", errno);
        return 1;
    }
    
    if(rdma_ack_cm_event(cm_event)) {
        fprintf(stderr, "failed to ack cm event (%d)\n", errno);
        return 1;
    }

    fprintf(stderr, "...route resolved\n");
    
    fprintf(stderr, "trying to connect at %s:%d\n", inet_ntoa(sockaddr.sin_addr), ntohs(port));


    struct ibv_pd *pd = ibv_alloc_pd(id->verbs);
	if (!pd) {
		fprintf(stderr, "failed to alloc pd (%d) \n", errno);
		return 1;
	}

	struct ibv_comp_channel *io_completion_channel = ibv_create_comp_channel(id->verbs);
	if (!io_completion_channel) {
		fprintf(stderr, "failed to create IO completion event channel (%d)\n", errno);
	    return 1;
	}

    struct ibv_cq *client_cq = ibv_create_cq(id->verbs /* which device*/, 
			CQ_CAPACITY /* maximum capacity*/, 
			NULL /* user context, not used here */,
			io_completion_channel /* which IO completion channel */, 
			0 /* signaling vector, not used here*/);
	if (!client_cq) {
		fprintf(stderr, "failed to create CQ (%d)\n", errno);
		return 1;
	}

	if (ibv_req_notify_cq(client_cq, 0)) {
		fprintf(stderr, "failed to request notifications, errno: %d\n", errno);
		return 1;
	}

    static struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof qp_init_attr);
    qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
    /* We use same completion queue, but one can use different queues */
    qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
    qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
	if (rdma_create_qp(id /* which connection id */,
            pd /* which protection domain*/,
            &qp_init_attr /* Initial attributes */)) {
		fprintf(stderr, "failed to create QP (%d)\n", errno);
	       return 1;
	}
	struct ibv_qp *client_qp = id->qp;


    struct rdma_conn_param conn_param;

	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3; // if fail, then how many times to retry

	if (rdma_connect(id, &conn_param)) {
		fprintf(stderr, "failed to connect to remote host (%d)\n", errno);
		return 1;
	}

	fprintf(stderr, "awaiting connection established event...\n");
	if (process_rdma_cm_event(event_channel, 
			RDMA_CM_EVENT_ESTABLISHED,
			&cm_event)) {
		fprintf(stderr, "failed to get cm event (%d)\n", errno);
	       return 1;
	}

	if (rdma_ack_cm_event(cm_event)) {
		fprintf(stderr, "failed to acknowledge cm event (%d)\n", errno);
		return 1;
	}
	printf("...connection established!\n");
    
    return 0;
}