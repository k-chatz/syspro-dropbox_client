#include <netinet/in.h>
#include <stdio.h>
#include <malloc.h>
#include <arpa/inet.h>
#include "request.h"
#include "session.h"
#include "client.h"
#include "handler.h"
#include "connection.h"

void req_get_file_list(in_addr_t ip, in_port_t port) {
    int fd = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ip;
    address.sin_port = port;
    fprintf(stdout, "GET_FILE_LIST\n");
    if ((fd = openConnection(address)) > 0) {
        createSession(fd, &lfd, address, &set);
        send(fd, "GET_FILE_LIST", 13, 0);
        Client c1 = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd, c1, sizeof(struct client), 0);
        free(c1);
        shutdown(fd, SHUT_WR);
    }
}

void req_get_file(in_addr_t ip, in_port_t port, file_t_ptr file) {
    int fd = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ip;
    address.sin_port = port;
    if ((fd = openConnection(address)) > 0) {
        createSession(fd, &lfd, address, &set);
        send(fd, "GET_FILE", 8, 0);
        Client c1 = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd, c1, sizeof(struct client), 0);
        free(c1);
        send(fd, file, sizeof(struct file_t), 0);
        shutdown(fd, SHUT_WR);
    }
}

void req_log_on(in_addr_t ip, in_port_t port) {
    int fd = 0;
    struct sockaddr_in address;
    Client c = NULL;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ip;
    address.sin_port = port;
    if ((fd = openConnection(address)) > 0) {
        if (!createSession(fd, &lfd, address, &set)) {
            fprintf(stderr, "HOST_IS_TOO_BUSY");
        }
        send(fd, "LOG_ON", 6, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd, SHUT_WR);
    }
}

void req_get_clients(in_addr_t ip, in_port_t port) {
    int fd = 0;
    struct sockaddr_in address;
    Client c = NULL;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ip;
    address.sin_port = port;
    if ((fd = openConnection(address)) > 0) {
        if (!createSession(fd, &lfd, address, &set)) {
            fprintf(stderr, "HOST_IS_TOO_BUSY");
        }
        send(fd, "GET_CLIENTS", 11, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd, SHUT_WR);
    }
}

void req_log_off(in_addr_t ip, in_port_t port) {
    int fd = 0;
    struct sockaddr_in address;
    Client c = NULL;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(serverIP);
    address.sin_port = htons(serverPort);
    if ((fd = openConnection(address) > 0)) {
        send(fd, "LOG_OFF", 7, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd, SHUT_WR);
    }
}
