#include <malloc.h>
#include <zconf.h>
#include "session.h"

int createSession(int fd, int *lfd, struct sockaddr_in address, fd_set *set) {
    if (fd <= FD_SETSIZE) {
        s[fd].buffer = malloc(1);
        s[fd].bytes = 1;
        s[fd].chunks = 0;
        s[fd].address = address;
        FD_SET(fd, set);
        if (fd > *lfd) {
            *lfd = fd;
        }
        return 1;
    } else {
        return 0;
    }
}

void destroySession(int fd, int *lfd, fd_set *set) {
    FD_CLR(fd, set);
    if (fd == *lfd) {
        *lfd--;
    }
    close(fd);
    free(s[fd].buffer);
    s[fd].buffer = NULL;
}

void initSessionArray() {
    for (int i = 0; i < FD_SETSIZE; i++) {
        s[i].buffer = NULL;
        s[i].bytes = 0;
        s[i].chunks = 0;
    }
}
