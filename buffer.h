#ifndef BUFFER_H
#define BUFFER_H

#include <netinet/in.h>

extern pthread_mutex_t mtx_pool;
extern int bufferSize;
extern pthread_cond_t condNonEmpty, condNonFull;

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

void createCircularBuffer(pool_t *pool);

void destroyCircularBuffer(pool_t *pool);

void place(pool_t *pool, in_addr_t ip, in_port_t port, char *pathname, long version);

circular_buffer_t obtain(pool_t *pool);

#endif
