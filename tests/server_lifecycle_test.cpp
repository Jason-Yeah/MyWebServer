// Linux 专用资源生命周期测试；--wrap 仅影响本测试，不影响正式 server。
#include "../code/server/server.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

static int waitsBeforeFailure = -1;
extern "C" int __real_epoll_wait(int, epoll_event*, int, int);
extern "C" int __wrap_epoll_wait(int fd, epoll_event* events, int capacity, int timeout)
{
    if (waitsBeforeFailure == 0) {
        errno = EIO;
        return -1;
    }
    int result = __real_epoll_wait(fd, events, capacity, timeout);
    if (result > 0 && waitsBeforeFailure > 0) --waitsBeforeFailure;
    return result;
}

static int FdCount()
{
    DIR* dir = opendir("/proc/self/fd");
    assert(dir);
    int count = 0;
    while (const auto* entry = readdir(dir))
        if (entry->d_name[0] != '.') ++count;
    assert(closedir(dir) == 0);
    return count;
}

static sockaddr_in Address()
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8080);
    return address;
}

int main(int argc, char** argv)
{
    const bool useET = argc > 1 && std::strcmp(argv[1], "ET") == 0;
    std::printf("Lifecycle mode: %s\n", useET ? "ET" : "LT");
    static_assert(!std::is_copy_constructible<Epoller>::value, "unique ownership");
    static_assert(!std::is_copy_constructible<Server>::value, "unique ownership");
    const int baseline = FdCount();
    {
        Epoller unused;
        Server unusedServer(useET);
    }
    assert(FdCount() == baseline);
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    {
        Epoller epoller;
        assert(epoller.init());
        assert(FdCount() == baseline + 3);
        assert(!epoller.init() && errno == EALREADY);
        assert(epoller.Wait(0) == 0);
        assert(epoller.ADD(pair[0], EPOLLIN));
        assert(!epoller.ADD(pair[0], EPOLLIN) && errno == EEXIST);
        assert(!epoller.ADD(-1, EPOLLIN) && errno == EBADF);
        assert(send(pair[1], "x", 1, MSG_NOSIGNAL) == 1);
        assert(epoller.Wait(1000) == 1);
        assert(epoller.Event(0).data.fd == pair[0]);
        assert(epoller.Event(0).events & EPOLLIN);
        char byte;
        assert(recv(pair[0], &byte, 1, 0) == 1 && byte == 'x');
        assert(epoller.Wait(0) == 0);
        assert(epoller.MOD(pair[0], EPOLLOUT));
        assert(epoller.Wait(1000) == 1);
        assert(epoller.Event(0).events & EPOLLOUT);
        assert(epoller.DEL(pair[0]));
        assert(epoller.Wait(0) == 0);
        assert(!epoller.MOD(pair[0], EPOLLIN) && errno == ENOENT);
    }
    // Epoller 销毁后，两个被观察的 socket 仍应由调用方持有。
    assert(fcntl(pair[0], F_GETFD) >= 0);
    assert(fcntl(pair[1], F_GETFD) >= 0);
    close(pair[0]);
    close(pair[1]);
    assert(FdCount() == baseline);
    std::puts("PASS Epoller ADD/MOD/DEL/Wait、错误返回、非 socket 所有权和析构");

    // 用本测试持有的监听 socket 制造端口占用，不接触外部服务。
    const auto address = Address();
    int blocker = socket(AF_INET, SOCK_STREAM, 0);
    assert(blocker >= 0);
    int reuse = 1;
    assert(setsockopt(blocker, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);
    if (bind(blocker, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        perror("test requires free 8080");
        close(blocker);
        return 1;
    }
    assert(listen(blocker, 8) == 0);
    {
        Server server(useET);
        assert(!server.Init());
    }
    assert(FdCount() == baseline + 1);
    assert(fcntl(blocker, F_GETFD) >= 0);
    close(blocker);
    for (int i = 0; i < 10; ++i) {
        {
            Server server(useET);
            assert(server.Init());
            assert(FdCount() == baseline + 3); // listen、epoll、eventfd
            assert(!server.Init());
        }
        assert(FdCount() == baseline);
    }
    std::puts("PASS 端口占用失败清理、重复初始化和十轮正常析构");

    // stdin 通常占据 fd 0；暂存并关闭后验证 socket 恰好得到 0 的清理路径。
    int savedInput = dup(STDIN_FILENO);
    assert(savedInput >= 0);
    close(STDIN_FILENO);
    {
        Server server(useET);
        assert(server.Init());
        assert(fcntl(STDIN_FILENO, F_GETFD) >= 0);
    }
    assert(fcntl(STDIN_FILENO, F_GETFD) == -1 && errno == EBADF);
    assert(dup2(savedInput, STDIN_FILENO) == STDIN_FILENO);
    close(savedInput);
    assert(FdCount() == baseline);

    // RLIMIT_NOFILE 仅改变本测试进程，制造真实 EMFILE，再恢复原限制。
    rlimit original{};
    assert(getrlimit(RLIMIT_NOFILE, &original) == 0);
    rlimit limited = original;
    limited.rlim_cur = 0;
    assert(setrlimit(RLIMIT_NOFILE, &limited) == 0);
    {
        Epoller epoller;
        assert(!epoller.init() && errno == EMFILE);
        Server server(useET);
        assert(!server.Init());
    }
    assert(setrlimit(RLIMIT_NOFILE, &original) == 0);
    assert(FdCount() == baseline);
    std::puts("PASS fd=0 析构和真实 EMFILE 初始化失败");

    int client = -1;
    {
        Server server(useET);
        assert(server.Init());
        client = socket(AF_INET, SOCK_STREAM, 0);
        assert(client >= 0);
        assert(connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        // 首次 Wait 返回监听事件，accept 完成后下一次 Wait 注入 EIO。
        waitsBeforeFailure = 1;
        assert(server.run() == 1);
        waitsBeforeFailure = -1;
        assert(FdCount() == baseline + 5); // listen、epoll、eventfd、accepted、client
    }
    assert(FdCount() == baseline + 1);
    timeval timeout{1, 0};
    assert(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    char byte;
    assert(recv(client, &byte, 1, 0) == 0);
    close(client);
    assert(FdCount() == baseline);
    std::puts("PASS 注入 epoll_wait 错误后 run 返回；析构关闭存活连接");
}
