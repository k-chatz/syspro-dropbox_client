#ifndef CLIENT_H
#define CLIENT_H

typedef struct client {
    in_addr_t ip;
    in_port_t port;
} *Client;

Client createClient(in_addr_t addr, in_port_t port);

void printClientTuple(Client c);

#endif
