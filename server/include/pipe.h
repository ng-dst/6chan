#ifndef LAB6_PIPE_H
#define LAB6_PIPE_H

#include <windows.h>
#include "model.h"

WINBOOL startPipeServer(const char *pipe_name);
void startAllControllers(const char *pipe_name);
void closeServer();

void clientMgmtController(const char *pipe_name);
void messageController(Client* c);

#endif //LAB6_PIPE_H
