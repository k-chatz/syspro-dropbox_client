#ifndef CONNECTION_H
#define CONNECTION_H

extern int lfd;

int openConnection(struct sockaddr_in address);

void fdMonitor(fd_set *set, fd_set *read_fds, sigset_t *oldset);

void fdActivityHandler(fd_set *read_fds, void *buffer, size_t bufferSize);

#endif
