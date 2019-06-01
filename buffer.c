#include <strings.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include "buffer.h"

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

void place(pool_t *pool, in_addr_t ip, in_port_t port, char *pathname, long version) {
    pthread_mutex_lock(&mtx_pool);
    while (pool->count >= bufferSize) {
        printf(">> Found Buffer Full \n");
        pthread_cond_wait(&condNonFull, &mtx_pool);
    }
    pool->end = (pool->end + 1) % bufferSize;
    pool->buffer[pool->end].ip = ip;
    pool->buffer[pool->end].port = port;
    strcpy(pool->buffer[pool->end].pathname, pathname);
    pool->buffer[pool->end].version = version;
    pool->count++;
    pthread_mutex_unlock(&mtx_pool);
}

circular_buffer_t obtain(pool_t *pool) {
    circular_buffer_t data;
    pthread_mutex_lock(&mtx_pool);
    while (pool->count <= 0) {
        printf(">> Found Buffer Empty \n");
        pthread_cond_wait(&condNonEmpty, &mtx_pool);
    }
    data.ip = pool->buffer[pool->start].ip;
    data.port = pool->buffer[pool->start].port;
    strcpy(data.pathname, pool->buffer[pool->start].pathname);
    data.version = pool->buffer[pool->start].version;
    pool->start = (pool->start + 1) % bufferSize;
    pool->count--;
    pthread_mutex_unlock(&mtx_pool);
    return data;
}