#include "server/server.h"

int main()
{
    Server server;

    if (!server.Init())
        return 1;
    return server.run();
}