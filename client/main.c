#include "include/client.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "5000"

int main(int argc, char** argv) {
    /**
     * @usage
     *      server.exe
     *      server.exe [port]
     *      server.exe [host] [port]
     *
     *  default is 127.0.0.1:5000
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
    return runClient(host, port);
}