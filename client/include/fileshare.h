#ifndef LAB6_FILESHARE_H
#define LAB6_FILESHARE_H

#include <winsock2.h>

#ifdef USE_PIPES
#define SOCKET HANDLE
#endif

void clientDownloadFile(SOCKET sock, DWORD file_id, CRITICAL_SECTION *cs_send, CRITICAL_SECTION *cs_recv);
void clientUploadFile(SOCKET sock, CRITICAL_SECTION *cs_send);

WINBOOL clientSelectOpenPath(char* path_buf);
WINBOOL clientSelectSavePath(char* path_buf);

void printLastError();
void printLastWSAError();

#endif //LAB6_FILESHARE_H
