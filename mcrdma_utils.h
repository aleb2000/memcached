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

/* 
 * Processes an RDMA connection management (CM) event.
 * Adapted from rdma example.
 * @param echannel       CM event channel where the event is expected. 
 * @param expected_event Expected event type 
 * @param cm_event       where the event will be stored
 * @return               0 on success, a non-zero value on error
 */
int mcrdma_process_event(struct rdma_event_channel *echannel, 
    	enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

#endif // MCRDMA_UTILS_H