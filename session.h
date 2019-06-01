#ifndef DROPBOX_CLIENT_SESSION_H
#define DROPBOX_CLIENT_SESSION_H

#include <netdb.h>

typedef struct session {
    size_t bytes;
    void *buffer;
    int chunks;
    struct sockaddr_in address;
} Session;

#endif //DROPBOX_CLIENT_SESSION_H
