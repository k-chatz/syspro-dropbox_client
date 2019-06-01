#ifndef FILE_H
#define FILE_H

typedef struct file_t {
    char pathname[128];
    long int version;
} *file_t_ptr;

void printFileTuple(file_t_ptr file);

#endif
