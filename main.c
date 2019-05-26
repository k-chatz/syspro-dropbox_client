#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zconf.h>
#include <netdb.h>

#define COLOR "\x1B[33m"
#define RESET "\x1B[0m"

typedef struct client {
    in_addr_t ip;
    in_port_t port;
} *Client;

void wrongOptionValue(char *opt, char *val) {
    fprintf(stderr, "\nWrong value [%s] for option '%s'\n", val, opt);
    exit(EXIT_FAILURE);
}

/**
 * Read options from command line*/
void readOptions(
        int argc,
        char **argv,
        char **dirname,                     /*dirname*/
        uint16_t *portNum,                  /*portNum*/
        int *workerThreads,                 /*workerThreads*/
        int *bufferSize,                    /*bufferSize*/
        uint16_t *serverPort,               /*serverPort*/
        char **serverIP                     /*serverIP*/
) {
    int i;
    char *opt, *optVal;
    for (i = 1; i < argc; ++i) {
        opt = argv[i];
        optVal = argv[i + 1];
        if (strcmp(opt, "-d") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *dirname = optVal;
            } else {
                wrongOptionValue(opt, optVal);
            }
        } else if (strcmp(opt, "-p") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *portNum = (uint16_t) strtol(optVal, NULL, 10);
            } else {
                wrongOptionValue(opt, optVal);
            }
        } else if (strcmp(opt, "-w") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *workerThreads = (int) strtol(optVal, NULL, 10);
            } else {
                wrongOptionValue(opt, optVal);
            }
        } else if (strcmp(opt, "-b") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *bufferSize = (int) strtol(optVal, NULL, 10);
            } else {
                wrongOptionValue(opt, optVal);
            }
        } else if (strcmp(opt, "-sp") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *serverPort = (uint16_t) strtol(optVal, NULL, 10);
            } else {
                wrongOptionValue(opt, optVal);
            }
        } else if (strcmp(opt, "-sip") == 0) {
            if (optVal != NULL && optVal[0] != '-') {
                *serverIP = optVal;
            } else {
                wrongOptionValue(opt, optVal);
            }
        }
    }
}


