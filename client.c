#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "client.h"

void printClientTuple(Client c) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = c->ip;
    addr.sin_port = c->port;
    fprintf(stdout, "<%s, %d> ", inet_ntoa(addr.sin_addr), ntohs(c->port));
}
