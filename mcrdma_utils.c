#include "mcrdma_utils.h"
#include <sys/poll.h>

// Based on https://github.com/animeshtrivedi/rdma-example/blob/master/src/rdma_common.c
int mcrdma_process_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event)
{
	int ret = 1;
	struct pollfd pfds[1];
	pfds[0].fd = echannel->fd;
	pfds[0].events = POLLIN;
	int pret = poll(pfds, 1, -1);

	if(pret == -1) {
		perror("poll");
		exit(1);
	}

	if(pfds[0].revents & POLLIN) {
		ret = rdma_get_cm_event(echannel, cm_event);
	}
	
	if (ret) {
		if(errno != EAGAIN) { 
			// If we are polling we don't want to print an error message every time
			mcrdma_error("Failed to retrieve a cm event");
		}
		return -errno;
	}
	/* lets see, if it was a good event */
	if(0 != (*cm_event)->status){
		mcrdma_log("CM event has non zero status: %d\n", (*cm_event)->status);
		ret = -((*cm_event)->status);
		/* important, we acknowledge the event */
		rdma_ack_cm_event(*cm_event);
		return ret;
	}
	/* if it was a good event, was it of the expected type */
	if ((*cm_event)->event != expected_event) {
		mcrdma_log("Unexpected event received: %s [ expecting: %s ]\n", 
				rdma_event_str((*cm_event)->event),
				rdma_event_str(expected_event));
		/* important, we acknowledge the event */
		rdma_ack_cm_event(*cm_event);
		return -1; // unexpected event :(
	}
	mcrdma_log("A new %s type event is received \n", rdma_event_str((*cm_event)->event));
	/* The caller must acknowledge the event */
	return ret;
}

// Based on https://github.com/animeshtrivedi/rdma-example/blob/master/src/rdma_common.c
int poll_wce (struct mcrdma_state* s, struct ibv_wc *wc, int max_wc)
{
	int ret = -1, i, total_wc = 0;
	total_wc = 0;
	struct pollfd epfds[1];
	epfds[0].fd = s->echannel->fd;
	epfds[0].events = POLLIN;

	int c = 0;
	do {
		ret = ibv_poll_cq(s->client_cq /* the CQ, we got notification for */, 
			max_wc - total_wc /* number of remaining WC elements*/,
			wc + total_wc/* where to store */);
		if (ret < 0) {
			mcrdma_error("Failed to poll CQ for WC");
			return ret;
		}
		total_wc += ret;
		// Don't poll for disconnection at every iteration
		if(c % 10000 == 0 && ret == 0) {
			int pret = poll(epfds, 1, 0);

			if(pret == -1) {
				perror("poll");
				exit(1);
			}

			if(epfds[0].revents & POLLIN) {
				struct rdma_cm_event *cm_event = NULL;
				if(rdma_get_cm_event(s->echannel, &cm_event)) {
					mcrdma_error("Failed to get cm event when polling WCEs\n");
					exit(1);
				}

				if(cm_event->event == RDMA_CM_EVENT_DISCONNECTED) {
					rdma_ack_cm_event(cm_event);
					return -1;
				}
			}
		}
		c++;
	} while (total_wc < max_wc); 
	mcrdma_log("%d WC are completed \n", total_wc);
	/* Now we check validity and status of I/O work completions */
	for( i = 0 ; i < total_wc ; i++) {
		if (wc[i].status != IBV_WC_SUCCESS) {
			mcrdma_error("Work completion (WC) has error status: %s at index %d", 
					ibv_wc_status_str(wc[i].status), i);
			/* return negative value */
			return -(wc[i].status);
		}
	}
	return total_wc; 
}