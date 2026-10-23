#include "server/server.h"

int main()
{
    Server server(true);

    if (!server.Init())
        return 1;
    return server.run();
}
