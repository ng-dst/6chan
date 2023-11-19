#include "../include/model.h"

static List* client_list;
static List* message_history;

List* getMessageHistory() {
    return message_history;
}

List* initMessageHistory() {
    message_history = list();
    return message_history;
}

void destroyMessageHistory() {
    for (Item* i = message_history->head; i != NULL; i = i->next) {
        Message* m = (Message*) i->data;
        if (m->buf) free(m->buf);
    }
    list_delete(message_history);
}

List* getClientList() {
    return client_list;
}

List* initClientList() {
    client_list = list();
    return client_list;
}

void destroyClientList() {
    list_delete(client_list);
}

void printLastError() {
    fprintf(stderr, "WinAPI error: %lu\r\n", GetLastError());
}

void printLastWSAError() {
    fprintf(stderr, "WSA error: %d\r\n", WSAGetLastError());
}

