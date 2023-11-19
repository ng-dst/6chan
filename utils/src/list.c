/*
 *      list data structure
 *
 *      types described:
 *          Item - pointer to data, pointer to next item
 *          List - length, pointer to first item (NULL if empty)
 *
 *      data type is (void *) - this means that ANY data could be stored there!
 *
 *      functions:
 *          list()  returns an empty list
 *
 *      methods:
 *          get()
 *          list_getitem()
 *          pop()
 *          pop_next()
 *          list_push()
 *          append()
 *          insert()
 *          print()
 *          clear()
 *          clear_item()
 *          delete()
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "../include/list.h"


List* list()
{
    List* empty_list = (List*) malloc(sizeof(List));
    empty_list->length = 0;
    empty_list->head = NULL;
    empty_list->tail = NULL;
    return empty_list;
}

bool list_push(List* given_list, void* x)
{
    Item* ptr = (Item*) malloc(sizeof(Item));
    if (!ptr) return false;
    ptr->data = x;
    ptr->next = given_list->head;

    given_list->head = ptr;
    given_list->length++;

    if (given_list->length == 1)
        given_list->tail = given_list->head;
    return true;
}

Item* list_getitem(List* given_list, size_t index)
{
    if (index >= given_list->length)
        return NULL;
    if (index+1 == given_list->length)
        return given_list->tail;

    Item* item = given_list->head;
    for (size_t i = 0; i < index; i++)
        item = item->next;

    return item;
}

void* list_get(List* given_list, size_t index)
{
    return list_getitem(given_list, index)->data;
}

void* list_pop(List* given_list, size_t index)
{
    void* x = NULL;
    if (given_list->length <= index || given_list->head == NULL)
        return NULL;

    if (index == 0)
    {
        Item* prev = given_list->head;
        x = prev->data;
        given_list->head = given_list->head->next;
        if (!given_list->head)
            given_list->tail = NULL;
        free(prev);
    }
    else if (index+1 < given_list->length)
    {
        Item* prev = list_getitem(given_list, index - 1);
        Item* to_pop = prev->next;
        x = to_pop->data;

        prev->next = to_pop->next;
        free(to_pop);
    }
    else if (index+1 == given_list->length && given_list->length >= 2)
    {
        Item* prev = list_getitem(given_list, given_list->length - 2);
        x = given_list->tail->data;
        given_list->tail = prev;
        free(prev->next);
        prev->next = NULL;
    }

    given_list->length--;
    return x;
}

void* list_popnext(List* given_list, Item* prev)
{
    if (!prev)
        return list_pop(given_list, 0);

    if (prev->next == given_list->tail)
        given_list->tail = prev;

    Item* to_pop = prev->next;
    void* x = to_pop->data;
    prev->next = to_pop->next;
    free(to_pop);

    given_list->length--;
    return x;
}

bool list_append(List* given_list, void* x)
{
    if (given_list->length)
    {
        Item *ptr = (Item *) malloc(sizeof(Item));
        if (!ptr) return false;

        ptr->data = x;
        ptr->next = NULL;

        Item *last = given_list->tail;
        last->next = ptr;
        given_list->tail = ptr;
        given_list->length++;

        return true;
    }
    else
    {
        return list_push(given_list, x);
    }
}

bool list_insert(List* given_list, size_t index, void* x)
{
    if (index+1 > given_list->length)
    {
        list_append(given_list, x);
        return false;
    }
    if (!index)
    {
        return list_push(given_list, x);
    }

    Item* ptr = (Item*) malloc(sizeof(Item));
    if (!ptr) return false;
    ptr->data = x;

    Item* prev = list_getitem(given_list, index - 1);
    ptr->next = prev->next;
    prev->next = ptr;

    given_list->length++;
    return true;
}

void list_clear(List* given_list, size_t index)
{
    void* x = list_get(given_list, index);
    free(x);
}

void list_clearitem(Item* given_item)
{
    void* x = given_item->data;
    free(x);
}

void list_print(List* given_list, const char* format)
{
    Item* curr = given_list->head;
    for (size_t i = 0; i < given_list->length; i++)
    {
        printf(format, *((int*) curr->data));
        curr = curr->next;
    }
}

void list_delete(List* given_list)
{
    while (given_list->head)
    {
        list_clear(given_list, 0);
        list_pop(given_list, 0);
    }
    free(given_list);
}