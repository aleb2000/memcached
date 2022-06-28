#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "mcrdma_client.h"
#include "../mcrdma_utils.h"

static struct mcrdma_client client;

int main(int argc, char** argv) {

    freopen("/dev/null", "w", stderr);

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

    //sleep(1);

    char* set_cmd = "set Test 0 100 5\r\nHello\r\n";
    char* get_cmd = "get Test\r\n";

    printf("##########################\n");
    printf("Trying to get the 'Test' key\n");

    char buf[MCRDMA_BUF_SIZE] = {0};
    memcpy(&buf, get_cmd, strlen(get_cmd));
    mcrdma_client_ascii_send_buf(&client, buf, strlen(get_cmd));

    printf("\nWaiting for response...\n\n");
    int len = mcrdma_client_ascii_recv(&client);
    printf("Server sesponse:\n%.*s", len, client.rbuf);

    //sleep(5);

    printf("##########################\n");
    printf("Trying to set the 'Test' key with value 'Hello'\n");

    bzero(buf, MCRDMA_BUF_SIZE);
    memcpy(&buf, set_cmd, strlen(set_cmd));
    mcrdma_client_ascii_send_buf(&client, buf, strlen(set_cmd));

    printf("\nWaiting for response...\n\n");
    len = mcrdma_client_ascii_recv(&client);
    printf("Server sesponse:\n%.*s", len, client.rbuf);

    //sleep(5);

    printf("##########################\n");
    printf("Trying to get the newly modified 'Test' key\n");

    bzero(buf, MCRDMA_BUF_SIZE);
    memcpy(&buf, get_cmd, strlen(get_cmd));
    mcrdma_client_ascii_send_buf(&client, buf, strlen(get_cmd));

    printf("\nWaiting for response...\n\n");
    len = mcrdma_client_ascii_recv(&client);
    printf("Server sesponse:\n%.*s", len, client.rbuf);

    //Keep alive
    //while(1);
    /*do {
        
        scanf("%s", buf);

        mcrdma_client_ascii_send(&client, buf, MCRDMA_BUF_SIZE);

    } while(1);*/
}