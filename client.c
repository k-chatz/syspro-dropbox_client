#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <malloc.h>
#include "client.h"

Client createClient(in_addr_t addr, in_port_t port) {
    Client c = malloc(sizeof(struct client));
    c->ip = addr;
    c->port = port;
    return c;
}

void printClientTuple(Client c) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = c->ip;
    addr.sin_port = c->port;
    fprintf(stdout, "<%s, %d> ", inet_ntoa(addr.sin_addr), ntohs(c->port));
}
