#include "server/server.h"
#include "auth/authservice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

int main()
try
{
    std::unique_ptr<AuthService> auth;

    const char* enabled = std::getenv("MYWEB_AUTH");

    if (enabled
        && std::strcmp(enabled, "0") != 0
        && std::strcmp(enabled, "1") != 0)
    {
        std::fputs("MYWEB_AUTH must be 0 or 1\n", stderr);
        return 1;
    }

    if (enabled && std::strcmp(enabled, "1") == 0)
    {
        const char* user =
            std::getenv("MYWEB_DB_USER");

        const char* password =
            std::getenv("MYWEB_DB_PASSWORD");

        if (!user || !*user || !password || !*password)
        {
            std::fputs(
                "database credentials are required\n",
                stderr
            );
            return 1;
        }

        auth = std::make_unique<AuthService>(
            user,
            password,
            "mywebserver"
        );
    }

    Server server(true, auth.get());

    if (!server.Init())
        return 1;

    return server.run();
}
catch (const std::exception& e)
{
    std::fprintf(stderr, "startup failed: %s\n", e.what());
    return 1;
}