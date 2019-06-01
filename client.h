#ifndef CLIENT_H
#define CLIENT_H

typedef struct client {
    in_addr_t ip;
    in_port_t port;
} *Client;

void printClientTuple(Client c);

#endif
