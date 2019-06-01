#include <stdio.h>
#include "file.h"

void printFileTuple(file_t_ptr file) {
    fprintf(stdout, "<%s, %ld> ", file->pathname, file->version);
}
