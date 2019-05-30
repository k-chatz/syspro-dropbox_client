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

#define COLOR "\x1B[33m"
#define RESET "\x1B[0m"

typedef struct session {
    size_t bytes;
    void *buffer;
    int chunks;
    struct sockaddr_in address;
} Session;

typedef struct client {
    in_addr_t ip;
    in_port_t port;
} *Client;

typedef struct file_t {
    char pathname[128];
    long int version;
} *file_t_ptr;

typedef struct {
    in_addr_t ip;
    in_port_t port;
    char pathname[128];
    long int version;
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
List client_list = NULL;
Session s[FD_SETSIZE];
fd_set set;
int workerThreads = 0, bufferSize = 0, lfd = 0;
uint16_t portNum = 0, serverPort = 0;
struct in_addr currentHostAddr;

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

void printFileTuple(file_t_ptr file) {
    fprintf(stdout, "<%s, %ld> ", file->pathname, file->version);
}

int createSession(int fd, struct sockaddr_in address) {
    if (fd <= FD_SETSIZE) {
        s[fd].buffer = malloc(1);
        s[fd].bytes = 1;
        s[fd].chunks = 0;
        s[fd].address = address;
        FD_SET(fd, &set);
        if (fd > lfd) {
            lfd = fd;
        }
        return 1;
    } else {
        return 0;
    }
}

/**
 * Read directory & subdirectories recursively*/
void rec_readdir(List files, const char *_p, char *input_dir) {
    char dirName[PATH_MAX], path[PATH_MAX], *fileName = NULL;
    struct dirent *d = NULL;
    struct stat s = {0};
    DIR *dir = NULL;
    file_t_ptr file = NULL;
    if ((dir = opendir(_p))) {
        while ((d = readdir(dir))) {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
                continue;
            }

            /* Construct real path.*/
            if (sprintf(path, "%s/%s", _p, d->d_name) < 0) {
                fprintf(stderr, "\n%s:%d-sprintf error\n", __FILE__, __LINE__);
                exit(EXIT_FAILURE);
            }

            /* Get file statistics*/
            if (!stat(path, &s)) {
                fileName = path + strlen(input_dir) + 1;
                if (S_ISDIR(s.st_mode)) {
                    strcpy(dirName, fileName);
                    strcat(dirName, "/\0");
                    file = malloc(sizeof(struct file_t));
                    strcpy(file->pathname, dirName);
                    file->version = s.st_ctim.tv_nsec;
                    listInsert(files, file);
                    rec_readdir(files, path, input_dir);
                } else if (S_ISREG(s.st_mode)) {
                    file = malloc(sizeof(struct file_t));
                    strcpy(file->pathname, fileName);
                    file->version = s.st_ctim.tv_nsec;
                    listInsert(files, file);
                }
            } else {
                fprintf(stderr, "\n%s:%d-[%s] stat error: '%s'\n", __FILE__, __LINE__, path, strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        closedir(dir);
    }
}

/**
 * Handle requests.*/
void requestHandler(int fd_client, Session *session) {
    unsigned int clients = 0, files = 0;
    Client c = NULL, client = NULL;
    int found = 0, offset = 0, fd_file = 0;
    List file_list = NULL;
    file_t_ptr file = NULL;
    struct stat s = {0};
    char path[PATH_MAX], buff[1024];
    ssize_t n = 0;

    if (strncmp(session->buffer, "GET_FILE_LIST", 13) == 0) {
        fprintf(stdout, "REQUEST: GET_FILE_LIST ");
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, session->buffer + 13, sizeof(struct client));
        printClientTuple(c);
        fprintf(stdout, "\n");
        listSetCurrentToStart(client_list);
        while ((client = listNext(client_list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                break;
            }
        }
        if (found) {
            listCreate(&file_list);
            rec_readdir(file_list, dirname, dirname);
            files = listGetLength(file_list);
            send(fd_client, "FILE_LIST", 9, 0);
            send(fd_client, &files, sizeof(unsigned int), 0);
            fprintf(stdout, "FILE_LIST %d ", files);
            listSetCurrentToStart(file_list);
            while ((file = listNext(file_list)) != NULL) {
                send(fd_client, file, sizeof(struct file_t), 0);
                printFileTuple(file);
            }
            fprintf(stdout, "\n");
        } else {
            free(c);
            send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
            fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
        }
    } else if (strncmp(session->buffer, "GET_FILE", 8) == 0) {
        fprintf(stdout, "REQUEST: GET_FILE ");
        offset = 8;
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, session->buffer + offset, sizeof(struct client));
        offset += sizeof(struct client);
        printClientTuple(c);
        fprintf(stdout, " ");
        listSetCurrentToStart(client_list);
        while ((client = listNext(client_list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                break;
            }
        }
        if (found) {
            file = malloc(sizeof(struct file_t));
            memcpy(file, session->buffer + offset, sizeof(struct file_t));
            offset += sizeof(struct file_t);
            printFileTuple(file);

            /* Construct real path.*/
            if (sprintf(path, "%s/%s", dirname, file->pathname) < 0) {
                fprintf(stderr, "\n%s:%d-sprintf error\n", __FILE__, __LINE__);
                exit(EXIT_FAILURE);
            }

            /* Get file statistics*/
            if (!stat(path, &s)) {
                if (file->version == s.st_ctim.tv_nsec) {
                    send(fd_client, "FILE_UP_TO_DATE", 15, 0);
                    fprintf(stdout, "FILE_UP_TO_DATE");
                } else {
                    send(fd_client, "FILE", 4, 0);
                    fprintf(stdout, "FILE ");

                    send(fd_client, file->pathname, strlen(file->pathname), 0);

                    send(fd_client, &s.st_ctim.tv_nsec, sizeof(long int), 0);
                    fprintf(stdout, "%ld ", s.st_ctim.tv_nsec);

                    if (S_ISDIR(s.st_mode)) {
                        send(fd_client, 0, sizeof(long int), 0);
                        fprintf(stdout, "0 ");
                    } else if (S_ISREG(s.st_mode)) {
                        send(fd_client, &s.st_size, sizeof(long int), 0);
                        fprintf(stdout, "%ld ", s.st_size);

                        /* Open file*/
                        if ((fd_file = open(path, O_RDONLY)) < 0) {
                            fprintf(stderr, "\n%s:%d-file %s open error: '%s'\n", __FILE__, __LINE__, path,
                                    strerror(errno));
                        }

                        /* Send whole file byte-byte.*/
                        if (s.st_size > 0) {
                            do {
                                if ((n = read(fd_file, buff, 1024)) > 0) {
                                    fprintf(stdout, ".");
                                    if (send(fd_client, buff, (size_t) n, 0) < 0) {
                                        fprintf(stderr, "\n%s:%d-fifo write error: '%s'\n", __FILE__, __LINE__,
                                                strerror(errno));
                                    }
                                }
                            } while (n == 1024);
                        }
                        fprintf(stdout, "\n");
                    }
                }
            } else {
                send(fd_client, "FILE_NOT_FOUND", 14, 0);
                fprintf(stderr, "\n%s:%d-[%s] stat error: '%s'\n", __FILE__, __LINE__, path, strerror(errno));
            }
            free(file);
        } else {
            free(c);
            send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
            fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
        }
    } else if (strncmp(session->buffer, "USER_ON", 7) == 0) {
        fprintf(stdout, "REQUEST: USER_ON ");
        found = false;
        c = malloc(sizeof(struct client));
        memcpy(c, session->buffer + 7, sizeof(struct client));
        printClientTuple(c);
        fprintf(stdout, "\n");
        listSetCurrentToStart(client_list);
        while ((client = listNext(client_list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                fprintf(stderr, "Duplicate entry!\n");
                break;
            }
        }
        if (!found) {
            if (!listInsert(client_list, c)) {
                fprintf(stderr, "Insert error!\n");
                free(c);
            } else {

                ////////////////////////////////////////////////////////////////////////////////////////////

                struct sockaddr_in address;
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = c->ip;
                address.sin_port = c->port;

                /* GET_FILE_LIST*/
                if ((fd_client = openConnection(address) > 0)) {
                    createSession(fd_client, address);
                    send(fd_client, "GET_FILE_LIST", 13, 0);
                    Client c1 = createClient(currentHostAddr.s_addr, htons(portNum));
                    send(fd_client, c1, sizeof(struct client), 0);
                    free(c1);
                    shutdown(fd_client, SHUT_WR);
                }
                ////////////////////////////////////////////////////////////////////////////////////////////


                ////////////////////////////////////////////////////////////////////////////////////////////
                /* GET_FILE*/
                if ((fd_client = openConnection(address)) > 0) {
                    createSession(fd_client, address);
                    send(fd_client, "GET_FILE", 8, 0);
                    Client c1 = createClient(currentHostAddr.s_addr, htons(portNum));
                    send(fd_client, c1, sizeof(struct client), 0);
                    free(c1);
                    file = malloc(sizeof(struct file_t));
                    strcpy(file->pathname, "TCP-socket-client-server-master/tcpechotimecli.c");
                    file->version = 232962259;
                    send(fd_client, file, sizeof(struct file_t), 0);
                    shutdown(fd_client, SHUT_WR);
                }
                ////////////////////////////////////////////////////////////////////////////////////////////

            }
        } else {
            free(c);
        }
    } else if (strncmp(session->buffer, "USER_OFF", 8) == 0) {
        fprintf(stdout, "REQUEST: USER_OFF\n");
        c = malloc(sizeof(struct client));
        memcpy(c, session->buffer + 8, sizeof(struct client));
        listSetCurrentToStart(client_list);
        while ((client = listNext(client_list)) != NULL) {
            if (c->ip == client->ip && c->port == client->port) {
                found = true;
                listSetCurrentToStart(client_list);
                if (listRemove(client_list, client)) {
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
    } else if (strncmp(session->buffer, "FILE_LIST", 9) == 0) {
        fprintf(stdout, "RESPONSE: FILE_LIST ");
        offset = 9;
        memcpy(&files, session->buffer + offset, sizeof(unsigned int));
        offset = offset + sizeof(unsigned int);
        fprintf(stdout, "%d ", files);
        for (int i = 0; i < files; i++) {
            file = malloc(sizeof(struct file_t));
            memcpy(file, session->buffer + offset, sizeof(struct file_t));
            offset = offset + sizeof(struct file_t);
            printFileTuple(file);
            //TODO: Insert into circular buffer, Mutex!!!
        }
        fprintf(stdout, "\n");
    } else if (strncmp(session->buffer, "LOG_ON_SUCCESS", 14) == 0) {
        fprintf(stdout, "RESPONSE: LOG_ON_SUCCESS\n");
    } else if (strncmp(session->buffer, "ALREADY_LOGGED_IN", 17) == 0) {
        fprintf(stdout, "RESPONSE: ALREADY_LOGGED_IN\n");
    } else if (strncmp(session->buffer, "CLIENT_LIST", 11) == 0) {
        int i;
        fprintf(stdout, "RESPONSE: CLIENT_LIST ");
        offset = 11;
        memcpy(&clients, session->buffer + offset, sizeof(unsigned int));
        offset = offset + sizeof(unsigned int);
        fprintf(stdout, "%d ", clients);
        if (clients > 0) {
            for (i = 0; i < clients; i++) {
                c = malloc(sizeof(struct client));
                memcpy(c, session->buffer + offset, sizeof(struct client));
                offset = offset + sizeof(struct client);
                if (listInsert(client_list, c)) {
                    printClientTuple(c);
                }
            }
        }
        fprintf(stdout, "\n");
    } else if (strncmp(session->buffer, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31) == 0) {
        fprintf(stdout, "RESPONSE: ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
    } else if (strncmp(session->buffer, "ERROR_NOT_REMOVED", 17) == 0) {
        fprintf(stdout, "RESPONSE: ERROR_NOT_REMOVED\n");
    } else if (strncmp(session->buffer, "LOG_OFF_SUCCESS", 15) == 0) {
        fprintf(stdout, "RESPONSE: LOG_OFF_SUCCESS\n");
    } else if (strncmp(session->buffer, "FILE_NOT_FOUND", 14) == 0) {
        fprintf(stdout, "RESPONSE: FILE_NOT_FOUND\n");
    } else if (strncmp(session->buffer, "FILE_UP_TO_DATE", 15) == 0) {
        fprintf(stdout, "RESPONSE: FILE_UP_TO_DATE\n");
    } else if (strncmp(session->buffer, "FILE", 4) == 0) {
        fprintf(stdout, "RESPONSE: FILE ");
        long int version = 0, bytes = 0;
        size_t fileNameLength = 0;
        char *filename = NULL;

        offset = 4;

        /* File name length*/
        memcpy(&fileNameLength, session->buffer + offset, sizeof(size_t));
        offset += sizeof(long int);
        fprintf(stdout, "%ld ", fileNameLength);

        /* File name*/
        memcpy(&filename, session->buffer + offset, fileNameLength);
        offset += sizeof(long int);
        fprintf(stdout, "%s ", filename);

        /* File version*/
        memcpy(&version, session->buffer + offset, sizeof(long int));
        offset += sizeof(long int);
        fprintf(stdout, "%ld ", version);

        memcpy(&bytes, session->buffer + offset, sizeof(long int));
        offset += sizeof(long int);
        fprintf(stdout, "%ld ", bytes);

        if (sprintf(path, "%s/%s_%d/%s", dirname, inet_ntoa(session->address.sin_addr),
                    ntohs(session->address.sin_port), filename) < 0) {
            fprintf(stderr, "\n%s:%d-sprintf error\n", __FILE__, __LINE__);
        }

        char *pch = NULL, ch;
        unsigned long int off = 0;

        /* Make dirs if not exists.*/
        pch = strchr(path, '/');
        while (pch != NULL) {
            off = pch - path + 1;
            ch = path[offset];
            path[offset] = '\0';
            printf("\nTry with: [%s]\n", path);
            if (stat(path, &s)) {
                mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
            }
            path[offset] = ch;
            pch = strchr(pch + 1, '/');
        }


        if ((fd_file = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR)) < 0) {
            fprintf(stderr, "\n%s:%d-file '%s' open error: '%s'\n", __FILE__, __LINE__, path, strerror(errno));
        }

/*        while (bytes > 0) {

            if ((bytes = read(r_fd_fifo, buff, bytes > 1024 ? 1024 : b)) < 0) {
                fprintf(stderr, "\n%s:%d-fifo read error: '%s'\n", __FILE__, __LINE__, strerror(errno));
            }

            r_bytes += bytes;
            if (write(r_fd_file, buffer, (size_t) bytes) == -1) {
                fprintf(stderr, "\n%s:%d-file write error: '%s'\n", __FILE__, __LINE__, strerror(errno));
            }

            b -= bytes;

        };*/

        fprintf(stdout, "\n");
    } else if (strncmp(session->buffer, "GET_CLIENTS", 11) == 0) {
        fprintf(stdout, "REQUEST: GET_CLIENTS\n");
        c = malloc(sizeof(struct client));
        memcpy(c, session->buffer + 11, sizeof(struct client));
        clients = listGetLength(client_list) - 1;
        send(fd_client, "CLIENT_LIST", 11, 0);
        fprintf(stdout, "CLIENT_LIST ");
        send(fd_client, &clients, sizeof(unsigned int), 0);
        fprintf(stdout, "%d ", clients);
        listSetCurrentToStart(client_list);
        while ((client = listNext(client_list)) != NULL) {
            if (!(c->ip == client->ip && c->port == client->port)) {
                send(fd_client, client, sizeof(struct client), 0);
                printClientTuple(client);
            }
        }
        fprintf(stdout, "\n");
        free(c);
    } else if (strncmp(session->buffer, "UNKNOWN_COMMAND", 15) == 0) {
        fprintf(stdout, "RESPONSE: UNKNOWN_COMMAND\n");
    } else {
        fprintf(stderr, "UNKNOWN_COMMAND\n");
    }
}

void destroySession(int fd) {
    FD_CLR(fd, &set);
    if (fd == lfd) {
        lfd--;
    }
    close(fd);
    free(s[fd].buffer);
    s[fd].buffer = NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr *new_client_in_addr_ptr = NULL;
    struct sockaddr *server_ptr = NULL, *client_ptr = NULL, *listen_ptr = NULL;
    struct sockaddr_in server_in_addr, client_in_addr, listen_in_addr;
    struct sockaddr_in new_client_in_addr;
    struct hostent *hostEntry = NULL;
    struct timespec timeout;
    struct sigaction sa;
    int opt = 1, fd_listen = 0, fd_client = 0, fd_new_client = 0, activity = 0, fd_active = 0;
    char hostBuffer[256], *currentHostStrIp = NULL;
    void *rcv_buffer = NULL;

    ssize_t bytes = 0;
    fd_set read_fds;
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

    for (int i = 0; i < FD_SETSIZE; i++) {
        s[i].buffer = NULL;
        s[i].bytes = 0;
        s[i].chunks = 0;
    }

    /* LOG_ON*/

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(serverIP);
    address.sin_port = htons(serverPort);


    int skata = 0;
    if ((skata = openConnection(address) > 0)) {
        if (!createSession(skata, address)) {
            fprintf(stderr, "HOST_IS_TOO_BUSY");
        }
        send(skata, "LOG_ON", 6, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(skata, c, sizeof(struct client), 0);
        free(c);
        shutdown(skata, SHUT_WR);
    }


    if ((fd_client = openConnection(address) > 0)) {
        if (!createSession(fd_client, address)) {
            fprintf(stderr, "HOST_IS_TOO_BUSY");
        }
        send(fd_client, "LOG_ON", 6, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    /* GET_CLIENTS*/
    if ((fd_client = openConnection(address)) > 0) {
        createSession(fd_client, address);
        send(fd_client, "GET_CLIENTS", 11, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    /* Create circular buffer.*/
    createCircularBuffer(&pool);

    /****************************************************************************************************/

    fprintf(stdout, "::Waiting for connections on %s:%d::\n", currentHostStrIp, portNum);
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
                    fprintf(stdout, "::%ld bytes were transferred into %d different chunks on socket %d::\n",
                            s[i].bytes - 1,
                            s[i].chunks, i);
                    fprintf(stdout, COLOR"%s\n"RESET"\n", (char *) s[i].buffer);
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

                    fprintf(stdout, "::Accept new client (%s:%d) on socket %d::\n",
                            inet_ntoa(new_client_in_addr.sin_addr),
                            ntohs(new_client_in_addr.sin_port),
                            fd_new_client);

                    if (!createSession(fd_new_client, new_client_in_addr)) {
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
                            requestHandler(fd_active, &s[fd_active]);
                        }
                        shutdown(fd_active, SHUT_WR);
                        destroySession(fd_active);
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
    if ((fd_client = openConnection(address) > 0)) {
        send(fd_client, "LOG_OFF", 7, 0);
        c = createClient(currentHostAddr.s_addr, htons(portNum));
        send(fd_client, c, sizeof(struct client), 0);
        free(c);
        shutdown(fd_client, SHUT_WR);
    }

    //TODO: pthread_joins

    free(currentHostStrIp);
    free(rcv_buffer);

    listDestroy(&client_list);

    pthread_cond_destroy(&cond_nonempty);
    pthread_cond_destroy(&cond_nonfull);

    pthread_mutex_destroy(&mtx_client_list);
    pthread_mutex_destroy(&mtx_pool);

    destroyCircularBuffer(&pool);
    return 0;
}
