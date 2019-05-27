#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"

typedef struct Node *nodePtr;

typedef void *pointer;

struct List {
    pointer identifier;
    unsigned int length;
    nodePtr start;
    nodePtr current;
};

struct Node {
    nodePtr right, left;
    pointer data;
};


/***Private functions***/

int l_existsNode(nodePtr node) {
    return node != NULL;
}

int l_isFirst(nodePtr target) {
    return target->left == NULL;
}

int l_isEmpty(List list) {
    return list->start == NULL;
}

nodePtr l_createNode() {
    nodePtr node = (nodePtr) malloc(sizeof(struct Node));
    if (node != NULL) {
        node->right = NULL;
        node->left = NULL;
        return node;
    } else
        return NULL;
}

bool listExists(List *list) {
    return (*list != NULL);
}


/***Public functions***/

void listCreate(List *list) {
    assert(*list == NULL);
    *list = (List) malloc(sizeof(struct List));
    if ((*list) != NULL) {
        (*list)->length = 0;
        (*list)->start = NULL;
        (*list)->current = NULL;
    }
}

pointer listGetIdentifier(List list) {
    return list->identifier;
}

bool listInsert(List list, pointer data) {
    assert(list != NULL);
    assert(data != NULL);
    nodePtr newNode = l_createNode();
    if (newNode != NULL) {
        newNode->data = data;
        newNode->right = list->start;
        newNode->left = NULL;
        if (!l_isEmpty(list))
            list->start->left = newNode;
        list->length++;
        list->start = newNode;
        list->current = newNode;
        return true;
    }
    return false;
}

bool listRemove(List list, pointer data) {
    assert(list != NULL);
    list->current = list->start;
    nodePtr next = NULL;
    nodePtr point = list->start;
    if (point != NULL) {
        do {
            next = point->right;
            if (point->data == data) {
                if (point->left == NULL) {
                    //First element
                    list->start = list->current = point->right;
                    if (point->right != NULL) {
                        point->right->left = NULL;
                    }
                } else {
                    //Others
                    point->left->right = point->right;
                    if (point->right != NULL) {
                        point->right->left = point->left;
                    }
                }
                list->length--;
                free(point);
                return true;
            }
        } while ((point = next) != NULL);
    }
    return false;
}

void listSetCurrentToStart(List list) {
    assert(list != NULL);
    list->current = list->start;
}

unsigned int listGetLength(List list) {
    assert(list != NULL);
    return list->length;
}

pointer listNext(List list) {
    assert(list != NULL);
    nodePtr tmp = NULL;
    if (list->current != NULL) {
        tmp = list->current;
        list->current = list->current->right;
        return tmp->data;
    }
    list->current = list->start;
    return NULL;
}

void listDestroy(List *list) {
    assert(*list != NULL);
    nodePtr next = NULL;
    nodePtr point = (*list)->start;
    if (point != NULL) {
        do {
            next = point->right;
            free(point);
        } while ((point = next) != NULL);
    }
    free(*list);
}
