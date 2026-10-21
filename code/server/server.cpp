#include "server.h"
#include "../http/httphandler.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_SIZE 8

Server::~Server()
{
    for (const auto& entry: connections_) close(entry.first);

    if (listenfd_ >= 0) close(listenfd_);
}

bool Server::SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

void Server::CloseClient(int fd)
{
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    if (!epoller_.DEL(fd))
        perror("epoll delete client");
    close(fd);
    connections_.erase(it);
}

bool Server::Init()
{
    if (listenfd_ >= 0)
    {
        errno = EALREADY;
        perror("server already initialized");
        return false;
    }

    // TCP
    listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd_ < 0)
    {
        perror("socket");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);

    int reuse = 1;
    
    // When server is quitted, TCP_CONNECTION may still in `TIME_WAIT`
    // 此时重启可能报错：Address already in use
    // SO_REUSEADDR 可以复用
    if (setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt SO_REUSEADDR");
        return false;
    }

    if (bind(listenfd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        perror("bind");
        return false;
    }

    if (listen(listenfd_, 8) < 0)
    {
        perror("listen");
        return false;
    }

    if (!SetNonBlocking(listenfd_))
    {
        perror("nonblocking listen");
        return false;
    }

    if (!epoller_.init())
    {
        perror("epoll_create1");
        return false;
    }

    if (!epoller_.ADD(listenfd_, EPOLLIN))
    {
        perror("epoll add listen");
        return false;
    }
        
    return true;
}

void Server::AcceptClients()
{
    while (true)
    {
        int clientfd = accept(listenfd_, nullptr, nullptr);
        if (clientfd < 0)
        {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            break;
        }
        if (!SetNonBlocking(clientfd))
        {
            perror("nonblocking client");
            close(clientfd);
            continue;
        }
        // Can read | Close send direction
        if (!epoller_.ADD(clientfd, EPOLLIN | EPOLLRDHUP))
        {
            perror("epoll ctl add client");
            close(clientfd);
            continue;
        }

        connections_.emplace(clientfd, Connection{});
    } 
}

void Server::HandlerClientEvent(int fd, uint32_t ready)
{
    constexpr std::size_t BUFFER_SIZE = 4096;
    auto it = connections_.find(fd);

    if (it == connections_.end()) return;

    if (ready & EPOLLERR)
    {
        CloseClient(fd);
        return ;
    }

    Connection& conn = it->second;

    // CHECK did at least one of these events occur?
    if (!conn.responding && (ready & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)))
    {
        char buf[BUFFER_SIZE];
        ssize_t n;

        do
        {
            n = recv(fd, buf, BUFFER_SIZE, 0);
        } while (n < 0 && errno == EINTR);

        if (n == 0)
        {
            CloseClient(fd);
            return;
        }
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("recv");
                CloseClient(fd);
            }
            return ;
        }

        conn.input.Append(buf, static_cast<size_t>(n));
        const auto result = conn.parser.Parse(conn.input);

        if (result != HttpParseResult::NeedMore)
        {
            conn.output = BuildResponse(conn.parser.Request(), result);
            conn.sent = 0;
            conn.responding = true;

            if (!epoller_.MOD(fd, EPOLLOUT))
            {
                perror("epoll modify write");
                CloseClient(fd);
                return;
            }
        }
    }

    if (conn.responding && (ready & (EPOLLOUT | EPOLLHUP)))
    {
        ssize_t n;
        do
        {
            n = send(fd, conn.output.data() + conn.sent
                        , conn.output.size() - conn.sent,
                        MSG_NOSIGNAL);
        }while (n < 0 && errno == EINTR);

        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("send");
                CloseClient(fd);
            }
            return;
        }
        if(n == 0)
        {
            CloseClient(fd);
            return;
        }
        
        conn.sent += static_cast<size_t>(n);

        if (conn.sent == conn.output.size())
        {
            CloseClient(fd);
            return;
        }
    }
}

int Server::run()
{
    std::puts("LT Epoll Server: http://127.0.0.1:8080");

    while (true)
    {
        int cnt = epoller_.Wait();

        if (cnt < 0)
        {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            return 1;
        }

        for (int i = 0; i < cnt; ++ i)
        {
            const auto& event = epoller_.Event(static_cast<size_t>(i));
            int fd = event.data.fd;

            if (fd == listenfd_) AcceptClients();
            else HandlerClientEvent(fd, event.events);
        }
    }
}