int main(int argc, char *argv[]) {
    char *dirname = NULL, *serverIP = NULL, hostbuffer[256], *ip = NULL;
    int opt = 1, lfd = 0, workerThreads = 0, bufferSize = 0, fd_listen = 0, fd_client = 0;
    void *buffer = NULL;
    struct sockaddr *server_ptr = NULL, *client_ptr = NULL, *listen_ptr = NULL;
    struct sockaddr_in server_in_addr, client_in_addr, listen_in_addr;
    struct hostent *host_entry = NULL;
    void *rcv_buffer = NULL;
    uint16_t portNum = 0, serverPort = 0;
    ssize_t bytes = 0;

    size_t socket_rcv_size = 0, socket_snd_size = 0;
    socklen_t st_rcv_len = 0, st_snd_len = 0;

    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);
    ip = inet_ntoa(*((struct in_addr *) host_entry->h_addr_list[0]));

    st_rcv_len = sizeof(socket_rcv_size);
    st_snd_len = sizeof(socket_snd_size);

    server_ptr = (struct sockaddr *) &server_in_addr;
    client_ptr = (struct sockaddr *) &client_in_addr;
    listen_ptr = (struct sockaddr *) &listen_in_addr;

    memset(server_ptr, 0, sizeof(struct sockaddr));
    memset(client_ptr, 0, sizeof(struct sockaddr));
    memset(listen_ptr, 0, sizeof(struct sockaddr));

    /* Setup listening address*/
    listen_in_addr.sin_family = AF_INET;
    listen_in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_in_addr.sin_port = htons(portNum);

    /* Create listening socket*/
    if ((fd_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) <= 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    getsockopt(fd_listen, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);
    getsockopt(fd_listen, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    rcv_buffer = malloc(socket_rcv_size + 1);

    /* Config*/
    if (setsockopt(fd_listen, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    printf("Bind listening socket %d to %s:%d address ...\n", fd_listen, inet_ntoa(listen_in_addr.sin_addr),
           ntohs(listen_in_addr.sin_port));
    if (bind(fd_listen, listen_ptr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Listen*/
    if (listen(fd_listen, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    if (fd_listen > lfd) {
        lfd = fd_listen;
    }



    //LOG_ON//
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /* Create socket */
    if ((fd_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
    }

    getsockopt(fd_client, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);
    getsockopt(fd_client, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    buffer = malloc(socket_rcv_size + 1);

    server_in_addr.sin_family = AF_INET;
    server_in_addr.sin_addr.s_addr = inet_addr(serverIP);
    server_in_addr.sin_port = htons(serverPort);

    printf("Connecting socket %d to remote host %s:%d ...\n", fd_client, inet_ntoa(server_in_addr.sin_addr),
           ntohs(server_in_addr.sin_port));

    if (connect(fd_client, server_ptr, sizeof server_in_addr) < 0) {
        perror("connect");
    } else {
        printf("::Connection to remote host %s:%d established::\n", inet_ntoa(server_in_addr.sin_addr),
               ntohs(server_in_addr.sin_port));

        Client c = malloc(sizeof(struct client));
        c->ip = client_in_addr.sin_addr.s_addr;
        c->port = client_in_addr.sin_port;

        send(fd_client, "LOG_ON", 6, 0);
        send(fd_client, c, sizeof(struct client), 0);

        shutdown(fd_client, SHUT_WR);
    }

    do {
        bytes = recv(fd_client, buffer, socket_rcv_size, 0);
        printf("Receive %ld bytes from socket %d\n", bytes, fd_client);
        if (bytes == 0) {
            close(fd_client);
        } else if (bytes > 0) {
            printf(COLOR"%s"RESET"\n", (char *) buffer);
        } else {
            perror("recv");
        }
    } while (bytes > 0);
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




    //GET_CLIENTS//
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /* Create socket */
    if ((fd_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
    }

    getsockopt(fd_client, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);
    getsockopt(fd_client, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    buffer = malloc(socket_rcv_size + 1);

    server_in_addr.sin_family = AF_INET;
    server_in_addr.sin_addr.s_addr = inet_addr(serverIP);
    server_in_addr.sin_port = htons(serverPort);

    printf("Connecting socket %d to remote host %s:%d ...\n", fd_client, inet_ntoa(server_in_addr.sin_addr),
           ntohs(server_in_addr.sin_port));

    if (connect(fd_client, server_ptr, sizeof server_in_addr) < 0) {
        perror("connect");
    } else {
        printf("::Connection to remote host %s:%d established::\n", inet_ntoa(server_in_addr.sin_addr),
               ntohs(server_in_addr.sin_port));

        Client c = malloc(sizeof(struct client));
        c->ip = client_in_addr.sin_addr.s_addr;
        c->port = client_in_addr.sin_port;

        send(fd_client, "GET_CLIENTS", 11, 0);
        send(fd_client, c, sizeof(struct client), 0);

        shutdown(fd_client, SHUT_WR);


        unsigned int clients = 0;
        bzero(buffer, socket_rcv_size);
        bytes = recv(fd_client, buffer, 11, 0);
        printf(COLOR"\n%s "RESET, (char *) buffer);

        bytes = recv(fd_client, &clients, sizeof(unsigned int), 0);
        printf(COLOR"%d "RESET, clients);
        for (int i = 0; i < clients; i++) {
            bytes = recv(fd_client, c, sizeof(struct client), 0);
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = c->ip;
            addr.sin_port = c->port;
            printf(COLOR"<%s, %d> "RESET, inet_ntoa(addr.sin_addr), ntohs(c->port));
        }
        printf("\n");

    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    return 0;
}
