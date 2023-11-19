#ifndef LAB6_MODEL_H
#define LAB6_MODEL_H

#include <Winsock2.h>
#include "../../utils/include/list.h"

#define FILE_NAME_LEN 32

#define MSG_TYPE_SYNC 0
#define MSG_TYPE_MSG 1
#define MSG_TYPE_FILE 2
#define MSG_TYPE_QUIT 3
#define MSG_TYPE_LOADFILE 4

typedef struct Client {
    SOCKET sock;
    DWORD id;
    char ip[16];
    WORD port;
} Client;

// messages and files are stored in RAM
typedef struct Message {
    DWORD msg_id;
    DWORD src_id;
    DWORD msg_len;
    BYTE msg_type;
    char file_name[FILE_NAME_LEN];
    char *buf;
} Message;

List* getMessageHistory();
List* initMessageHistory();
void destroyMessageHistory();

List* getClientList();
List* initClientList();
void destroyClientList();

void printLastError();
void printLastWSAError();

#endif //LAB6_MODEL_H
