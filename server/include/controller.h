#ifndef LAB6_CONTROLLER_H
#define LAB6_CONTROLLER_H

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "model.h"


WINBOOL startServer(const char* ip, const char* port);
void closeServer(ADDRINFOA *fullserv, SOCKET sock);

void startAllControllers(ADDRINFOA *fullserv, SOCKET sock);

void messageController(Client* c);
void clientMgmtController(SOCKET sock);


#endif //LAB6_CONTROLLER_H
