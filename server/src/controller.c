#include <stdio.h>
#include <windows.h>
#include <winsock2.h>
#include "../include/controller.h"
#include "../include/service.h"
#include "../../utils/include/recvbuf.h"


#define ANNOUNCE_LEN 32
#define USER_ID_SYSTEM 0
#define NO_MESSAGES (-1)

#define MAX_USERS 255

CRITICAL_SECTION cs_mh;
DWORD msg_id_counter;
bool cv_stop;

#define terminate() \
    do { \
        printLastWSAError(); \
        closeServer(fullserv, sock); \
        return EXIT_FAILURE; \
    } while(0)

WINBOOL startServer(const char* ip, const char* port) {
    /**
     * @brief Run TCP server: socket() bind() listen(), transfer control to startAllControllers()
     */

    int err;
    WSADATA wsa = {0};
    SOCKET sock = INVALID_SOCKET;
    ADDRINFOA server = {0};
    ADDRINFOA *fullserv = NULL;

    err = WSAStartup(0x0202, &wsa);
    fprintf(stderr, "[startServ] WSAStartup: code %d\r\n", err);
    if (err != ERROR_SUCCESS) terminate();

    server.ai_family = AF_INET;
    server.ai_socktype = SOCK_STREAM;
    server.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(ip, port, &server, &fullserv);
    fprintf(stderr, "[startServ] GetAddrInfo: code %d\r\n", err);
    if (err != ERROR_SUCCESS) terminate();

    sock = socket(fullserv->ai_family, fullserv->ai_socktype, fullserv->ai_protocol);
    if (sock == INVALID_SOCKET) terminate();
    fprintf(stderr, "[startServ] Socket created successfully\r\n");

    err = bind(sock, fullserv->ai_addr, fullserv->ai_addrlen);
    if (err == SOCKET_ERROR) terminate();

    err = listen(sock, SOMAXCONN);
    if (err == SOCKET_ERROR) terminate();

    fprintf(stderr, "[startServ] Server is listening at %s:%s\r\n", ip, port);
    printf("Server is listening at %s:%s\r\n", ip, port);

    initMessageHistory();
    initClientList();

    startAllControllers(fullserv, sock);

    fprintf(stderr, "[startServ] Shutting down server...\r\n");
    destroyMessageHistory();
    destroyClientList();

    return 0;
}

void startAllControllers(ADDRINFOA *fullserv, SOCKET sock) {
    /**
     * @brief Launch threads for all controllers
     * (though only one main controller at the moment, but used to be more)
     */
    DWORD dwt;
    HANDLE controllers[1] = {INVALID_HANDLE_VALUE};

    InitializeCriticalSection(&cs_mh);
    cv_stop = FALSE;

    controllers[0] = CreateThread(NULL, 0, (LPVOID ) clientMgmtController, (LPVOID) sock, 0, &dwt);
    if (controllers[0] == INVALID_HANDLE_VALUE) {
        closeServer(fullserv, sock);
        DeleteCriticalSection(&cs_mh);
        return;
    }

    fprintf(stderr, "[startCtrls] Server is online!\r\n");
    printf("Server is online! Press Enter to stop.\r\n");

    scanf("%*c");

    printf("Stopping server...\r\n");

    cv_stop = TRUE;

    closeServer(fullserv, sock);
    WaitForMultipleObjects(1, controllers, TRUE, INFINITE);
    CloseHandle(controllers[0]);

    fprintf(stderr, "[startCtrls] Threads stopped.\r\n");
    DeleteCriticalSection(&cs_mh);
}

void closeServer(ADDRINFOA *fullserv, SOCKET sock) {
    /**
     * @brief Disconnect all clients, close socket and free address info
     */
    Client *c;

    fprintf(stderr, "[closeServer] Disconnecting clients...\r\n");
    List* cl = getClientList();
    for (Item* i = cl->head; i != NULL; i = i->next) {
        c = (Client*) i->data;
        if (c->sock != INVALID_SOCKET) {
            shutdown(c->sock, SD_BOTH);
            closesocket(c->sock);
            fprintf(stderr, "[closeServer] Disconnected client #%lu\r\n", c->id);
        }
    }

    fprintf(stderr, "[closeServer] Closing server socket...\r\n");

    if (fullserv)
        freeaddrinfo(fullserv);

    if (sock != INVALID_SOCKET)
        closesocket(sock);
}

