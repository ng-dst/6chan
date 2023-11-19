#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include "../include/client.h"
#include "../include/fileshare.h"
#include "../include/color.h"
#include "../../utils/include/recvbuf.h"

bool cv_stop;
HANDLE ev_stop_client;
CRITICAL_SECTION cs_msg, cs_rcv;

int last_msg_id;
DWORD my_id = 0;

#ifdef DEBUG
#define POLL_INTERVAL_MS 5000
#else
#define POLL_INTERVAL_MS 300
#endif

#define SYNC_BUF_LEN 32
#define INPUT_BUF_LEN 1024

#define STR_(x) #x
#define STR(x) STR_(x)

#define NO_MESSAGES (-1)


#define disconnectOnError() \
    if (err != ERROR_SUCCESS) { \
        printLastWSAError(); \
        closeClient(fullcli, sock); \
        return EXIT_FAILURE; \
    }

WINBOOL runClient(const char *ip, const char *port) {
    /**
     * @brief initialize client: wsaStartup(), getaddrinfo(), socket(), connect()
     * transfer control to startAllServices()
     */
    int err;
    WSADATA wsa = {0};
    SOCKET sock = INVALID_SOCKET;
    ADDRINFOA client = {0};
    ADDRINFOA *fullcli = NULL;

    err = WSAStartup(0x0202, &wsa);
#ifdef DEBUG
    fprintf(stderr, "[runCli] WSAStartup: code %d\n", err);
#endif
    disconnectOnError();

    client.ai_family = AF_INET;
    client.ai_socktype = SOCK_STREAM;
    client.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(ip, port, &client, &fullcli);
#ifdef DEBUG
    fprintf(stderr, "[runCli] GetAddrInfo: code %d\n", err);
#endif
    disconnectOnError();

    sock = socket(fullcli->ai_family, fullcli->ai_socktype, fullcli->ai_protocol);
    if (sock == INVALID_SOCKET) err = SOCKET_ERROR;
    disconnectOnError();
#ifdef DEBUG
    fprintf(stderr, "[runCli] Socket created successfully\n");
#endif

    printf("Connecting to %s:%s...\r\n", ip, port);
    err = connect(sock, fullcli->ai_addr, fullcli->ai_addrlen);
    disconnectOnError();

#ifdef USE_COLOR
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attr = consoleInfo.wAttributes;
#endif

    startAllServices(fullcli, sock);

    return 0;
}

void closeClient(ADDRINFOA *fullcli, SOCKET sock)  {
    /**
     * @brief Close socket and free address info
     */
#ifdef DEBUG
    fprintf(stderr, "[closeClient] Closing client socket\r\n");
#endif
    if (fullcli)
        freeaddrinfo(fullcli);

    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
    }
}

void startAllServices(ADDRINFOA *fullcli, SOCKET sock) {
    /**
     * @brief Launch threads for services: sendService(), syncService(), recvService()
     * @details
     *
     *  Initialize critical sections:
     *      - cs_msg   - lock for send()
     *      - cs_rcv   - lock for recv()
     *
     *  Launch service threads:
     *      - syncService():   sends /sync in background. uses cs_msg lock
     *      - recvService():   receives /sync responses, uses cs_rcv lock
     *      - sendService():   parses user input, sends messages to server
     *          * File download:
     *              uses cs_msg and cs_rcv locks, calls clientDownloadFile()
     *          * File upload:
     *              uses cs_msg lock, calls clientUploadFile()
     *
     *  Wait for event ev_stop_client (is set once connection is closed)
     */
    DWORD dwt;
    HANDLE controllers[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};

    InitializeCriticalSection(&cs_msg);
    InitializeCriticalSection(&cs_rcv);
    ev_stop_client = CreateEventA(0, 0, FALSE, NULL);
    cv_stop = FALSE;

    controllers[0] = CreateThread(NULL, 0, (LPVOID) syncService, (LPVOID) sock, 0, &dwt);
    controllers[1] = CreateThread(NULL, 0, (LPVOID) sendService, (LPVOID) sock, 0, &dwt);
    controllers[2] = CreateThread(NULL, 0, (LPVOID) recvService, (LPVOID) sock, 0, &dwt);
    for (int i = 0; i < 3; i++)
        if (controllers[i] == INVALID_HANDLE_VALUE)
            SetEvent(ev_stop_client);

#ifdef DEBUG
    fprintf(stderr, "[startAllSrv] Services launched!\n");
#endif
    WaitForSingleObject(ev_stop_client, INFINITE);

#ifdef DEBUG
    fprintf(stderr, "[startAllSrv] Stopping client...\n");
#endif
    cv_stop = TRUE;
    closeClient(fullcli, sock);
    WaitForMultipleObjects(3, controllers, TRUE, INFINITE);

    for (int i = 0; i < 3; i++)
        if (controllers != INVALID_HANDLE_VALUE)
            CloseHandle(controllers[i]);
#ifdef DEBUG
    fprintf(stderr, "[startAllSrv] Service threads stopped\n");
#endif
    DeleteCriticalSection(&cs_msg);
    DeleteCriticalSection(&cs_rcv);
}

