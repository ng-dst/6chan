#ifndef LAB6_CLIENT_H
#define LAB6_CLIENT_H

#ifndef USE_PIPES
#include <winsock2.h>
#include <ws2tcpip.h>

WINBOOL runClient(const char *ip, const char *port);
void closeClient(ADDRINFOA *fullcli, SOCKET sock);
void startAllServices(ADDRINFOA *fullcli, SOCKET sock);
#else
#define SOCKET HANDLE
WINBOOL runClient(const char *pipe);
void startAllServices(SOCKET sock);
#endif

void syncService(SOCKET sock);
void sendService(SOCKET sock);
void recvMessages(SOCKET sock);

#endif //LAB6_CLIENT_H
