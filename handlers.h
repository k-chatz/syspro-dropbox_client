#ifndef HANDLERS_H
#define HANDLERS_H

#include "session.h"
#include "list.h"
#include "buffer.h"

extern char *dirname, *serverIP;
extern pthread_mutex_t mtx_client_list, mtx_pool;
extern pthread_cond_t condNonEmpty, condNonFull;
extern List client_list;
extern pool_t pool;

void handle_req_get_file_list(int fd_client, Session *session);

void handle_req_get_file(int fd_client, Session *session);

void handle_req_user_on(int fd_client, Session *session);

void handle_req_user_off(int fd_client, Session *session);

void handle_req_get_clients(int fd_client, Session *session);

void handle_res_get_file_list(int fd_client, Session *session);

void handle_res_get_clients(int fd_client, Session *session);

#endif