void syncService(SOCKET sock) {
    /**
     * @brief Background service: Send /sync within a certain time interval
     * @details
     *   Sends /sync command in a certain time interval. Uses cs_msg, i.e. lock for send()
     */
    char buf[SYNC_BUF_LEN];
    int res;
    last_msg_id = NO_MESSAGES;

    while (!cv_stop) {
        memset(buf, 0, SYNC_BUF_LEN);

        EnterCriticalSection(&cs_msg);
        sprintf(buf, "/sync %d", last_msg_id);

        res = send(sock, buf, strlen(buf)+1, 0); // with trailing \0
        if (res == SOCKET_ERROR) {
            printf("Sync connection reset.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
        }
        LeaveCriticalSection(&cs_msg);

        Sleep(POLL_INTERVAL_MS);
    }
}

#define CMD_QUIT "/q"
#define CMD_DL "/dl"
#define CMD_FILE "/file"
#define CMD_SYNC "/sync"

void sendService(SOCKET sock) {
    /**
     * @brief Foreground activity: process user input and send messages to server
     * @details
     *  Parses user input, sends messages to server
     *      * File download:
     *          uses cs_msg and cs_rcv locks, calls clientDownloadFile()
     *      * File upload:
     *          uses cs_msg lock, calls clientUploadFile()
     */
    char buf[INPUT_BUF_LEN] = {0};
    int res;
    DWORD file_id;

    while (!cv_stop) {
        memset(buf, 0, INPUT_BUF_LEN);

        scanf("%" STR(INPUT_BUF_LEN) "[^\n]", buf);
        if (buf[INPUT_BUF_LEN - 1] != '\0') {
            printf("Message is too long. Consider sending as a file.\n");
            fflush(stdin);
            continue;
        }
        scanf("%*c");

        if (cv_stop) break;
        if (strlen(buf) <= 0) continue;

        // Quit
        if (!strcmp(CMD_QUIT, buf)) {
            printf("Disconnecting...\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
            break;
        }

        // Download file
        else if (!strncmp(CMD_DL, buf, 3)) {
            if (strlen(buf) < 5 || !(file_id = atol(&buf[4]))) {
                printf("Specify file id to download.\r\n");
                continue;
            }
            clientDownloadFile(sock, file_id, &cs_msg, &cs_rcv);
        }

        // Upload file
        else if (!strcmp(CMD_FILE, buf)) {
            clientUploadFile(sock, &cs_msg);
        }

        // Some other command (except /sync <id>)
        else if (buf[0] == '/' && strncmp(CMD_SYNC, buf, 5) != 0)
            printf("Available commands:\r\n/file - upload file\r\n/dl <id> - download file or message by #id\r\n/q - quit");

        // Not a command, send message
        else {
            EnterCriticalSection(&cs_msg);
            res = send(sock, buf, strlen(buf)+1, 0);
            if (res == SOCKET_ERROR) {
                printf("Send connection reset.\r\n");
                SetEvent(ev_stop_client);
                cv_stop = TRUE;
            }
            LeaveCriticalSection(&cs_msg);
        }
    }
}

void recvService(SOCKET sock) {
    /**
     * @brief Background service: receive /sync response from server
     * @details
     *  Uses cs_rcv lock, receives messages from server and prints in terminal.
     */

    int res;
    int msg_id, user_id;
    char *buf, *tmp;

    // Monitor server's responses and print them
    while (!cv_stop) {
        EnterCriticalSection(&cs_rcv);
        res = recvuntil('\0', &buf, sock);
        LeaveCriticalSection(&cs_rcv);

#ifdef USE_COLOR
        setColor(DEFAULT_COLOR);
#endif

        if (cv_stop) break;
        if (res == SOCKET_ERROR) {
            printf("Connection reset.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
            break;
        }
        if (res == 0) {
            printf("Disconnected from server.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
            break;
        }
        if (buf[0] == '#') {
            // Matches message form, update last message id
            msg_id = atoi(&buf[1]);
#ifdef DEBUG
            fprintf(stderr, "Got msg id=%lu\r\n", msg_id);
#endif
            if (msg_id > last_msg_id) last_msg_id = msg_id;
#ifdef USE_COLOR
            // search for sender id
            tmp = strchr(&buf[1], '#');
            if (tmp != NULL) {
                user_id = atoi(tmp+1);

                // Get my id from welcome message
                if (msg_id == 0) {
                    my_id = user_id;
                    setColor(DEFAULT_COLOR);
                }

                // set message's sender #id as seed
                else setColor(user_id);
            }
            else setColor(DEFAULT_COLOR);
#endif
        }
        printf("%s\r\n", buf);
#ifdef USE_COLOR
        setColor(my_id);
#endif
    }
}

#ifdef USE_COLOR
void setColor(int seed) {
    WORD colors[NUM_COLORS] = { COLORS_ARRAY };
    if (seed == DEFAULT_COLOR)
        SetConsoleTextAttribute(hConsole, saved_attr);
    else
        SetConsoleTextAttribute(hConsole, colors[seed % NUM_COLORS]);
}
#endif
