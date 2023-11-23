#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include "../include/pipe.h"
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

#define PIPE_TIMEOUT_MS 10000

#define SYNC_BUF_LEN 32
#define INPUT_BUF_LEN 1024

#define STR_(x) #x
#define STR(x) STR_(x)

#define NO_MESSAGES (-1)


WINBOOL runPipeClient(const char *pipe) {
    /**
     * @brief Initialize pipe client, transfer control to startAllServices()
     */
    HANDLE hPipe = INVALID_HANDLE_VALUE;

    while (TRUE) {
        hPipe = CreateFile(
                pipe,   // pipe name
                GENERIC_READ | GENERIC_WRITE,
                0,              // no sharing
                NULL,           // default security attributes
                OPEN_EXISTING,  // opens existing pipe
                0,              // default attributes
                NULL);          // no template file

        // Break if the pipe handle is valid.
        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        // Exit if an error other than ERROR_PIPE_BUSY occurs.
        if (GetLastError() != ERROR_PIPE_BUSY)
        {
            fprintf(stderr, "Could not open pipe. Error code %lu\n", GetLastError());
            return EXIT_FAILURE;
        }

        // All pipe instances are busy, so wait for PIPE_TIMEOUT
        if (!WaitNamedPipe(pipe, PIPE_TIMEOUT_MS)) {
            printf("Could not connect to pipe %s\r\n", pipe);
            return EXIT_FAILURE;
        }
    }

#ifdef USE_COLOR
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attr = consoleInfo.wAttributes;
#endif

    startAllServices(hPipe);

    return EXIT_SUCCESS;
}

void closeClient(HANDLE sock)  {
    /**
     * @brief Close pipe to shut down client
     */
#ifdef DEBUG
    fprintf(stderr, "[closeClient] Closing client pipe\r\n");
#endif
    if (sock != INVALID_HANDLE_VALUE) {
        CloseHandle(sock);
    }
}

void startAllServices(HANDLE sock) {
    /**
     * @brief Launch threads for services: sendService(), syncService(), recvService()
     * @details
     *
     *  Initialize critical sections:
     *      - cs_msg   - lock for send()
     *      - cs_rcv   - lock for recv()
     *
     *  Launch service threads:
     *      - syncService():   sends /sync in background and receives messages until \0\0. uses cs_msg and cs_rcv locks
     *      - sendService():   parses user input, sends messages to server
     *          * File download:
     *              uses cs_msg and cs_rcv locks, calls clientDownloadFile()
     *          * File upload:
     *              uses cs_msg lock, calls clientUploadFile()
     *
     *  Wait for event ev_stop_client (is set once connection is closed)
     */

    DWORD dwt;
    HANDLE controllers[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};

    InitializeCriticalSection(&cs_msg);
    InitializeCriticalSection(&cs_rcv);
    ev_stop_client = CreateEventA(0, 0, FALSE, NULL);
    cv_stop = FALSE;

    controllers[0] = CreateThread(NULL, 0, (LPVOID) syncService, (LPVOID) sock, 0, &dwt);
    controllers[1] = CreateThread(NULL, 0, (LPVOID) sendService, (LPVOID) sock, 0, &dwt);
    for (int i = 0; i < 2; i++)
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
    closeClient(sock);
    WaitForMultipleObjects(2, controllers, TRUE, INFINITE);

    for (int i = 0; i < 2; i++)
        if (controllers != INVALID_HANDLE_VALUE)
            CloseHandle(controllers[i]);
#ifdef DEBUG
    fprintf(stderr, "[startAllSrv] Service threads stopped\n");
#endif
    DeleteCriticalSection(&cs_msg);
    DeleteCriticalSection(&cs_rcv);
}

void syncService(HANDLE sock) {
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

        res = sendpipe(sock, buf, strlen(buf)+1, 0); // with trailing \0
        if (res == SOCKET_ERROR) {
#ifdef USE_COLOR
            setColor(DEFAULT_COLOR);
#endif
            printf("Connection reset.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
        }
        LeaveCriticalSection(&cs_msg);
#ifdef DEBUG
        fprintf(stderr, "[syncService] Sync request sent, last_msg_id=%d\r\n", last_msg_id);
#endif
        recvMessages(sock);
#ifdef DEBUG
        fprintf(stderr, "[syncService] Messages received.\r\n");
#endif
        Sleep(POLL_INTERVAL_MS);
    }
}

#define CMD_QUIT "/q"
#define CMD_DL "/dl"
#define CMD_FILE "/file"
#define CMD_SYNC "/sync"

void sendService(HANDLE sock) {
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
#ifdef USE_COLOR
        setColor(my_id);
#endif
        scanf("%" STR(INPUT_BUF_LEN) "[^\n]", buf);
        if (buf[INPUT_BUF_LEN - 1] != '\0') {
            printf("Message is too long. Consider sending as a file.\n");
            fflush(stdin);
            continue;
        }
        scanf("%*c");

#ifdef USE_COLOR
        setColor(DEFAULT_COLOR);
#endif
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
#ifdef USE_COLOR
            setColor(DEFAULT_COLOR);
#endif
            clientUploadFile(sock, &cs_msg);
        }

            // Some other command (now manual /sync is disabled)
        else if (buf[0] == '/' != 0)
            printf("Available commands:\r\n/file - upload file\r\n/dl <id> - download file or message by #id\r\n/q - quit\r\n");

            // Not a command, send message
        else {
            EnterCriticalSection(&cs_msg);
            res = sendpipe(sock, buf, strlen(buf)+1, 0);
            if (res == SOCKET_ERROR) {
                printf("Send connection reset.\r\n");
                SetEvent(ev_stop_client);
                cv_stop = TRUE;
            }
            LeaveCriticalSection(&cs_msg);
        }
    }
}

void recvMessages(SOCKET sock) {
    /**
     * @brief syncService's subroutine: receive /sync response from server
     * @details
     *  Uses cs_rcv lock, receives messages from server and prints in terminal.
     */

    int res;
    int msg_id, user_id;
    char *buf = NULL, *tmp;

    // Monitor server's responses and print them, stop on \0\0, i.e. empty message
    while (!cv_stop) {
        EnterCriticalSection(&cs_rcv);
        res = recvuntil('\0', &buf, sock);
        LeaveCriticalSection(&cs_rcv);

        // Received only \0 (which means \0 twice in a row)
        if (res == 1 && !buf[0]) { if (buf) free(buf); return; }

#ifdef USE_COLOR
        setColor(DEFAULT_COLOR);
#endif

        if (cv_stop) return;

        if (res == SOCKET_ERROR) {
            printf("Connection reset.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
            if (buf) free(buf);
            return;
        }
        if (res == 0) {
            printf("Disconnected from server.\r\n");
            SetEvent(ev_stop_client);
            cv_stop = TRUE;
            return;
        }
        if (buf[0] == '#') {
            // Matches message form, update last message id
            msg_id = atoi(&buf[1]);
#ifdef DEBUG
            fprintf(stderr, "[recvMessages] Got msg_id=%d, last_msg_id=%d\r\n", msg_id, last_msg_id);
#endif
            if (msg_id > last_msg_id) last_msg_id = msg_id;

#ifdef USE_COLOR
            // search for sender id:  #3 [hh:mm] Anonim #id: ...
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
        free(buf);
        buf = NULL;

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
