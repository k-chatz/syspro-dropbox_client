#include <lzma.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "connection.h"

/**
 * Open TCP connection.*/
int openConnection(struct sockaddr_in address) {
    int fd = 0;
    struct sockaddr *in_addr_ptr = NULL;
    in_addr_ptr = (struct sockaddr *) &address;

    /* Create socket */
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
        return 0;
    }

    fprintf(stdout, "::Connecting socket %d to remote host %s:%d::\n", fd, inet_ntoa(address.sin_addr),
            ntohs(address.sin_port));

    /* Initiate connection */
    if (connect(fd, in_addr_ptr, sizeof(struct sockaddr)) < 0) {
        perror("connect");
        return 0;
    }

    fprintf(stdout, "::Connection to remote host %s:%d established::\n", inet_ntoa(address.sin_addr),
            ntohs(address.sin_port));

    return fd;
}
