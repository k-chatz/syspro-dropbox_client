#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <zconf.h>
#include <arpa/inet.h>
#include <errno.h>
#include "list.h"
#include "session.h"
#include "handler.h"
#include "file.h"
#include "buffer.h"
#include "client.h"
#include "request.h"
#include "connection.h"

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

    struct sockaddr *listen_ptr = NULL;
    struct sockaddr_in listen_in_addr;
    struct hostent *hostEntry = NULL;

    struct sigaction sa;
    int opt = 1, fd_listen = 0;
    char hostBuffer[256], *currentHostStrIp = NULL;
    void *rcv_buffer = NULL;

    fd_set read_fds;
    size_t socket_rcv_size = 0;
    socklen_t st_rcv_len = 0, st_snd_len = 0;


    /* Read argument options from command line*/
    readOptions(argc, argv, &dirname, &portNum, &workerThreads, &bufferSize, &serverPort, &serverIP);

    pthread_t workers[workerThreads];

    pthread_mutex_init(&mtx_client_list, 0);
    pthread_mutex_init(&mtx_pool, 0);

    pthread_cond_init(&condNonEmpty, 0);
    pthread_cond_init(&condNonFull, 0);


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

    listen_ptr = (struct sockaddr *) &listen_in_addr;


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
        fdMonitor(&set, &read_fds, &oldset);
        if (quit_request) {
            fprintf(stdout, "C[%d]: quiting ...""\n", getpid());
            break;
        }
        fdActivityHandler(&read_fds, rcv_buffer, socket_rcv_size);
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
