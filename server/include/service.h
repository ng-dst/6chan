#ifndef LAB6_SERVICE_H
#define LAB6_SERVICE_H

#include <winsock2.h>
#include "model.h"

#ifdef USE_PIPES
#define SOCKET HANDLE
#endif

void getIpPort(SOCKET sock, char *ip, WORD *port);

WINBOOL sendMessageToClient(Client* c, Message* msg);
WINBOOL sendFileToClient(Client* c, Message* msg);

Message* parseMsgFromClient(const char* buf, int len, SOCKET sock);
int acceptFileFromClient(Message* msg, SOCKET sock);

#endif //LAB6_SERVICE_H
