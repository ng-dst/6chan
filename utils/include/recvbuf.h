#ifndef LAB6_RECVBUF_H
#define LAB6_RECVBUF_H

#include <winsock2.h>

#ifdef DEBUG
#define BASE_BUF_LEN 32
#define MAX_BUF_LEN 256
#else
#define BASE_BUF_LEN 1048576   /*   1 MB */
#define MAX_BUF_LEN 104857600  /* 100 MB */
#endif

#ifdef USE_PIPES
#define SOCKET HANDLE
int sendpipe(SOCKET sock, const char* buf, DWORD len, DWORD flags);
#endif

int recvuntil(char delim, char **ptr, SOCKET sock);
int recvlen(DWORD len, char **ptr, SOCKET sock);

#endif //LAB6_RECVBUF_H
