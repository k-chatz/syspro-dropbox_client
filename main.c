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
} Client;

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
    int opt = 1, workerThreads = 0, bufferSize = 0, fd_client = 0, hostname = 0;
    void *buffer = NULL;

    size_t socket_rcv_size = 0, socket_snd_size = 0;
    socklen_t st_rcv_len = 0, st_snd_len = 0;

    struct sockaddr *server_ptr = NULL, *client_ptr = NULL;
    struct sockaddr_in server, client;
    struct hostent *host_entry;

    uint16_t portNum = 0, serverPort = 0;
    ssize_t bytes = 0;

    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    st_rcv_len = sizeof(socket_rcv_size);
    st_snd_len = sizeof(socket_snd_size);

    server_ptr = (struct sockaddr *) &server;
    client_ptr = (struct sockaddr *) &client;

    memset(server_ptr, 0, sizeof(struct sockaddr));
    memset(client_ptr, 0, sizeof(struct sockaddr));

    /* Create socket */
    if ((fd_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
    }

    getsockopt(fd_client, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);

    getsockopt(fd_client, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    buffer = malloc(socket_rcv_size + 1);

    /* Config*/
    if (setsockopt(fd_client, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    hostname = gethostname(hostbuffer, sizeof(hostbuffer));

    host_entry = gethostbyname(hostbuffer);

    ip = inet_ntoa(*((struct in_addr *) host_entry->h_addr_list[0]));

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(ip);
    client.sin_port = htons(portNum);

    printf("Bind socket to %s:%d address ...\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
    if (bind(fd_client, client_ptr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(serverIP);
    server.sin_port = htons(serverPort);

    printf("Connecting to %s:%d ...\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));

    if (connect(fd_client, server_ptr, sizeof server) < 0) {
        perror("connect");
    } else {
        printf("Connect to server %s:%d sussessfully!\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));

        Client c;
        c.ip = client.sin_addr.s_addr;
        c.port = client.sin_port;

        send(fd_client, "LAG_ON\n", 7, 0);
        send(fd_client, "KWSTARIKANOS\n", 13, 0);
        send(fd_client, "COSTAS\n", 7, 0);
        send(fd_client, "CHATZOPOULOS\n", 13, 0);
        send(fd_client, "MSI\n", 4, 0);
        send(fd_client, "DELL\n", 5, 0);
        send(fd_client, "ALIENWARE\n", 10, 0);
        send(fd_client, "NETBOOK\n", 8, 0);

        //send(fd_client, &c, sizeof(Client), 0);
        shutdown(fd_client, SHUT_WR);

        do {
            bytes = recv(fd_client, buffer, socket_rcv_size, MSG_DONTWAIT);
            printf("Receive %ld bytes from socket %d\n", bytes, fd_client);
            if (bytes == 0) {
                close(fd_client);
            } else if (bytes > 0) {
                printf("Received content:\n"COLOR"%s"RESET"\n", (char *) buffer);
            } else {
                perror("recv");
            }
        } while (bytes > 0);
    }
    return 0;
}
