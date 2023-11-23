#ifdef USE_PIPES

#include "include/pipe.h"
#define DEFAULT_HOST "\\\\.\\pipe\\6chan"

#else

#include "include/controller.h"
#define DEFAULT_HOST "127.0.0.1"

#endif
#define DEFAULT_PORT "5000"

int main(int argc, char** argv) {
    /**
     * @usage
     *      ./lab6
     *      ./lab6 [port]
     *      ./lab6 [host] [port]
     *
     *  default is
     *      127.0.0.1:5000  (socket)
     *      \.\pipe\6chan   (pipe)
     */
    char *host, *port;
    if (argc < 2) {
        host = DEFAULT_HOST;
        port = DEFAULT_PORT;
    }
    else if (argc == 2) {
        host = DEFAULT_HOST;
        port = argv[1];
    }
    else {
        host = argv[1];
        port = argv[2];
    }
    return
#ifdef USE_PIPES
    startPipeServer(host);
#else
    startServer(host, port);
#endif
}