void clientMgmtController(SOCKET sock) {
    /**
     * @brief Controller for clients management
     */

    List* cl = getClientList();
    List* mh = getMessageHistory();

    Client *c = NULL;
    Message* announce = NULL;
    SOCKET c_sock = INVALID_SOCKET;

    HANDLE msgThreads[MAX_USERS];

    int clients_counter = 1;
    msg_id_counter = 1;
    DWORD dwt;

    fprintf(stderr, "[clMgmtCtrl] Controller launched\r\n");

    // Accept connections in loop
    while (!cv_stop) {
        c_sock = accept(sock, NULL, NULL);
        if (c_sock == INVALID_SOCKET) {
            fprintf(stderr, "[clMgmtCtrl] Failed to accept new client: code %d\r\n", WSAGetLastError());
            continue;
        }

        // Create new client
        c = calloc(1, sizeof(Client));
        if (!c) break;
        c->sock = c_sock;
        c->id = clients_counter;
        getIpPort(c_sock, c->ip, &c->port);

        list_append(cl, c);

        // Publish system message about new client
        announce = calloc(1, sizeof(Message));
        if (!announce) break;
        announce->msg_type = MSG_TYPE_MSG;
        announce->src_id = USER_ID_SYSTEM;
        announce->buf = calloc(1, ANNOUNCE_LEN);
        if (!announce->buf) { free(announce); break; }
        sprintf(announce->buf, "New anon joined. Welcome, Anonim #%lu", c->id);
        announce->msg_len = strlen(announce->buf);

        EnterCriticalSection(&cs_mh);
        announce->msg_id = msg_id_counter++;
        list_append(mh, announce);
        LeaveCriticalSection(&cs_mh);

        fprintf(stderr, "[clMgmtCtrl] New user #%d (%s:%d) joined\r\n", clients_counter, c->ip, c->port);
        printf("New user #%d (%s:%d) joined!\r\n", clients_counter, c->ip, c->port);

        // Create messageController() thread for client
        msgThreads[clients_counter] = CreateThread(NULL, 0, (LPVOID) messageController, (LPVOID) c, 0, &dwt);
        if (msgThreads[clients_counter] == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[clMgmtCtrl] Failed to create thread!\r\n");
            break;
        }

        // Disable users registration if max users count reached
        clients_counter++;
        if (clients_counter == MAX_USERS) {
            fprintf(stderr, "[clMgmtCtrl] Max users count reached (%d). New user registration is disabled.\r\n", MAX_USERS);
            printf("Max users count reached (%d). New user registration is disabled.\r\n", MAX_USERS);
            break;
        }
    }
    fprintf(stderr, "[clMgmtCtrl] Registration loop stopped, waiting for threads...\r\n");

    WaitForMultipleObjects(clients_counter, msgThreads, TRUE, INFINITE);
    for (int j = 0; j < clients_counter; j++)
        if (msgThreads[j] != INVALID_HANDLE_VALUE)
            CloseHandle(msgThreads[j]);

    fprintf(stderr, "[clMgmtCtrl] Stopped all threads, quitting...\r\n");
}


#define disconnectClient() \
    if (c->sock != INVALID_SOCKET) { \
        shutdown(c->sock, SD_BOTH); \
        closesocket(c->sock);       \
        c->sock = INVALID_SOCKET;   \
        return;                     \
    }


