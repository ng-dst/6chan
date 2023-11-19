#ifndef LAB5_LIST_H
#define LAB5_LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


typedef struct Item
{
    void* data;
    struct Item* next;
} Item;

typedef struct List
{
    size_t length;
    Item* head;
    Item* tail;
} List;


List* list();

Item* list_getitem(List* given_list, size_t index);
void* list_get(List* given_list, size_t index);
void* list_pop(List* given_list, size_t index);
void* list_popnext(List* given_list, Item* prev);

bool list_push(List* given_list, void* x);
bool list_append(List* given_list, void* x);
bool list_insert(List* given_list, size_t index, void* x);

void list_clear(List* given_list, size_t index);
void list_clearitem(Item* given_item);

void list_print(List* given_list, const char* format);
void list_delete(List* given_list);

#endif //LAB5_LIST_H
