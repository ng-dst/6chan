#include <stdio.h>
#include <windows.h>
#include "../include/pipe.h"
#include "../include/service.h"
#include "../../utils/include/recvbuf.h"


#define ANNOUNCE_LEN 32
#define USER_ID_SYSTEM 0
#define NO_MESSAGES (-1)

#define MAX_USERS 65536

CRITICAL_SECTION cs_mh;
DWORD msg_id_counter;
bool cv_stop;
HANDLE new_sock = INVALID_HANDLE_VALUE;

WINBOOL startPipeServer(const char* pipe) {
    /**
     * @brief Run Pipe server, transfer control to startAllControllers()
     */

    fprintf(stderr, "[startServ] Server is listening at %s\r\n", pipe);
    printf("Server is listening at %s\r\n", pipe);

    initMessageHistory();
    initClientList();

    startAllControllers(pipe);

    fprintf(stderr, "[startServ] Shutting down server...\r\n");
    destroyMessageHistory();
    destroyClientList();

    return 0;
}


void startAllControllers(const char *pipe) {
    /**
     * @brief Launch threads for all controllers
     * (though only one main controller at the moment, but used to be more)
     */
    DWORD dwt;
    HANDLE controllers[1] = {INVALID_HANDLE_VALUE};

    InitializeCriticalSection(&cs_mh);
    cv_stop = FALSE;

    controllers[0] = CreateThread(NULL, 0, (LPVOID) clientMgmtController, (LPVOID) pipe, 0, &dwt);
    if (controllers[0] == INVALID_HANDLE_VALUE) {
        DeleteCriticalSection(&cs_mh);
        return;
    }

    fprintf(stderr, "[startCtrls] Server is online!\r\n");
    printf("Server is online! Press Enter to stop.\r\n");

    scanf("%*c");

    printf("Stopping server...\r\n");

    cv_stop = TRUE;

    closeServer();
    WaitForMultipleObjects(1, controllers, TRUE, INFINITE);
    CloseHandle(controllers[0]);

    fprintf(stderr, "[startCtrls] Threads stopped.\r\n");
    DeleteCriticalSection(&cs_mh);
}


#define disconnectClient()                      \
    if (c->sock != INVALID_HANDLE_VALUE) {      \
        DisconnectNamedPipe(c->sock);           \
        CloseHandle(c->sock);                   \
        c->sock = INVALID_HANDLE_VALUE;         \
    }                                           \


void clientMgmtController(const char *pipe) {
    /**
     * @brief Controller for clients management
     */

    List* cl = getClientList();
    List* mh = getMessageHistory();

    Client *c = NULL;
    Message* announce = NULL;
    HANDLE* tmp;
    BOOL fConn = FALSE;

    int msg_threads_size = MAX_USERS;
    HANDLE *msgThreads = calloc(MAX_USERS, sizeof(HANDLE));
    if (!msgThreads) return;

    int clients_counter = 1;
    msg_id_counter = 1;
    DWORD dwt;

    fprintf(stderr, "[clMgmtCtrl] Controller launched\r\n");

    // Accept connections in loop
    while (!cv_stop) {
        new_sock = CreateNamedPipeA(pipe,
                                    PIPE_ACCESS_DUPLEX,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES,
                                    BASE_BUF_LEN,
                                    BASE_BUF_LEN,
                                    0,
                                    NULL);
        if (new_sock == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[clMgmtCtrl] Failed to create pipe: code %lu\r\n", GetLastError());
            break;
        }

        fConn = ConnectNamedPipe(new_sock, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!fConn) {
            CloseHandle(new_sock);
            break;
        }
        // Create new client
        c = calloc(1, sizeof(Client));
        if (!c) break;
        c->sock = new_sock;
        c->id = clients_counter;

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
        GetLocalTime(&announce->timestamp);

        EnterCriticalSection(&cs_mh);
        announce->msg_id = msg_id_counter++;
        list_append(mh, announce);
        LeaveCriticalSection(&cs_mh);

        fprintf(stderr, "[clMgmtCtrl] New user #%d joined\r\n", clients_counter);
        printf("New user #%d joined!\r\n", clients_counter);

        // Create messageController() thread for client
        msgThreads[clients_counter] = CreateThread(NULL, 0, (LPVOID) messageController, (LPVOID) c, 0, &dwt);
        if (msgThreads[clients_counter] == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[clMgmtCtrl] Failed to create thread for client #%lu! Closing connection.\r\n", c->id);
            sendpipe(c->sock, "Sorry, something went wrong.\r\n\0", 32, 0);
            disconnectClient();
        }

        // Extend msgThreads buffer if needed
        clients_counter++;
        if (clients_counter == msg_threads_size) {
            fprintf(stderr, "[clMgmtCtrl] Max users count reached (%d). Reallocating.\r\n", msg_threads_size);
            tmp = realloc(msgThreads, msg_threads_size + MAX_USERS);
            if (!tmp) disconnectClient();
            msgThreads = tmp;
            msg_threads_size += MAX_USERS;
        }
    }
    fprintf(stderr, "[clMgmtCtrl] Registration loop stopped, waiting for threads...\r\n");

    WaitForMultipleObjects(clients_counter-1, msgThreads, TRUE, INFINITE);
    for (int j = 0; j < clients_counter-1; j++)
        if (msgThreads[j] != INVALID_HANDLE_VALUE)
            CloseHandle(msgThreads[j]);

    free(msgThreads);
    fprintf(stderr, "[clMgmtCtrl] Stopped all threads, quitting...\r\n");
}


void closeServer() {
    /**
     * @brief Disconnect all clients, close handles
     */
    Client *c;

    fprintf(stderr, "[closeServer] Disconnecting clients...\r\n");
    List* cl = getClientList();

    for (Item* i = cl->head; i != NULL; i = i->next) {
        c = (Client*) i->data;
        if (c->sock != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(c->sock);
            DisconnectNamedPipe(c->sock);
            CloseHandle(c->sock);
            c->sock = INVALID_HANDLE_VALUE;
            fprintf(stderr, "[closeServer] Disconnected client #%lu\r\n", c->id);
        }
    }

    fprintf(stderr, "[closeServer] Closing server pipe...\r\n");
    CancelIoEx(new_sock, NULL);
}


void messageController(Client *c) {
    /**
     * @brief Threaded controller for communicating with client
     */

    int res;
    char *buf, welcome_msg[ANNOUNCE_LEN];

    List* msgs = getMessageHistory();
    HANDLE c_sock = c->sock;

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
            if (!msg)  { disconnectClient(); return; }
            msg->src_id = c->id;
        }
        else {
            // Connection closed, closing pipe
            fprintf(stderr, "[msgCtrl | Thread %lu] Closed connection with client #%lu\r\n", GetCurrentThreadId(), c->id);
            disconnectClient();
            return;
        }

        // Process message
        switch (msg->msg_type) {

            // Sync: send new messages (if any) to client, end with \0\0
            // msg_id = ID of client's last stored message
            case MSG_TYPE_SYNC:
                fprintf(stderr, "[msgCtrl | Thread %lu] Sync request from #%lu, last msg %d\r\n", GetCurrentThreadId(), msg->src_id, (int) msg->msg_id);

                if ((int) msg->msg_id == NO_MESSAGES) {
                    // New client, send welcome message and skip message search
                    sprintf(welcome_msg, "#0  Welcome back, Anonim #%lu", msg->src_id);
                    sendpipe(c->sock, welcome_msg, strlen(welcome_msg)+1, 0);
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
                    msgbuf.timestamp = orig_msg->timestamp;
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
                sendpipe(c->sock, "\0", 1, 0);

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
