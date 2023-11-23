#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "../include/fileshare.h"
#include "../../utils/include/recvbuf.h"

#define CMD_BUF_LEN 32
#define FILE_HEADER '\xff'

#ifdef USE_PIPES
#define send sendpipe
#endif


WINBOOL clientSelectSavePath(char* buf) {
    /**
     * @brief Make dialog in explorer 'Save as...'
     */
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Kotiki (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "txt";

    return GetSaveFileNameA(&ofn);
}

WINBOOL clientSelectOpenPath(char* buf) {
    /**
     * @brief Make dialog in explorer 'Open...'
     */
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Kotiki (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = "txt";

    return GetOpenFileNameA(&ofn);
}


#define abortDlOnError() \
    if (res <= 0) { \
        printf("File #%lu not found.\r\n", file_id); \
        LeaveCriticalSection(cs_recv); \
        LeaveCriticalSection(cs_send); \
        return; \
    }

void clientDownloadFile(SOCKET sock, DWORD file_id, CRITICAL_SECTION *cs_send, CRITICAL_SECTION *cs_recv) {
    /**
     * @brief Download file by ID and write it on disk
     * @details
     *  Uses critical sections cs_msg and cs_rcv, i.e. locks both send() and recv() while downloading.
     *
     *  Request format:   /dl <id>
     *  Response format:  FILE_HEADER <size> <content>
     *
     *  Once downloaded to buffer, releases sections and asks user 'Save as...'.
     */
    char *tmp = NULL, cmd_buf[CMD_BUF_LEN] = {0};
    int res, size;

    sprintf(cmd_buf, "/dl %lu", file_id);

    EnterCriticalSection(cs_send);
    send(sock, cmd_buf, strlen(cmd_buf)+1, 0);

    EnterCriticalSection(cs_recv);

#ifdef DEBUG
    fprintf(stderr, "[downloadFile] Waiting for header...\r\n");
#endif
    res = recvuntil(FILE_HEADER, &tmp, sock);
    abortDlOnError();
    free(tmp);

#ifdef DEBUG
    fprintf(stderr, "[downloadFile] Header loaded\r\n");
#endif

    res = recvlen(sizeof(int), (LPVOID) &tmp, sock);
    abortDlOnError();

#ifdef DEBUG
    fprintf(stderr, "[downloadFile] Size received\r\n");
#endif

    size = *((int*) tmp);
    free(tmp);
    if (size == INVALID_FILE_SIZE) res = 0;
    abortDlOnError();

    printf("Downloading file #%lu (%d bytes)...\r\n", file_id, size);
    res = recvlen(size, &tmp, sock);
    abortDlOnError();

    LeaveCriticalSection(cs_recv);

#ifdef DEBUG
    fprintf(stderr, "[downloadFile] File downloaded, saving...\r\n");
#endif

    // now `tmp` contains `size`-bytes file content
    DWORD bw;
    char file_path[MAX_PATH] = {0};
    if (!clientSelectSavePath(file_path)) {
        free(tmp);
        LeaveCriticalSection(cs_send);
        return;
    }

    LeaveCriticalSection(cs_send);

    // now we have file path to save to
    HANDLE hf = CreateFileA(file_path,
                            GENERIC_WRITE,
                            FILE_SHARE_READ,
                            NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        printf("Could not save to %s\r\n", file_path);
        printLastError();
        free(tmp);
        return;
    }

    WriteFile(hf, tmp, size, &bw, NULL);
    free(tmp);
    CloseHandle(hf);
    printf("File #%lu saved as %s\r\n", file_id, file_path);
}

void clientUploadFile(SOCKET sock, CRITICAL_SECTION *cs_send) {
    /**
     * @brief Routine to pick a file from disk and send it to server
     * @details
     *  Uses cs_msg lock, i.e. locks send() while uploading
     *
     *  request format:  /file <name> \0 <size> <content>
     *  response format:  None (does not wait for response)
     */
    DWORD dw, size, total_size, pos;
    char* tmp;
    char file_path[MAX_PATH] = {0}, file_name[MAX_PATH] = {0}, *buf = NULL;
    if (!clientSelectOpenPath(file_path)) return;

    HANDLE hf = CreateFileA(file_path,
                            GENERIC_READ,
                            FILE_SHARE_READ,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        printf("Could not read file %s\r\n", file_path);
        printLastError();
        return;
    }

    // Try to strip path from file name
    tmp = strrchr(file_path, '\\');
    if (tmp) strncpy(file_name, tmp+1, tmp-file_path+MAX_PATH-1);
    else strncpy(file_name, file_path, MAX_PATH);

    size = GetFileSize(hf, NULL);
    total_size = 6 + strlen(file_name) + 1 + sizeof(DWORD) + size;

    // buf will look like:    /file <file_name>\0<file_size><content>
    buf = malloc(total_size);
    if (!buf) return;

    sprintf(buf, "/file %s", file_name); //  /file <name>\0
    pos = 6 + strlen(file_name) + 1;
    memcpy(buf+pos, &size, sizeof(DWORD)); //  <size>
    pos += sizeof(DWORD);
    ReadFile(hf, buf+pos, size, &dw, NULL);

    CloseHandle(hf);

    // Now file has been read, sending
    EnterCriticalSection(cs_send);
    send(sock, buf, total_size, 0);
    LeaveCriticalSection(cs_send);

    int res = recvuntil('\0', &tmp, sock);
    if (res <= 0) printf("This file is so big!\r\n");
    // Print response
    else printf("%s\r\n", tmp);

    free(tmp);
    free(buf);
}

void printLastError() {
    fprintf(stderr, "WinAPI error: %lu\r\n", GetLastError());
}

void printLastWSAError() {
    fprintf(stderr, "WSA error: %d\r\n", WSAGetLastError());
}
