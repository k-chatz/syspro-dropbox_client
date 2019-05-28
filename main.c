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
#include "list.h"

#define COLOR "\x1B[33m"
#define RESET "\x1B[0m"

typedef struct session {
    size_t bytes;
    void *buffer;
    int chunks;
} Session;

typedef struct client {
    in_addr_t ip;
    in_port_t port;
} *Client;

typedef struct {
    in_addr_t ip;
    in_port_t port;
    char pathname[128];
    int version;
} circular_buffer_t;

typedef struct {
    circular_buffer_t *buffer;
    int start;
    int end;
    int count;
} pool_t;

static volatile int quit_request = 0;
char *dirname = NULL, *serverIP = NULL;
pthread_cond_t cond_nonempty;
pthread_cond_t cond_nonfull;
pthread_mutex_t mtx_client_list, mtx_pool;

pool_t pool;
List list = NULL;
int workerThreads = 0, bufferSize = 0;
uint16_t portNum = 0, serverPort = 0;

void createCircularBuffer(pool_t *pool) {
    int i;
    pool->buffer = malloc(bufferSize * sizeof(circular_buffer_t));
    for (i = 0; i < bufferSize; i++) {
        pool->buffer[i].ip = 0;
        pool->buffer[i].port = 0;
        bzero(pool->buffer[i].pathname, 128);
        pool->buffer[i].version = 0;
    }
    pool->start = 0;
    pool->end = -1;
    pool->count = 0;
}

void destroyCircularBuffer(pool_t *pool) {
    free(pool->buffer);
}

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

/**
 * Open TCP connection.*/
int openConnection(in_addr_t ip, in_port_t port) {
    struct sockaddr_in in_addr;
    struct sockaddr *in_addr_ptr = NULL;
    int fd = 0;

    in_addr_ptr = (struct sockaddr *) &in_addr;
    memset(in_addr_ptr, 0, sizeof(struct sockaddr));

    /* Create socket */
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
        return 0;
    }

    in_addr.sin_family = AF_INET;
    in_addr.sin_addr.s_addr = ip;
    in_addr.sin_port = port;

    printf("::Connecting socket %d to remote host %s:%d::\n", fd, inet_ntoa(in_addr.sin_addr), ntohs(in_addr.sin_port));

    /* Initiate connection */
    if (connect(fd, in_addr_ptr, sizeof(struct sockaddr)) < 0) {
        perror("connect");
        return 0;
    }
    printf("::Connection to remote host %s:%d established::\n", inet_ntoa(in_addr.sin_addr),
           ntohs(in_addr.sin_port));
    return fd;
}

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

int createSession(Session *s, int fd, fd_set *set, int *lfd) {
    if (fd <= FD_SETSIZE) {
        s[fd].buffer = malloc(1);
        s[fd].bytes = 1;
        s[fd].chunks = 0;
        FD_SET(fd, set);
        if (fd > *lfd) {
            *lfd = fd;
        }
        return 1;
    } else {
        return 0;
    }
}

/**
 * Handle requests.*/
