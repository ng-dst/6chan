#ifndef LAB6_MODEL_H
#define LAB6_MODEL_H

#include <Winsock2.h>
#include "../../utils/include/list.h"

#define FILE_NAME_LEN 32

#define MSG_TYPE_SYNC 0
#define MSG_TYPE_MSG 1
#define MSG_TYPE_FILE 2
#define MSG_TYPE_LOADFILE 3


typedef struct Client {
#ifdef USE_PIPES
    HANDLE sock;
#else
    SOCKET sock;                        // Client socket
    char ip[16];                        // IP in decimal notation
#endif
    DWORD id;                           // Client #id
    WORD port;                          // Port number
} Client;


typedef struct Message {
    DWORD msg_id;                       // Message #id
    DWORD src_id;                       // Sender #id
    DWORD msg_len;                      // Length of buffer
    BYTE msg_type;                      // Type of message (in #define)
    char file_name[FILE_NAME_LEN];      // File name (if message is a file)
    char *buf;                          // Message buffer
    SYSTEMTIME timestamp;               // Time stamp of message
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
