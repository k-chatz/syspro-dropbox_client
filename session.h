#ifndef SESSION_H
#define SESSION_H

#include <netdb.h>

typedef struct session {
    size_t bytes;
    void *buffer;
    int chunks;
    struct sockaddr_in address;
} Session;

extern Session s[FD_SETSIZE];

int createSession(int fd, int *lfd, struct sockaddr_in address, fd_set *set);

void destroySession(int fd, int *lfd, fd_set *set);

void initSessionArray();

#endif
