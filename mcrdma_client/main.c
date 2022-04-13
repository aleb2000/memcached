#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "mcrdma_client.h"
#include "../mcrdma_utils.h"

static struct mcrdma_client client;

int main(int argc, char** argv) {
    if(argc != 3) {
        printf("Usage: rdma_client HOST PORT\n");
        return 0;
    }

    char* host = argv[1];
    int port = atoi(argv[2]);
    
    bzero(&client.addr, sizeof(struct sockaddr_in));
    client.addr.sin_family = AF_INET;
    client.addr.sin_port = htons(port);


    if(!inet_pton(AF_INET, host, &client.addr.sin_addr)) {
        fprintf(stderr, "Invalid host address: %s\n", host);
        return 1;
    }

    if(mcrdma_client_init(&client)) {
        printf("Cannot initialize client\n");
        return 1;
    }

    printf("Client initialized\n");

    if(mcrdma_client_alloc_resources(&client)) {
        printf("Cannot allocate resources\n");
        return 1;
    }

    printf("Resources allocated\n");

    if(mcrdma_client_connect(&client)) {
        printf("Cannot connect to server\n");
        return 1;
    }

    printf("Connection established!\n");

    char buf[MCRDMA_BUF_SIZE] = {0};
    do {
        
        scanf("%s", buf);

        mcrdma_client_ascii_send(&client, buf, MCRDMA_BUF_SIZE);

    } while(1);
}