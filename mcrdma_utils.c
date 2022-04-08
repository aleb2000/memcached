#include "mcrdma_utils.h"

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