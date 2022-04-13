#ifndef MCRDMA_UTILS_H
#define MCRDMA_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <netdb.h>
#include <netinet/in.h>	
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#define mcrdma_log(...) fprintf(stderr, __VA_ARGS__);

#define mcrdma_error(...) do {                                  \
        fprintf(stderr, "MCRDMA ERROR: %s:%d | ", __FILE__, __LINE__);         \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, " | errno: %s (%d)\n", strerror(errno), errno); \
    } while(0);

// This value is tentative and a more dynamic approach might be used in the future
#define MCRDMA_BUF_SIZE 1024

#define MCRDMA_BUF_ACCESS_FLAGS (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE)

/**
 * Processes an RDMA connection management (CM) event.
 * Adapted from rdma example at https://github.com/animeshtrivedi/rdma-example/blob/master/src/rdma_common.c
 * @param echannel       CM event channel where the event is expected. 
 * @param expected_event Expected event type 
 * @param cm_event       where the event will be stored
 * @return               0 on success, a non-zero value on error
 */
int mcrdma_process_event(struct rdma_event_channel *echannel, 
    	enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

/**
 * Process max_wc work completion events on a given completion channel.
 * Adapted from rdma example at https://github.com/animeshtrivedi/rdma-example/blob/master/src/rdma_common.c
 * @param comp_channel The completion channel to get events from
 * @param wc           The array where to store the events
 * @param max_wc       Max number of work completion events to get
 * @return             The actual number of work completion events processed on success,
 *                      or a negative number on error
 */
int process_work_completion_events (struct ibv_comp_channel *comp_channel,
        struct ibv_wc *wc, int max_wc);


#endif // MCRDMA_UTILS_H