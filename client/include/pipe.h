#ifndef LAB6_PIPE_H
#define LAB6_PIPE_H

#include <windows.h>

WINBOOL runPipeClient(const char *pipe);
void startAllServices(HANDLE sock);

void syncService(HANDLE sock);
void sendService(HANDLE sock);
void recvMessages(HANDLE sock);

#endif //LAB6_PIPE_H
