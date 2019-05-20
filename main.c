#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
    int opt = 1, workerThreads = 0, bufferSize = 0, fd_client = 0;
    uint16_t portNum = 0, serverPort = 0;
    struct sockaddr_in server, client;
    struct sockaddr *server_ptr = (struct sockaddr *) &server;
    struct sockaddr *client_ptr = (struct sockaddr *) &client;
    char *dirname = NULL, *serverIP = NULL, buffer1[256], buffer2[256];

    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    memset(server_ptr, 0, sizeof(struct sockaddr));
    memset(client_ptr, 0, sizeof(struct sockaddr));

    /* Create socket */
    if ((fd_client = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
    }

    /* Config*/
    if (setsockopt(fd_client, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = INADDR_ANY;
    client.sin_port = htons(portNum);

    printf("Bind client to %s:%d address ...\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
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
        printf("Connect to server [%d] %s:%d sussessfully!\n", fd_client,
               inet_ntoa(server.sin_addr), ntohs(server.sin_port));
        strcpy(buffer2, "Hello");
        send(fd_client, buffer2, 256, 0);
        recv(fd_client, buffer1, 256, 0);
        printf("Server : %s\n", buffer1);
    }
    return 0;
}
