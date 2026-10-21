/**
 * Non-blocking Socket: Don't keep waiting when there is no data coming.
 * EPOLL: Tell which connection recv data now, and do not need to polling each one.
 */

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstdio>

int main()
{
    int fd[2];
    /**
     * `socketpair()` creates a pair of pre-connected local sockets; 
     * it does not require `bind`, `listen`, or `accept` calls, 
     * and does not use TCP ports.
     */
    // [Macros] AF_UNIX: local communication; AF_INET: IPv4
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) < 0)
    {
        perror("sockerpair");
        return 1;
    }

    int epbf = -1;
    auto cleanup = [&]()
    {
        if (epbf >= 0) close(epbf);
        close(fd[0]);
        close(fd[1]);
    };

    // Retrieve the current file status flags for fd[0] and save them into flag.
    int flag = fcntl(fd[0], F_GETFL, 0);

    if (flag < 0 || fcntl(fd[0], F_SETFL, flag | O_NONBLOCK) < 0)
    {
        perror("fcntl");
        cleanup();
        return 1;
    }

    char buf[64];
    ssize_t n = recv(fd[0], buf, sizeof(buf), 0);

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        std::puts("1. no data now, `recv()` ret...");
    else
    {
        std::fputs("non-blocking recv result not right\n", stderr);
        cleanup();
        return 1;
    }

    epbf = epoll_create1(EPOLL_CLOEXEC);
    if (epbf < 0)
    {
        perror("epoll_create1");
        cleanup();
        return 1;
    }

    epoll_event ev{};
    // 只要 Socket 仍有未读数据，后续等待时还可以继续得到可读通知。
    ev.events = EPOLLIN; // LT
    ev.data.fd = fd[0];

    if (epoll_ctl(epbf, EPOLL_CTL_ADD, fd[0], &ev) < 0)
    {
        perror("epoll_ctl");
        cleanup();
        return 1;
    }

    epoll_event ready[4];
    int cnt = epoll_wait(epbf, ready, 4, 0);

    if (cnt != 0)
    {
        std::fputs("预期此时没有就绪事件\n", stderr);
        cleanup();
        return 1;
    }

    std::puts("2. 没有数据，epoll 没有报告事件");

    const char text[] = "hello";
    if (send(fd[1], text, sizeof(text) - 1, MSG_NOSIGNAL) 
        != static_cast<ssize_t>(sizeof(text) - 1))
    {
        std::fputs("实验数据发送失败或不完整\n", stderr);
        cleanup();
        return 1;
    }

    cnt = epoll_wait(epbf, ready, 4, 1000);
    if (cnt <= 0)
    {
        std::fputs("未取得预期的可读事件\n", stderr);
        cleanup();
        return 1;
    }
    std::printf("3. epoll 报告 fd=%d 已就绪\n",
                ready[0].data.fd);

    n = recv(fd[0], buf, sizeof(buf), 0);

    if (n <= 0)
    {
        std::fputs("读取实验数据失败\n", stderr);
        cleanup();
        return 1;
    }

    std::printf("4. 收到 %zd 字节：", n);
    std::fwrite(buf, 1, static_cast<size_t>(n), stdout);
    std::putchar('\n');

    cleanup();
    return 0;
}