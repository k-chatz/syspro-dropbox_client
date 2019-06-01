#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zconf.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "list.h"
#include "session.h"
#include "handler.h"
#include "file.h"
#include "buffer.h"
#include "client.h"
#include "request.h"

#define COLOR "\x1B[33m"
#define RESET "\x1B[0m"

static volatile int quit_request = 0;

int workerThreads = 0, bufferSize = 0, lfd = 0;
pthread_mutex_t mtx_client_list, mtx_pool;
pthread_cond_t condNonEmpty, condNonFull;
char *dirname = NULL, *serverIP = NULL;
uint16_t portNum = 0, serverPort = 0;
struct in_addr currentHostAddr;
List client_list = NULL;
Session s[FD_SETSIZE];
pool_t pool;
fd_set set;

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

/**
 * Signal handler.*/
static void hdl(int sig) {
    quit_request = 1;
}

void *worker(void *ptr) {
    circular_buffer_t data;
    printf("I am worker: %d\n", *((int *) ptr));
    while (1) {
        data = obtain(&pool);
        if (strcmp(data.pathname, "") == 0 && data.version == 0) {
            req_get_file_list(data.ip, data.port);
        } else {
            file_t_ptr file = malloc(sizeof(struct file_t));
            strcpy(file->pathname, data.pathname);
            file->version = 0;//data.version;
            req_get_file(data.ip, data.port, file);
            free(file);
        }
        pthread_cond_signal(&condNonFull);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr *new_client_ptr = NULL;
    struct sockaddr_in new_client;
    struct sockaddr *listen_ptr = NULL;
    struct sockaddr_in listen_in_addr;
    struct hostent *hostEntry = NULL;
    struct timespec timeout;
    struct sigaction sa;
    int opt = 1, fd_listen = 0, fd_new_client = 0, activity = 0, fd_active = 0;
    char hostBuffer[256], *currentHostStrIp = NULL;
    void *rcv_buffer = NULL;
    ssize_t bytes = 0;
    fd_set read_fds;
    size_t socket_rcv_size = 0, socket_snd_size = 0;
    socklen_t st_rcv_len = 0, st_snd_len = 0;
    socklen_t client_len = 0;

    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    pthread_t workers[workerThreads];

    pthread_mutex_init(&mtx_client_list, 0);
    pthread_mutex_init(&mtx_pool, 0);

    pthread_cond_init(&condNonEmpty, 0);
    pthread_cond_init(&condNonFull, 0);

    timeout.tv_sec = 3600;
    timeout.tv_nsec = 0;

    /* Initialize file descriptor sets.*/
    FD_ZERO(&set);
    FD_ZERO(&read_fds);

    /* Create clients client_list.*/
    listCreate(&client_list);

    /* Setup signal handler for SIGINT signal.*/
    sa.sa_handler = hdl;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Block SIGINT.
    sigset_t sigset, oldset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &oldset);

    /* Get ip and name of current host.*/
    gethostname(hostBuffer, sizeof(hostBuffer));
    hostEntry = gethostbyname(hostBuffer);
    currentHostAddr = *((struct in_addr *) hostEntry->h_addr_list[0]);
    currentHostStrIp = strdup(inet_ntoa(currentHostAddr));

    st_rcv_len = sizeof(socket_rcv_size);
    st_snd_len = sizeof(socket_snd_size);
    client_len = sizeof(struct sockaddr);

    listen_ptr = (struct sockaddr *) &listen_in_addr;
    new_client_ptr = (struct sockaddr *) &new_client;

    memset(listen_ptr, 0, sizeof(struct sockaddr));
    memset(new_client_ptr, 0, sizeof(struct sockaddr));

    /* Setup listening address*/
    listen_in_addr.sin_family = AF_INET;
    listen_in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_in_addr.sin_port = htons(portNum);

    /* Create listening socket*/
    if ((fd_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) <= 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (fd_listen > lfd) {
        lfd = fd_listen;
    }

    FD_SET(fd_listen, &set);

    getsockopt(fd_listen, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);
    getsockopt(fd_listen, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    rcv_buffer = malloc(socket_rcv_size + 1);

    /* Reuse the same address*/
    if (setsockopt(fd_listen, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "Bind listening socket %d to %s:%d address ...\n",
            fd_listen,
            inet_ntoa(listen_in_addr.sin_addr),
            ntohs(listen_in_addr.sin_port)
    );

    if (bind(fd_listen, listen_ptr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Listen*/
    if (listen(fd_listen, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    initSessionArray();

    /* LOG_ON request.*/
    req_log_on(inet_addr(serverIP), htons(serverPort));

    /* GET_CLIENTS request.*/
    req_get_clients(inet_addr(serverIP), htons(serverPort));

    /* Create circular buffer.*/
    createCircularBuffer(&pool);

    /* Create worker threads.*/
    for (int i = 0; i < workerThreads; i++) {
        pthread_create(&workers[i], NULL, worker, &i);
    }

    /****************************************************************************************************/

    fprintf(stdout, "::Waiting for connections on %s:%d::\n", currentHostStrIp, portNum);

    while (!quit_request) {
        read_fds = set;
        activity = pselect(lfd + 1, &read_fds, NULL, NULL, &timeout, &oldset);
        if (activity < 0 && (errno != EINTR)) {
            perror("select");
        } else if (activity == 0) {
            for (int i = 0; i < FD_SETSIZE; i++) {
                if (s[i].buffer != NULL) {
                    fprintf(stdout, "The request timed out, connection on socket %d is about to be closed ...\n", i);
                    fprintf(stdout, "::%ld bytes were transferred into %d different chunks on socket %d::\n",
                            s[i].bytes - 1,
                            s[i].chunks, i);
                    fprintf(stdout, "::"COLOR" %s "RESET"::\n",
                            (s[fd_active].bytes - 1 > 0) ? (char *) s[fd_active].buffer : "(Empty response body)");
                    shutdown(fd_active, SHUT_RD);
                    if (s[fd_active].bytes - 1 > 0) {
                        handler(fd_active, &s[fd_active]);
                    }
                    shutdown(i, SHUT_WR);
                    FD_CLR(i, &set);
                    if (i == lfd) {
                        lfd--;
                    }
                    close(i);
                    free(s[i].buffer);
                    s[i].buffer = NULL;
                };
            }
            continue;
        }

        if (quit_request) {
            fprintf(stdout, "C[%d]: quiting ...""\n", getpid());
            break;
        }

        for (fd_active = 0; fd_active <= lfd; fd_active++) {
            if (FD_ISSET(fd_active, &read_fds)) {
                if (fd_active == fd_listen) {

                    if ((fd_new_client = accept(fd_active, new_client_ptr, &client_len)) < 0) {
                        perror("accept");
                        break;
                    }
                    fprintf(stdout, "::Accept new client (%s:%d) on socket %d::\n",
                            inet_ntoa(new_client.sin_addr),
                            ntohs(new_client.sin_port),
                            fd_new_client);

                    if (!createSession(fd_new_client, &lfd, new_client, NULL)) {
                        fprintf(stderr, "HOST_IS_TOO_BUSY");
                        send(fd_new_client, "HOST_IS_TOO_BUSY", 16, 0);
                        close(fd_new_client);
                    }
                } else {
                    bzero(rcv_buffer, socket_rcv_size);
                    bytes = recv(fd_active, rcv_buffer, socket_rcv_size, 0);
                    if (bytes == 0) {
                        fprintf(stdout, "::%ld bytes were transferred into %d different chunks on socket %d::\n",
                                s[fd_active].bytes - 1,
                                s[fd_active].chunks, fd_active);
                        fprintf(stdout, "::"COLOR" %s "RESET"::\n",
                                (s[fd_active].bytes - 1 > 0) ? (char *) s[fd_active].buffer : "(Empty response body)");
                        shutdown(fd_active, SHUT_RD);
                        if (s[fd_active].bytes - 1 > 0) {
                            handler(fd_active, &s[fd_active]);
                        }
                        shutdown(fd_active, SHUT_WR);
                        destroySession(fd_active, &lfd, &set);
                    } else if (bytes > 0) {
                        size_t offset = s[fd_active].chunks ? s[fd_active].bytes - 1 : 0;
                        s[fd_active].buffer = realloc(s[fd_active].buffer, s[fd_active].bytes + bytes - 1);
                        memcpy(s[fd_active].buffer + offset, rcv_buffer, (size_t) bytes);
                        s[fd_active].bytes += bytes;
                        s[fd_active].chunks++;
                        //fprintf(stdout,"::Receive %ld bytes from chunk %d on socket %d::\n", bytes, s[fd_active].chunks, fd_active);
                        //fprintf(stdout,COLOR"%s"RESET"\n", (char *) rcv_buffer);
                    } else {
                        perror("recv");
                        send(fd_active, "-", 1, 0);
                        close(fd_active);
                    }
                }
            }
        }
    }

    /* Inform server with a LOG_OFF message.*/
    req_log_off(inet_addr(serverIP), htons(serverPort));

    /* Join workers*/
    for (int i = 0; i < workerThreads; i++) {
        pthread_join(workers[i], 0);
    }

    free(currentHostStrIp);
    free(rcv_buffer);

    listDestroy(&client_list);

    pthread_cond_destroy(&condNonEmpty);
    pthread_cond_destroy(&condNonFull);

    pthread_mutex_destroy(&mtx_client_list);
    pthread_mutex_destroy(&mtx_pool);

    destroyCircularBuffer(&pool);
    return 0;
}
