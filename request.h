#ifndef REQUEST_H
#define REQUEST_H

#include "file.h"

extern int lfd;
extern fd_set set;
extern struct in_addr currentHostAddr;
extern uint16_t portNum, serverPort;

void req_get_file_list(in_addr_t ip, in_port_t port);

void req_get_file(in_addr_t ip, in_port_t port, file_t_ptr file);

void req_log_on(in_addr_t ip, in_port_t port);

void req_get_clients(in_addr_t ip, in_port_t port);

void req_log_off(in_addr_t ip, in_port_t port);

#endif
