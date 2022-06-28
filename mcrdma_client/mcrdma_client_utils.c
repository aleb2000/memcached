#include "mcrdma_client_utils.h"

// Based on https://github.com/animeshtrivedi/rdma-example/blob/master/src/rdma_common.c
int mcrdma_process_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event)
{
	int ret = 1;
	ret = rdma_get_cm_event(echannel, cm_event);
	
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

int poll_cq_for_wc (struct ibv_cq* cq, struct ibv_wc *wc, int max_wc)
{
	//struct ibv_cq *cq_ptr = NULL;
	//void *context = NULL;
	int ret = -1, i, total_wc = 0;
	/* We wait for the notification on the CQ channel */
	// ret = ibv_get_cq_event(comp_channel, /* IO channel where we are expecting the notification */ 
	// 		&cq_ptr, /* which CQ has an activity. This should be the same as CQ we created before */ 
	// 		&context); /* Associated CQ user context, which we did set */
	// if (ret) {
	// 	mcrdma_error("Failed to get next CQ event");
	// 	return -errno;
	// }
	// /* Request for more notifications. */
	// ret = ibv_req_notify_cq(cq_ptr, 0);
	// if (ret){
	// 	mcrdma_error("Failed to request further notifications");
	// 	return -errno;
	// }
	/* We got notification. We reap the work completion (WC) element. It is 
	 * unlikely but a good practice it write the CQ polling code that 
	 * can handle zero WCs. ibv_poll_cq can return zero. Same logic as 
	 * MUTEX conditional variables in pthread programming.
	 */
	total_wc = 0;
	do {
		ret = ibv_poll_cq(cq /* the CQ, we got notification for */, 
			max_wc - total_wc /* number of remaining WC elements*/,
			wc + total_wc/* where to store */);
		if (ret < 0) {
			mcrdma_error("Failed to poll CQ for WC");
			/* ret is errno here */
			return ret;
		}
		total_wc += ret;
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
	/* Similar to connection management events, we need to acknowledge CQ events */
	// ibv_ack_cq_events(cq_ptr, 
	// 		1 /* we received one event notification. This is not 
	//		number of WC elements */);*/
	return total_wc; 
}
