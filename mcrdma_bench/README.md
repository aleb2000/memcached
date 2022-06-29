# mcrdma_bench
mcrdma_bench is a benchmarking utility for Memcached over RDMA.
It uses the mcrdma_client as a library to communicate with the server instance.

It performs a number of iterations for every client, with every client executing a type of operation (dependant on the TEST parameter), calculating the latency and at the end output mean, max and min latency values for every client, as well as a final mean score.
It supports two different types of benchmark:
* SET_GET: which will perform a SET operation followed by a GET using the same key
* PING_PONG: which uses a special PING command added to Memcached over RDMA

The mandatory arguments are:
* NET: networking technology to use, can be --rdma or --tcp
* TEST: can be --set-get or --ping-pong
* HOST: the ip address of a Memcached over RDMA instance
* PORT: the port of the instance
* CLIENTS: number of clients that will perform the benchmark simultaneously
* ITERS: number of iterations per client
* PAYLOAD_SIZE: size of the messages to send, in bytes

Example usage:
```
mcrdma_bench --rdma --set-get 10.149.0.53 11211 1 10000 256
```