void requestHandler(int fd_client, void *buffer) {
    unsigned int clients = 0;
    Client c = NULL, client = NULL;
    int found = 0, offset = 0;
    if (strncmp(buffer, "GET_FILE_LIST", 13) == 0) {
        printf("REQUEST: GET_FILE_LIST\n");
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, buffer + 13, sizeof(struct client));
        printClientTuple(c);
        printf("\n");
        listSetCurrentToStart(list);
        while ((client = listNext(list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                break;
            }
        }
        if (found) {
            printf("Client exists!\n");
            /*TODO: Get file version like this: printf("|%ld|", s.st_ctim.tv_nsec );*/

            /*GET_FILE_LIST στέλνει στον πελάτη μια λίστα ονομάτων (pathnames) όλων των αρχείων που
            βρίσκονται στο φάκελο ​dirName ​. Τα αρχεία μπορούν να είναι σε υποκαταλόγους κάτω από
            το dirName. Το πρωτόκολλο για να επιστραφούν τα ονόματα θα είναι FILE_LIST n
            <pathname1, version1> … <pathnameN,versionN> όπου n είναι ο αριθμός των ονομάτων
            αρχείων που θα επιστραφούν. Θεωρούμε πως το pathname δεν μπορεί να περιέχει το
            κόμμα.*/

        } else {
            free(c);
            send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
            fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
        }
    } else if (strncmp(buffer, "GET_FILE", 8) == 0) {
        printf("REQUEST: GET_FILE\n");
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, buffer + 13, sizeof(struct client));
        printClientTuple(c);
        printf("\n");
        listSetCurrentToStart(list);
        while ((client = listNext(list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                break;
            }
        }
        if (found) {
            printf("Client exists!\n");

            /*GET_FILE <pathname, version> Ελέγχει αν το αρχείο με όνομα ​dirName/​pathname υπάρχει (το
            dirName έχει δοθεί ως παράμετρος στο πρόγραμμα). Αν δεν υπάρχει τότε στέλνεται το string
            FILE_NOT_FOUND. Αν υπάρχει, ελέγχει αν έχει αλλάξει το αρχείο σε σχέση με την έκδοση
            που ζητείται. Αν δεν ἐχει αλλάξει το αρχείο (δηλαδή η version αντιστοιχεί στην τελευταία
            εκδοχή του αρχείου), τότε στέλνεται το string FILE_UP_TO_DATE στον πελάτη. Αν η τοπική
            έκδοση διαφέρει από τη ζητούμενη, τότε θα πρέπει να επιστραφεί το αρχείο. Το τι θα περιέχει
            η πληροφορία version είναι δική σας σχεδιαστική επιλογή: μπορεί να είναι π.χ., ένα hash,
            ένα timestamp, η ένας αύξοντας αριθμός. Αν έχει αλλάξει το αρχείο, τότε επιστρέφεται το
            string FILE_SIZE version n byte0byte1...byten, όπου version είναι η παρούσα έκδοση του
            αρχείου, n είναι το μέγεθος του αρχείου σε bytes, και ακολούθως τα bytes του αρχείου.*/


        } else {
            free(c);
            send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
            fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
        }

    } else if (strncmp(buffer, "USER_ON", 7) == 0) {
        printf("REQUEST: USER_ON ");
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, buffer + 7, sizeof(struct client));
        printClientTuple(c);
        printf("\n");
        listSetCurrentToStart(list);
        while ((client = listNext(list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                fprintf(stderr, "Duplicate entry!\n");
                break;
            }
        }
        if (!found) {
            if (!listInsert(list, c)) {
                fprintf(stderr, "Insert error!\n");
                free(c);
            }
        } else {
            free(c);
        }
    } else if (strncmp(buffer, "USER_OFF", 8) == 0) {
        printf("REQUEST: USER_OFF\n");
        c = malloc(sizeof(struct client));
        memcpy(c, buffer + 8, sizeof(struct client));
        listSetCurrentToStart(list);
        while ((client = listNext(list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                listSetCurrentToStart(list);
                if (listRemove(list, client)) {
                    send(fd_client, "USER_OFF_SUCCESS", 16, 0);
                    fprintf(stdout, "USER_OFF_SUCCESS\n");
                } else {
                    send(fd_client, "ERROR_NOT_REMOVED", 17, 0);
                    fprintf(stderr, "ERROR_NOT_REMOVED\n");
                }
                break;
            }
        }
        if (!found) {
            send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
            fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
        }
        free(c);
    } else if (strncmp(buffer, "LOG_ON_SUCCESS", 14) == 0) {
        printf("RESPONSE: LOG_ON_SUCCESS\n");
    } else if (strncmp(buffer, "ALREADY_LOGGED_IN", 17) == 0) {
        printf("RESPONSE: ALREADY_LOGGED_IN\n");
    } else if (strncmp(buffer, "CLIENT_LIST", 11) == 0) {
        printf("RESPONSE: CLIENT_LIST ");
        offset = 11;
        memcpy(&clients, buffer + offset, sizeof(unsigned int));
        offset = offset + sizeof(unsigned int);
        printf("%d ", clients);
        for (int i = 0; i < clients; i++) {
            c = malloc(sizeof(struct client));
            memcpy(c, buffer + offset, sizeof(struct client));
            offset = offset + sizeof(struct client);
            if (listInsert(list, c)) {
                printClientTuple(c);
            }
        }
        printf("\n");
    } else if (strncmp(buffer, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31) == 0) {
        printf("RESPONSE: ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
    } else if (strncmp(buffer, "ERROR_NOT_REMOVED", 17) == 0) {
        printf("RESPONSE: ERROR_NOT_REMOVED\n");
    } else if (strncmp(buffer, "LOG_OFF_SUCCESS", 15) == 0) {
        printf("RESPONSE: LOG_OFF_SUCCESS\n");
    } else if (strncmp(buffer, "GET_CLIENTS", 11) == 0) {
        printf("REQUEST: GET_CLIENTS\n");
        c = malloc(sizeof(struct client));
        memcpy(c, buffer + 11, sizeof(struct client));
        clients = listGetLength(list) - 1;
        send(fd_client, "CLIENT_LIST", 11, 0);
        fprintf(stdout, "CLIENT_LIST ");
        send(fd_client, &clients, sizeof(unsigned int), 0);
        fprintf(stdout, "%d ", clients);
        listSetCurrentToStart(list);
        while ((client = listNext(list)) != NULL) {
            if (!(c->ip == client->ip && c->port == client->port)) {
                send(fd_client, client, sizeof(struct client), 0);
                printClientTuple(client);
            }
        }
        fprintf(stdout, "\n");
        free(c);
    } else if (strncmp(buffer, "UNKNOWN_COMMAND", 15) == 0) {
        printf("RESPONSE: UNKNOWN_COMMAND\n");
    } else {
        fprintf(stderr, "UNKNOWN_COMMAND\n");
        send(fd_client, "UNKNOWN_COMMAND", 15, 0);
    }
}

void destroySession(Session *s, int fd, fd_set *set, int *lfd) {
    FD_CLR(fd, set);
    if (fd == *lfd) {
        *lfd--;
    }
    close(fd);
    free(s[fd].buffer);
    s[fd].buffer = NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr *listen_in_addr_ptr = NULL, *new_client_in_addr_ptr = NULL, *client_in_addr_ptr = NULL;
    struct sockaddr *server_ptr = NULL, *client_ptr = NULL, *listen_ptr = NULL;
    struct sockaddr_in server_in_addr, client_in_addr, listen_in_addr;
    struct sockaddr_in new_client_in_addr;
    struct in_addr currentHostAddr;

    struct hostent *hostEntry = NULL;
    struct timespec timeout;
    struct sigaction sa;
    int opt = 1, lfd = 0, fd_listen = 0, fd_client = 0, fd_new_client = 0, activity = 0, fd_active = 0;
    char hostBuffer[256], *currentHostStrIp = NULL;
    unsigned int clients = 0;
    void *rcv_buffer = NULL;

    ssize_t bytes = 0;
    Session s[FD_SETSIZE];
    fd_set set, read_fds;
    size_t socket_rcv_size = 0, socket_snd_size = 0;
    socklen_t st_rcv_len = 0, st_snd_len = 0;
    socklen_t client_len = 0;
    Client c = NULL;

    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    pthread_mutex_init(&mtx_client_list, 0);
    pthread_mutex_init(&mtx_pool, 0);

    pthread_cond_init(&cond_nonempty, 0);
    pthread_cond_init(&cond_nonfull, 0);

    timeout.tv_sec = 60;
    timeout.tv_nsec = 0;

    /* Initialize file descriptor sets.*/
    FD_ZERO(&set);
    FD_ZERO(&read_fds);

    /* Create clients list.*/
    listCreate(&list);

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

    if (fd_listen > lfd) {
        lfd = fd_listen;
    }

    FD_SET(fd_listen, &set);

    getsockopt(fd_listen, SOL_SOCKET, SO_RCVBUF, (void *) &socket_rcv_size, &st_rcv_len);
    getsockopt(fd_listen, SOL_SOCKET, SO_SNDBUF, (void *) &socket_snd_size, &st_snd_len);

    rcv_buffer = malloc(socket_rcv_size + 1);

    /* Config*/
    if (setsockopt(fd_listen, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    printf("Bind listening socket %d to %s:%d address ...\n",
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

    for (int i = 0; i < FD_SETSIZE; i++) {
        s[i].buffer = NULL;
        s[i].bytes = 0;
        s[i].chunks = 0;
    }

    /* LOG_ON*/
    if ((fd_client = openConnection(inet_addr(serverIP), htons(serverPort))) > 0) {
        if (!createSession(s, fd_client, &set, &lfd)) {
            fprintf(stderr, "HOST_IS_TOO_BUSY");
        }
        send(fd_client, "LOG_ON", 6, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    /* GET_CLIENTS*/
    if ((fd_client = openConnection(inet_addr(serverIP), htons(serverPort))) > 0) {
        createSession(s, fd_client, &set, &lfd);
        send(fd_client, "GET_CLIENTS", 11, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    /* Create circular buffer.*/
    createCircularBuffer(&pool);

    /****************************************************************************************************/

    printf("::Waiting for connections on %s:%d::\n", currentHostStrIp, portNum);
    while (!quit_request) {
        read_fds = set;
        activity = pselect(lfd + 1, &read_fds, NULL, NULL, &timeout, &oldset);
        //activity = select(lfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0 && (errno != EINTR)) {
            perror("select");
        } else if (activity == 0) {
            for (int i = 0; i < FD_SETSIZE; i++) {
                if (s[i].buffer != NULL) {
                    fprintf(stdout, "The request timed out, connection on socket %d is about to be closed ...\n", i);
                    printf("::%ld bytes were transferred into %d different chunks on socket %d::\n",
                           s[i].bytes - 1,
                           s[i].chunks, i);
                    printf(COLOR"%s\n"RESET"\n", (char *) s[i].buffer);
                    shutdown(i, SHUT_RD);
                    requestHandler(i, s[i].buffer);
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
                    if ((fd_new_client = accept(fd_active, new_client_in_addr_ptr, &client_len)) < 0) {
                        perror("accept");
                        break;
                    }
                    printf("::Accept new client (%s:%d) on socket %d::\n", inet_ntoa(new_client_in_addr.sin_addr),
                           ntohs(new_client_in_addr.sin_port),
                           fd_new_client);
                    if (!createSession(s, fd_new_client, &set, &lfd)) {
                        fprintf(stderr, "HOST_IS_TOO_BUSY");
                        send(fd_new_client, "HOST_IS_TOO_BUSY", 16, 0);
                        close(fd_new_client);
                    }
                } else {
                    bzero(rcv_buffer, socket_rcv_size);
                    bytes = recv(fd_active, rcv_buffer, socket_rcv_size, 0);
                    if (bytes == 0) {
                        printf("::%ld bytes were transferred into %d different chunks on socket %d::\n",
                               s[fd_active].bytes - 1,
                               s[fd_active].chunks, fd_active);
                        printf("::"COLOR"%s"RESET"::\n", (char *) s[fd_active].buffer);
                        shutdown(fd_active, SHUT_RD);
                        if (s[fd_active].bytes - 1 > 0) {
                            requestHandler(fd_active, s[fd_active].buffer);
                        } else {
                            printf("Empty response\n");
                        }
                        shutdown(fd_active, SHUT_WR);
                        destroySession(s, fd_active, &set, &lfd);
                    } else if (bytes > 0) {
                        size_t offset = s[fd_active].chunks ? s[fd_active].bytes - 1 : 0;
                        s[fd_active].buffer = realloc(s[fd_active].buffer, s[fd_active].bytes + bytes - 1);
                        memcpy(s[fd_active].buffer + offset, rcv_buffer, (size_t) bytes);
                        s[fd_active].bytes += bytes;
                        s[fd_active].chunks++;
                        //printf("::Receive %ld bytes from chunk %d on socket %d::\n", bytes, s[fd_active].chunks, fd_active);
                        //printf(COLOR"%s"RESET"\n", (char *) rcv_buffer);
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
    if ((fd_client = openConnection(inet_addr(serverIP), htons(serverPort))) > 0) {
        send(fd_client, "LOG_OFF", 7, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    //TODO: pthread_joins

    free(currentHostStrIp);
    free(rcv_buffer);

    listDestroy(&list);

    pthread_cond_destroy(&cond_nonempty);
    pthread_cond_destroy(&cond_nonfull);

    pthread_mutex_destroy(&mtx_client_list);
    pthread_mutex_destroy(&mtx_pool);

    destroyCircularBuffer(&pool);
    return 0;
}
