#include <ws2tcpip.h>
#include "../include/service.h"
#include "../../utils/include/recvbuf.h"

#define MSG_HEADER_LEN 128
#define INPUT_BUF_LEN 1024

#define FILE_HEADER "\0\0\0\xff"
#define FILE_HEADER_LEN 4

#define FILE_SIZE_MAX MAX_BUF_LEN


#ifdef USE_PIPES
#define send sendpipe
#define SOCKET HANDLE
#endif


#ifndef USE_PIPES
void getIpPort(SOCKET sock, char *ip, WORD *port) {
    /**
     * @brief Get IP and port by socket
     */
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(sock, &addr, &addr_len);
    struct sockaddr_in *addr_inet = (struct sockaddr_in *) &addr;
    strncpy(ip, inet_ntoa(addr_inet->sin_addr), 16);
    *port = ntohs(addr_inet->sin_port);
}
#endif


#define returnOnError() \
    if (res < 1) { \
        fprintf(stderr, "[sendToClient] Error sending to client %lu\r\n", c->id); \
        return FALSE; \
    }


WINBOOL sendMessageToClient(Client* c, Message* msg) {
    /**
     * @brief Send message in human-readable format to client, ending with \0
     */
    int res;
    if (!c || !msg || !msg->buf) return FALSE;

    fprintf(stderr, "[sendMsgToClient] Service invoked for msg id=%lu\r\n", msg->msg_id);

    WORD hh = msg->timestamp.wHour;
    WORD mm = msg->timestamp.wMinute;

    char msg_header[MSG_HEADER_LEN];
    if (msg->src_id != 0)
        sprintf(msg_header, "#%lu [%02hu:%02hu]  Anonim #%lu: ", msg->msg_id, hh, mm, msg->src_id);
    else
        sprintf(msg_header, "#%lu [%02hu:%02hu]  ", msg->msg_id, hh, mm);

    // Send message meta info: '#id [hh:mm]  Anonim #id : '
    res = send(c->sock, msg_header, strlen(msg_header), 0); // no trailing \0 yet
    returnOnError();

    if (msg->msg_type == MSG_TYPE_MSG) {
        // Send actual message with \0
        res = send(c->sock, msg->buf, msg->msg_len+1, 0); // with trailing \0
        returnOnError();
    }
    else if (msg->msg_type == MSG_TYPE_FILE) {
        // Send file details (see sprintf below)
        sprintf(msg_header, "File '%s' (%lu bytes). Type '/dl %lu' to download", msg->file_name, msg->msg_len, msg->msg_id);
        res = send(c->sock, msg_header, strlen(msg_header)+1, 0);
        returnOnError();
    }
    else return FALSE;

    return TRUE;
}


WINBOOL sendFileToClient(Client* c, Message* msg) {
    /**
     * @brief Routine to process file download request
     *
     * @details
     *  request format:    /dl <id>
     *  response format:   FILE_HEADER <size> <content>
     *
     *  Both files and messages can be downloaded
     */

    if (!c) {
        return FALSE;
    }

    fprintf(stderr, "[sendFile] Starting file download, client #%lu...\r\n", c->id);

    // send file header
    int res = send(c->sock, FILE_HEADER, FILE_HEADER_LEN, 0);
    returnOnError();

    // if file not found, send invalid len
    res = INVALID_FILE_SIZE;
    if (!msg || msg->msg_len < 1 || !msg->buf) {
        send(c->sock, (LPVOID) &res, sizeof(DWORD), 0);
        return TRUE;
    }

    // send 4-byte file length
    res = send(c->sock, (LPVOID) &msg->msg_len, sizeof(DWORD), 0);
    returnOnError();

    fprintf(stderr, "[sendFile] Sent header and file size, sending content...\r\n");

    // send actual file content
    res = send(c->sock, msg->buf, msg->msg_len, 0);
    returnOnError();

    fprintf(stderr, "[sendFile] Sent file #%lu (%lu bytes) to client #%lu\r\n", msg->msg_id, msg->msg_len, c->id);

    return TRUE;
}

#define CMD_QUIT "/q"
#define CMD_DL "/dl"
#define CMD_FILE "/file"
#define CMD_SYNC "/sync"

Message* parseMsgFromClient(const char* buf, int len, SOCKET sock) {
    /**
     * @brief parse raw message to process commands (if any) and form Message struct
     */

    if (!buf) return NULL;

    Message* msg = calloc(1, sizeof(Message));
    if (!msg) return NULL;

    GetLocalTime(&msg->timestamp);

    if (len > 1) {

        if (!strncmp(CMD_DL, buf, 3)) {
            // dl format:     /dl <file_id>
            msg->msg_type = MSG_TYPE_LOADFILE;
            msg->msg_id = atol(&buf[4]);
            return msg;
        }

        if (!strncmp(CMD_FILE, buf, 5)) {
            // file format:   /file <name>%00<size><content>     (client types /file, then chooses one in explorer)
            msg->msg_type = MSG_TYPE_FILE;
            strncpy(msg->file_name, &buf[6], FILE_NAME_LEN-1);
            if (acceptFileFromClient(msg, sock) == SOCKET_ERROR) {
                fprintf(stderr, "[parseMsg] Incoming file size is too big. Denying upload\r\n");
                send(sock, "Wow, it's so big!", 18, 0);
                return NULL;
            }
            send(sock, "File uploaded", 14, 0);
            return msg;
        }

        if (!strncmp(CMD_SYNC, buf, 5)) {
            // sync format:    /sync <last_msg_id>
            msg->msg_type = MSG_TYPE_SYNC;
            msg->msg_id = atol(&buf[6]);
            return msg;
        }
    }

    // default: message
    msg->msg_type = MSG_TYPE_MSG;
    msg->buf = calloc(len, sizeof(char));
    if (!msg->buf) { free(msg); return NULL; }
    strncpy(msg->buf, buf, len-1);
    msg->buf[len-1] = '\0';
    msg->msg_len = len-1;

    return msg;
}

int acceptFileFromClient(Message* msg, SOCKET sock) {
    /**
     * @brief Use socket to accept file from client and write it as msg
     */

    int res;
    DWORD size;
    char* tmp;

    fprintf(stderr, "[acceptFile] Accepting file %s\r\n", msg->file_name);

    // get file size
    res = recvlen(sizeof(DWORD), &tmp, sock);
    if (res <= 0) return SOCKET_ERROR;

    size = *((DWORD*) tmp);
    free(tmp);
    if (size > FILE_SIZE_MAX) return SOCKET_ERROR;

    fprintf(stderr, "[acceptFile] File size = %lu\r\n", size);

    char *buf;
    res = recvlen(size, (LPVOID) &buf, sock);
    if (res == SOCKET_ERROR) return SOCKET_ERROR;

    fprintf(stderr, "[acceptFile] File accepted!\r\n");

    msg->msg_len = size;
    msg->buf = buf;

    return res;
}
