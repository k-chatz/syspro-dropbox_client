#ifndef LIST_H
#define LIST_H

#include <stdbool.h>
#include <stdio.h>

typedef struct List *List;

void listCreate(List *list);

void *listGetIdentifier(List);

bool listInsert(List, void *);

bool listRemove(List, void *);

void listSetCurrentToStart(List list);

unsigned int listGetLength(List list);

void *listNext(List);

void listDestroy(List *list);

#endif