void messageController(Client *c) {
    /**
     * @brief Threaded controller for communicating with client
     */

    int res;
    char *buf, welcome_msg[ANNOUNCE_LEN];

    List* msgs = getMessageHistory();
    SOCKET c_sock = c->sock;

    Message *msg = NULL, *orig_msg = NULL, msgbuf = {0};
    Item* i;

    fprintf(stderr, "[msgCtrl | Thread %lu] Controller launched\r\n", GetCurrentThreadId());

    // Process client's requests in loop
    while (!cv_stop) {
        res = recvuntil('\0', &buf, c_sock);

        if (res > 0) {
            fprintf(stderr, "[msgCtrl | Thread %lu] Received data from client #%lu\r\n", GetCurrentThreadId(), c->id);

            // Construct Message from raw buffer
            msg = parseMsgFromClient(buf, res, c_sock);
            free(buf);
            if (!msg) disconnectClient();

            msg->src_id = c->id;
        }
        else {
            // Connection closed, closing socket
            fprintf(stderr, "[msgCtrl | Thread %lu] Closed connection with client #%lu\r\n", GetCurrentThreadId(), c->id);
            disconnectClient();
        }

        // Process message
        switch (msg->msg_type) {

            // Sync: send new messages (if any) to client, end with \0\0
            // msg_id = ID of client's last stored message
            case MSG_TYPE_SYNC:
                fprintf(stderr, "[msgCtrl | Thread %lu] Sync request from #%lu, last msg %d\r\n", GetCurrentThreadId(), msg->src_id, (int) msg->msg_id);

                if ((int) msg->msg_id == NO_MESSAGES) {
                    // New client, send welcome message and skip message search
                    sprintf(welcome_msg, "#0 | Welcome back, Anonim #%lu", msg->src_id);
                    send(c->sock, welcome_msg, strlen(welcome_msg)+1, 0);
                    i = msgs->head;
                }
                else {
                    // Iterate over Message History until msg_id
                    EnterCriticalSection(&cs_mh);
                    for (i = msgs->head; i != NULL; i = i->next)
                        if (((Message*) i->data)->msg_id == msg->msg_id) {
                            i = i->next;
                            break;
                        }
                    LeaveCriticalSection(&cs_mh);
                }

                // Starting from next message, send all messages to client
                for (; i != NULL; i = i->next) {
                    EnterCriticalSection(&cs_mh);
                    orig_msg = (Message*) i->data;
                    // Copy message contents to buffer, or else all Message History will be locked while sending
                    msgbuf.msg_id = orig_msg->msg_id;
                    msgbuf.src_id = orig_msg->src_id;
                    msgbuf.msg_type = orig_msg->msg_type;
                    msgbuf.msg_len = orig_msg->msg_len;
                    memcpy(msgbuf.file_name, orig_msg->file_name, FILE_NAME_LEN);
                    msgbuf.buf = calloc(msgbuf.msg_len+1, sizeof(char));
                    if (msgbuf.buf != NULL) {
                        memcpy(msgbuf.buf, orig_msg->buf, msgbuf.msg_len);
                        // Leave CS before sending
                        LeaveCriticalSection(&cs_mh);

                        // Send message contents to client
                        sendMessageToClient(c, &msgbuf);

                        free(msgbuf.buf);
                    } else LeaveCriticalSection(&cs_mh);
                }
                send(c->sock, "\0", 1, 0);

                free(msg);
                break;

            // Messages and Files: simply add to Message History
            case MSG_TYPE_MSG:
                // Display messages on server, do not display files
                printf("#%lu | Anonim #%lu : %s\r\n", msg_id_counter, msg->src_id, msg->buf);
            case MSG_TYPE_FILE:
                EnterCriticalSection(&cs_mh);

                // Add to Message History
                msg->msg_id = msg_id_counter++;
                fprintf(stderr, "[msgCtrl | Thread %lu] msg_id = %d  msg_len = %lu\r\n", GetCurrentThreadId(), (int) msg->msg_id, msg->msg_len);
                list_append(msgs, msg);

                LeaveCriticalSection(&cs_mh);
                break;

            // Download File or Message
            // msg_id = id of requested file / message
            case MSG_TYPE_LOADFILE:
                // Iterate over Message History to find file / message by id
                EnterCriticalSection(&cs_mh);

                orig_msg = NULL;
                for (i = msgs->head; i != NULL; i = i->next)
                    if (((Message*) i->data)->msg_id == msg->msg_id) {
                        orig_msg = i->data;
                        break;
                    }
                if (!orig_msg)
                    fprintf(stderr, "[msgCtrl | Thread %lu] User #%lu requested unknown file id=%lu\r\n", GetCurrentThreadId(), c->id, msg->msg_id);

                LeaveCriticalSection(&cs_mh);

                // Initiate file download
                sendFileToClient(c, orig_msg);

                free(msg);
                break;

        }
    }
}
