#include <stdio.h>
#include "../include/recvbuf.h"

#ifdef SERVER
static thread_local char *_buf = NULL;
static thread_local int end = 0;
static thread_local int size = BASE_BUF_LEN;
#else
static char *_buf = NULL;
static int end = 0;
static int size = BASE_BUF_LEN;
#endif

#ifdef USE_PIPES
#define SOCKET HANDLE
#endif


int recvuntil(char delim, char **ptr, SOCKET sock) {
    /**
     * @brief Allocate buffer and receive until `delimiter` char
     * @details
     *  Uses thread-local (for server) or shared (for client) static buffer.
     *  Buffer state persists between calls.
     *      end = current index of last received byte in buffer
     *      size = current buffer size
     *
     *  Algorithm
     *
     *      if not buf:
     *          allocate buf
     *          size = BASE_LEN
     *
     *      while True:
     *          if delim in buf[0:end]:
     *              pos = index of delim
     *              allocate 'return buffer', copy first `pos` bytes
     *              pop first `pos` bytes from buffer, shift remaining data
     *              shrink buffer, decrease size
     *              *ptr = 'return buffer'
     *              return len
     *          else:
     *              n = recv( &buf[end] <- (size-end) bytes )
     *              end = end + n
     *              if end > MAX_SIZE:
     *                  clear buffer, return 1
     *              if end >= size:
     *                  extend buffer, increase size
     *
     *  Illustration
     *
     *      Please refer to docs.
     */
    int n, pos, new_size;
    char *tmp, *ret;
    BOOL fSuccess;

    if (!_buf) {
        _buf = calloc(1, BASE_BUF_LEN);
        size = BASE_BUF_LEN;
        end = 0;
        if (!_buf) return SOCKET_ERROR;
    }

    while (TRUE) {
        tmp = memchr(_buf, delim, end);
        if (tmp != NULL) {

            // Delimiter is at `pos`
            pos = tmp - _buf + 1;

            // Allocate 'return buffer'
            ret = calloc(pos, sizeof(char));
            if (!ret) { free(_buf); _buf = NULL; return SOCKET_ERROR; }

            // Cut first `pos` bytes from buf, shift buf
            end -= pos;
            memcpy(ret, _buf, pos);
            memmove(_buf, _buf+pos, end);

            // Shrink buf (if needed)
            new_size = end + BASE_BUF_LEN - end % BASE_BUF_LEN;
            if (new_size + BASE_BUF_LEN + 1 < size) {
                tmp = realloc(_buf, new_size);
                if (tmp) {
                    _buf = tmp;
                    size = new_size;
                }
            }
            *ptr = ret;
            return pos;
        }

        // No delimiter, continue receiving
#ifdef USE_PIPES
        fSuccess = ReadFile(sock, _buf+end, size-end, (LPDWORD) &n, NULL);
        if (!fSuccess) n = SOCKET_ERROR;
#else
        n = recv(sock, _buf+end, size-end, 0);
#endif
        if (n == SOCKET_ERROR || n == 0) {
            if (_buf) free(_buf);
            _buf = NULL;
            return n;
        }

        end += n;
        // If buffer is full, extend it
        if (end >= size) {
            if (size + BASE_BUF_LEN < MAX_BUF_LEN) {
                size += BASE_BUF_LEN;
                tmp = realloc(_buf, size);
                if (!tmp) {
                    free(_buf);
                    _buf = NULL;
                    return SOCKET_ERROR;
                }
                _buf = tmp;
            } else {
                // Too large message. Deny.
                free(_buf);
                _buf = NULL;
                *ptr = strdup("\n");
                return 1;
            }
        }
    }
}

int recvlen(DWORD len, char **ptr, SOCKET sock) {
    /**
     * @brief Allocate buffer and receive exactly `len` characters
     * @details
     *  Uses thread-local (for server) or shared (for client) static buffer.
     *  Buffer state persists between calls.
     *      end = current index of last received byte in buffer
     *      size = current buffer size
     *
     *  Algorithm
     *
     *      if len > MAX_SIZE:
     *          *ptr = '\n'
     *          return 1
     *
     *      if not buf:
     *          allocate buf
     *          size = BASE_LEN
     *
     *      extend if needed
     *
     *      while True:
     *          if end >= len:   (buffer has enough data)
     *              allocate 'return buffer', copy first `len` bytes
     *              pop first `len` bytes from buffer, shift remaining data
     *              shrink buffer, decrease size
     *              *ptr = 'return buffer'
     *              return len
     *          else:
     *              n = recv( &buf[end] <- (size-end) bytes )
     *              end = end + n
     */
    int n, new_size;
    char *ret, *tmp;
    BOOL fSuccess;

    if (len > MAX_BUF_LEN) {
        // too large message. Deny.
        *ptr = strdup("\n");
        return 1;
    }

    if (!_buf) {
        _buf = calloc(1, BASE_BUF_LEN);
        size = BASE_BUF_LEN;
        end = 0;
        if (!_buf) return SOCKET_ERROR;
    }

    // extend buffer if needed
    if (size < len) {
        size = len + BASE_BUF_LEN - len % BASE_BUF_LEN;
        tmp = realloc(_buf, size);
        if (!tmp) {
            free(_buf);
            _buf = NULL;
            return SOCKET_ERROR;
        }
        _buf = tmp;
    }

    while (TRUE) {
        // Buffer already has `len` bytes?
        if (end >= len) {

            // Allocate 'return buffer'
            ret = calloc(len, sizeof(char));
            if (!ret) { free(_buf); _buf = NULL; return SOCKET_ERROR; }

            // Cut first `len` bytes, shift buf
            end -= len;
            memcpy(ret, _buf, len);
            memmove(_buf, _buf+len, end);

            // Shrink buf if needed
            new_size = end + BASE_BUF_LEN - end % BASE_BUF_LEN;
            if (new_size + BASE_BUF_LEN + 1 < size) {
                tmp = realloc(_buf, new_size);
                if (tmp) {
                    _buf = tmp;
                    size = new_size;
                }
            }
            *ptr = ret;
            return len;
        }

        // Not enough bytes received, continue
#ifdef USE_PIPES
        fSuccess = ReadFile(sock, _buf+end, size-end, (LPDWORD) &n, NULL);
        if (!fSuccess) n = SOCKET_ERROR;
#else
        n = recv(sock, _buf+end, size-end, 0);
#endif
        if (n == SOCKET_ERROR || n == 0) {
            if (_buf) free(_buf);
            _buf = NULL;
            return n;
        }
        end += n;
    }
}


#ifdef USE_PIPES
int sendpipe(HANDLE sock, const char* buf, DWORD len, DWORD flags) {
    int n;
    BOOL fSuccess = WriteFile(sock,
                              buf,
                              len,
                              (LPDWORD) &n,
                              NULL);
    if (!fSuccess) n = SOCKET_ERROR;
    return n;
}
#endif