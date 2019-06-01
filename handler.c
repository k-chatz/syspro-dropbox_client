#include <pthread.h>
#include <string.h>
#include <malloc.h>
#include <sys/socket.h>
#include <zconf.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include "handler.h"
#include "list.h"
#include "client.h"
#include "file.h"
#include "buffer.h"

#define LIGHT_BLUE "\x1b[94m"
#define RESET "\x1B[0m"

void mkdirs(char *path) {
    struct stat s = {0};
    char *pch = NULL, ch;
    unsigned long int i = 0;
    pch = strchr(path, '/');
    while (pch != NULL) {
        i = pch - path + 1;
        ch = path[i];
        path[i] = '\0';
        if (stat(path, &s)) {
            mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
        }
        path[i] = ch;
        pch = strchr(pch + 1, '/');
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

void handle_req_get_file_list(int fd_client, Session *session) {
    unsigned int files = 0;
    int found = 0;

    Client c = NULL, client = NULL;
    List file_list = NULL;
    file_t_ptr file = NULL;
    c = malloc(sizeof(struct client));
    memcpy(c, session->buffer + 13, sizeof(struct client));
    printClientTuple(c);
    fprintf(stdout, "\n");

    pthread_mutex_lock(&mtx_client_list);
    listSetCurrentToStart(client_list);
    while ((client = listNext(client_list)) != NULL) {
        if (c->ip == client->ip && c->port == client->port) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mtx_client_list);


    if (found) {
        listCreate(&file_list);
        rec_readdir(file_list, dirname, dirname);
        files = listGetLength(file_list);
        send(fd_client, "FILE_LIST", 9, 0);
        send(fd_client, &files, sizeof(unsigned int), 0);
        fprintf(stdout, "FILE_LIST %d ", files);

        pthread_mutex_lock(&mtx_client_list);
        listSetCurrentToStart(file_list);
        while ((file = listNext(file_list)) != NULL) {
            send(fd_client, file, sizeof(struct file_t), 0);
            printFileTuple(file);
        }
        pthread_mutex_unlock(&mtx_client_list);

        fprintf(stdout, "\n");
    } else {
        free(c);
        send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
        fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
    }
}

void handle_req_get_file(int fd_client, Session *session) {
    Client c = NULL, client = NULL;
    int found = 0, offset = 0, fd_file = 0;
    file_t_ptr file = NULL;
    struct stat s = {0};
    char path[PATH_MAX], buff[1024];
    ssize_t n = 0;

    offset = 8;
    found = false;
    c = malloc(sizeof(struct client));
    memcpy(c, session->buffer + offset, sizeof(struct client));
    offset += sizeof(struct client);
    printClientTuple(c);
    fprintf(stdout, " ");

    pthread_mutex_lock(&mtx_client_list);
    listSetCurrentToStart(client_list);
    while ((client = listNext(client_list)) != NULL) {
        if (c->ip == client->ip && c->port == client->port) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mtx_client_list);

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
                size_t fileNameLength = strlen(file->pathname);
                send(fd_client, "FILE", 4, 0);
                fprintf(stdout, "\nFILE ");

                /* File name length*/
                send(fd_client, &fileNameLength, sizeof(size_t), 0);
                fprintf(stdout, "%ld ", fileNameLength);

                /* File name*/
                send(fd_client, file->pathname, fileNameLength, 0);
                fprintf(stdout, "%s ", file->pathname);

                /* File version*/
                send(fd_client, &s.st_ctim.tv_nsec, sizeof(long int), 0);
                fprintf(stdout, "%ld ", s.st_ctim.tv_nsec);

                if (S_ISDIR(s.st_mode)) {

                    /* Number of bytes*/
                    send(fd_client, 0, sizeof(long int), 0);
                    fprintf(stdout, "0 ");
                } else if (S_ISREG(s.st_mode)) {

                    /* Number of bytes*/
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
}

void handle_req_user_on(int fd_client, Session *session) {
    Client c = NULL, client = NULL;
    int found = 0;
    found = false;
    c = malloc(sizeof(struct client));
    memcpy(c, session->buffer + 7, sizeof(struct client));
    printClientTuple(c);
    fprintf(stdout, "\n");

    pthread_mutex_lock(&mtx_client_list);
    listSetCurrentToStart(client_list);
    while ((client = listNext(client_list)) != NULL) {
        if (c->ip == client->ip && c->port == client->port) {
            found = true;
            fprintf(stderr, "Duplicate entry!\n");
            break;
        }
    }
    pthread_mutex_unlock(&mtx_client_list);

    if (!found) {

        pthread_mutex_lock(&mtx_client_list);
        int x = listInsert(client_list, c);
        pthread_mutex_unlock(&mtx_client_list);

        if (!x) {
            fprintf(stderr, "Insert error!\n");
            free(c);
        } else {
            printf(LIGHT_BLUE"\nMain thread (handle_req_user_on) place NEW connection %d ...\n"RESET, ntohs(c->port));
            place(&pool, c->ip, c->port, "", 0);
            pthread_cond_signal(&condNonEmpty);
        }
    } else {
        free(c);
    }
}

void handle_req_user_off(int fd_client, Session *session) {
    Client c = NULL, client = NULL;
    int found = 0;
    c = malloc(sizeof(struct client));
    memcpy(c, session->buffer + 8, sizeof(struct client));

    pthread_mutex_lock(&mtx_client_list);
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
    pthread_mutex_unlock(&mtx_client_list);

    if (!found) {
        send(fd_client, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31, 0);
        fprintf(stderr, "ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
    }
    free(c);
}

void handle_req_get_clients(int fd_client, Session *session) {
    unsigned int clients = 0;
    Client c = NULL, client = NULL;
    c = malloc(sizeof(struct client));
    memcpy(c, session->buffer + 11, sizeof(struct client));

    pthread_mutex_lock(&mtx_client_list);
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
    pthread_mutex_unlock(&mtx_client_list);

    fprintf(stdout, "\n");
    free(c);
}

void handle_res_get_file_list(int fd_client, Session *session) {
    unsigned int files = 0;
    int offset = 9;
    file_t_ptr file = NULL;
    memcpy(&files, session->buffer + offset, sizeof(unsigned int));
    offset = offset + sizeof(unsigned int);
    fprintf(stdout, "%d ", files);
    for (int i = 0; i < files; i++) {
        file = malloc(sizeof(struct file_t));
        memcpy(file, session->buffer + offset, sizeof(struct file_t));
        offset = offset + sizeof(struct file_t);
        printFileTuple(file);
        printf(LIGHT_BLUE"\nMain thread (handle_res_get_file_list) place %d - %s - %ld ...\n"RESET,
               ntohs(session->address.sin_port), file->pathname, file->version);
        place(&pool, session->address.sin_addr.s_addr, session->address.sin_port, file->pathname, file->version);
        pthread_cond_signal(&condNonEmpty);
    }
    fprintf(stdout, "\n");
}

void handle_res_get_file(int fd_client, Session *session) {
    int offset = 4, fd_file = 0;
    char path[PATH_MAX];
    char filename[PATH_MAX];
    long int version = 0;
    size_t bytes = 0;
    size_t fileNameLength = 0;

    /* File name length*/
    memcpy(&fileNameLength, session->buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);
    fprintf(stdout, "%ld ", fileNameLength);

    /* File name*/
    memcpy(&filename, session->buffer + offset, fileNameLength);
    offset += fileNameLength;
    filename[fileNameLength] = '\0';
    fprintf(stdout, "%s ", filename);

    /* File version*/
    memcpy(&version, session->buffer + offset, sizeof(long int));
    offset += sizeof(long int);
    fprintf(stdout, "%ld ", version);

    /* Number of bytes*/
    memcpy(&bytes, session->buffer + offset, sizeof(long int));
    offset += sizeof(long int);
    fprintf(stdout, "%ld ", bytes);

    if (sprintf(path, "%s/%d%d/%s", dirname, session->address.sin_addr.s_addr,
                session->address.sin_port, filename) < 0) {
        fprintf(stderr, "\n%s:%d-sprintf error\n", __FILE__, __LINE__);
    }

    /* Make dirs if not exists.*/
    mkdirs(path);

    //todo: if is file
    if (1) {
        /* Open file for write*/
        if ((fd_file = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR)) < 0) {
            fprintf(stderr, "\n%s:%d-file '%s' open error: '%s'\n", __FILE__, __LINE__, path, strerror(errno));
        }

        if (write(fd_file, session->buffer + offset, bytes) == -1) {
            fprintf(stderr, "\n%s:%d-file write error: '%s'\n", __FILE__, __LINE__, strerror(errno));
        }
    }

    fprintf(stdout, "\n");
}

void handle_res_get_clients(int fd_client, Session *session) {
    unsigned int clients = 0;
    Client c = NULL;
    int offset = 0, i = 0;
    offset = 11;
    memcpy(&clients, session->buffer + offset, sizeof(unsigned int));
    offset = offset + sizeof(unsigned int);
    fprintf(stdout, "%d ", clients);
    if (clients > 0) {
        for (i = 0; i < clients; i++) {
            c = malloc(sizeof(struct client));
            memcpy(c, session->buffer + offset, sizeof(struct client));
            offset = offset + sizeof(struct client);

            pthread_mutex_lock(&mtx_client_list);
            if (listInsert(client_list, c)) {
                printClientTuple(c);
            }
            pthread_mutex_unlock(&mtx_client_list);

            printf(LIGHT_BLUE"\nMain thread (handle_res_get_clients) place NEW connection %d ...\n"RESET,
                   ntohs(c->port));
            place(&pool, c->ip, c->port, "", 0);

            pthread_cond_signal(&condNonEmpty);
        }
    }
    fprintf(stdout, "\n");
}

/**
 * Handle requests & responses.*/
void handler(int fd_client, Session *session) {
    if (strncmp(session->buffer, "GET_FILE_LIST", 13) == 0) {
        fprintf(stdout, "\n\nREQUEST: GET_FILE_LIST ");
        handle_req_get_file_list(fd_client, session);
    } else if (strncmp(session->buffer, "GET_FILE", 8) == 0) {
        fprintf(stdout, "\n\nREQUEST: GET_FILE ");
        handle_req_get_file(fd_client, session);
    } else if (strncmp(session->buffer, "USER_ON", 7) == 0) {
        fprintf(stdout, "\n\nREQUEST: USER_ON ");
        handle_req_user_on(fd_client, session);
    } else if (strncmp(session->buffer, "USER_OFF", 8) == 0) {
        fprintf(stdout, "\n\nREQUEST: USER_OFF\n");
        handle_req_user_off(fd_client, session);
    } else if (strncmp(session->buffer, "FILE_LIST", 9) == 0) {
        fprintf(stdout, "\n\nRESPONSE: FILE_LIST ");
        handle_res_get_file_list(fd_client, session);
    } else if (strncmp(session->buffer, "LOG_ON_SUCCESS", 14) == 0) {
        fprintf(stdout, "\n\nRESPONSE: LOG_ON_SUCCESS\n");
    } else if (strncmp(session->buffer, "ALREADY_LOGGED_IN", 17) == 0) {
        fprintf(stdout, "\n\nRESPONSE: ALREADY_LOGGED_IN\n");
    } else if (strncmp(session->buffer, "CLIENT_LIST", 11) == 0) {
        fprintf(stdout, "\n\nRESPONSE: CLIENT_LIST ");
        handle_res_get_clients(fd_client, session);
    } else if (strncmp(session->buffer, "ERROR_IP_PORT_NOT_FOUND_IN_LIST", 31) == 0) {
        fprintf(stdout, "\n\nRESPONSE: ERROR_IP_PORT_NOT_FOUND_IN_LIST\n");
    } else if (strncmp(session->buffer, "ERROR_NOT_REMOVED", 17) == 0) {
        fprintf(stdout, "\n\nRESPONSE: ERROR_NOT_REMOVED\n");
    } else if (strncmp(session->buffer, "LOG_OFF_SUCCESS", 15) == 0) {
        fprintf(stdout, "\n\nRESPONSE: LOG_OFF_SUCCESS\n");
    } else if (strncmp(session->buffer, "FILE_NOT_FOUND", 14) == 0) {
        fprintf(stdout, "\n\nRESPONSE: FILE_NOT_FOUND\n");
    } else if (strncmp(session->buffer, "FILE_UP_TO_DATE", 15) == 0) {
        fprintf(stdout, "\n\nRESPONSE: FILE_UP_TO_DATE\n");
    } else if (strncmp(session->buffer, "FILE", 4) == 0) {
        fprintf(stdout, "\n\nRESPONSE: FILE ");
        handle_res_get_file(fd_client, session);
    } else if (strncmp(session->buffer, "GET_CLIENTS", 11) == 0) {
        fprintf(stdout, "\n\nREQUEST: GET_CLIENTS\n");
        handle_req_get_clients(fd_client, session);
    } else if (strncmp(session->buffer, "UNKNOWN_COMMAND", 15) == 0) {
        fprintf(stdout, "\n\nRESPONSE: UNKNOWN_COMMAND\n");
    } else {
        fprintf(stderr, "\n\nUNKNOWN_COMMAND\n");
    }
}
