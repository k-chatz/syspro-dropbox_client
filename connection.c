#include <lzma.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <malloc.h>
#include <zconf.h>
#include <strings.h>
#include <string.h>
#include "connection.h"
#include "session.h"
#include "handler.h"
#include "request.h"

#define YELLOW "\x1B[33m"
#define RESET "\x1B[0m"

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

/**
 * fd Monitor.*/
void fdMonitor(fd_set *set, fd_set *read_fds, sigset_t *oldset) {
    struct timespec timeout;
    int activity = 0, fd_active = 0;
    *read_fds = *set;
    timeout.tv_sec = 3600;
    timeout.tv_nsec = 0;
    activity = pselect(lfd + 1, read_fds, NULL, NULL, &timeout, oldset);
    if (activity < 0 && (errno != EINTR)) {
        perror("pselect");
    } else if (activity == 0) {
        for (int i = 0; i < FD_SETSIZE; i++) {
            if (s[i].buffer != NULL) {
                fprintf(stdout, "The request timed out, connection on socket %d is about to be closed ...\n", i);
                fprintf(stdout, "::%ld bytes were transferred into %d different chunks on socket %d::\n",
                        s[i].bytes - 1,
                        s[i].chunks, i);
                fprintf(stdout, "::"YELLOW" %s "RESET"::\n",
                        (s[fd_active].bytes - 1 > 0) ? (char *) s[fd_active].buffer : "(Empty response body)");
                shutdown(fd_active, SHUT_RD);
                if (s[fd_active].bytes - 1 > 0) {
                    handler(fd_active, &s[fd_active]);
                }
                shutdown(i, SHUT_WR);
                FD_CLR(i, set);
                if (i == lfd) {
                    lfd--;
                }
                close(i);
                free(s[i].buffer);
                s[i].buffer = NULL;
            };
        }
        return;
    }
}

/**
 * fd Activity Handler.*/
void fdActivityHandler(fd_set *read_fds, int fd_listen, void *buffer, size_t bufferSize) {
    int fd_active = 0, fd_new_client = 0;
    struct sockaddr *new_client_ptr = NULL;
    struct sockaddr_in new_client;
    socklen_t client_len = sizeof(struct sockaddr);
    new_client_ptr = (struct sockaddr *) &new_client;
    memset(new_client_ptr, 0, sizeof(struct sockaddr));
    ssize_t bytes = 0;

    for (fd_active = 0; fd_active <= lfd; fd_active++) {
        if (FD_ISSET(fd_active, read_fds)) {
            if (fd_active == fd_listen) {

                if ((fd_new_client = accept(fd_active, new_client_ptr, &client_len)) < 0) {
                    perror("accept");
                    break;
                }
                fprintf(stdout, "::Accept new client (%s:%d) on socket %d::\n",
                        inet_ntoa(new_client.sin_addr),
                        ntohs(new_client.sin_port),
                        fd_new_client);

                if (!createSession(fd_new_client, new_client, &set)) {
                    fprintf(stderr, "HOST_IS_TOO_BUSY");
                    send(fd_new_client, "HOST_IS_TOO_BUSY", 16, 0);
                    close(fd_new_client);
                }
            } else {
                bzero(buffer, bufferSize);
                bytes = recv(fd_active, buffer, bufferSize, 0);
                if (bytes == 0) {
                    fprintf(stdout, "::%ld bytes were transferred into %d different chunks on socket %d::\n",
                            s[fd_active].bytes - 1,
                            s[fd_active].chunks, fd_active);
                    fprintf(stdout, "::"YELLOW" %s "RESET"::\n",
                            (s[fd_active].bytes - 1 > 0) ? (char *) s[fd_active].buffer : "(Empty response body)");
                    shutdown(fd_active, SHUT_RD);
                    if (s[fd_active].bytes - 1 > 0) {
                        handler(fd_active, &s[fd_active]);
                    }
                    shutdown(fd_active, SHUT_WR);
                    destroySession(fd_active, &set);
                } else if (bytes > 0) {
                    size_t offset = s[fd_active].chunks ? s[fd_active].bytes - 1 : 0;
                    s[fd_active].buffer = realloc(s[fd_active].buffer, s[fd_active].bytes + bytes - 1);
                    memcpy(s[fd_active].buffer + offset, buffer, (size_t) bytes);
                    s[fd_active].bytes += bytes;
                    s[fd_active].chunks++;
                    //fprintf(stdout,"::Receive %ld bytes from chunk %d on socket %d::\n", bytes, s[fd_active].chunks, fd_active);
                    //fprintf(stdout,YELLOW"%s"RESET"\n", (char *) rcv_buffer);
                } else {
                    perror("recv");
                    close(fd_active);
                }
            }
        }
    }
}
