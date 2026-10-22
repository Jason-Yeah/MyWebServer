// LT 对照测试入口：与正式 ET 入口共用相同 Server 源码，不修改 main 的模式。
#include "../code/server/server.h"

int main()
{
    Server server(false);
    if (!server.Init())
        return 1;
    return server.run();
}
