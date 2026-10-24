#include "server.h"
#include "../http/httphandler.h"
#include "../log/logger.h"
#include "connection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <exception>
#include <sys/eventfd.h>
#include <utility>
#include <vector>

#define LISTEN_SIZE 8

Server::~Server()
{
    pool_.reset();

    for (const auto& entry: connections_) close(entry.first);
    
    if (completedFd_ >= 0) close(completedFd_);
    if (listenfd_ >= 0) close(listenfd_);
}

void Server::ExpireIdleClients()
{
    const auto now = Connection::Clock::now();
    if (now < nextTimeoutCheck_) return;

    // 1 seconds scanning ...
    nextTimeoutCheck_ = now + std::chrono::seconds(1);
    std::vector<int> expired;
    const auto timeout = std::chrono::seconds(30);
    for (const auto& entry: connections_)
    {
        const auto& conn = entry.second;
        if (now - conn.lastActive >= timeout)
            expired.push_back(entry.first);
    }

    for (int fd: expired) CloseClient(fd, "idle_timeout");
}

bool Server::InitWorkers()
{
    // A notification counter represented by a file descriptor, 
    // often used to allow worker threads to wake up a main thread 
    // that is waiting on `epoll`.
    completedFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (completedFd_ < 0)
    {
        perror("eventfd");
        return false;
    }

    if (!epoller_.ADD(completedFd_, EPOLLIN | TriggerMode()))
    {
        perror("epoll add completion");
        return false;
    }

    constexpr size_t MAXTHREADPOOLCOUNT = 4;
    constexpr size_t MAXQUEUED = 64;

    try 
    {
        pool_ = std::make_unique<ThreadPool>(MAXTHREADPOOLCOUNT, MAXQUEUED);
    }
    catch (const std::exception& e)
    {
        std::fprintf(
            stderr, "Create thread pool: %s\n", e.what()
        );
        return false;
    }
    return true;
}

void Server::PublishCompletion(CompletedResponse result) noexcept
{
    {
        std::lock_guard<std::mutex> lock(completedMtx_);
        completed_.push(std::move(result));

        const std::uint64_t one = 1;
        ssize_t n;
        do 
        {
            n = write(completedFd_, &one, sizeof(one));
        } while (n < 0 && errno == EINTR);

        if (n == static_cast<ssize_t>(sizeof(one))) return;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;

        std::fputs("completion notifictaion failed\n", stderr);
        std::terminate();
    }
}

bool Server::HandleCompletions()
{
    while (true)
    {
        std::uint64_t cnt = 0;
        // Read acc_cnt from completedFd_ (eventfd)
        // `cnt` is used to clear notification not to do task...
        const ssize_t n = read(completedFd_, &cnt, sizeof(cnt));

        // Read again to check if any other worker threads have sent notifications in the meantime.
        if (n == static_cast<ssize_t>(sizeof(cnt))) continue;

        if (n < 0 && errno == EINTR) continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        
        std::fputs("completion notification read failed\n", stderr);
        return false;
    }

    std::queue<CompletedResponse> local;
    {
        std::lock_guard<std::mutex> lock(completedMtx_);
        local.swap(completed_);
    }

    while (!local.empty())
    {
        CompletedResponse result = std::move(local.front());
        local.pop();

        auto it = connections_.find(result.fd);

        if (it == connections_.end())
            continue;

        Connection& conn = it->second;
        // 1. id is different. Old connection may closed and the same 'fd' is reused by new connection.
        // So use id to preven sending old responset to new client.
        // 2. !conn.processing: This connection donot wait result from worker thread.
        if (conn.id != result.id || !conn.processing)
            continue;

        if (!result.ok)
        {
            Log(LogLevel::ERROR, "response_build_failed", result.fd, result.id);
            CloseClient(result.fd, "response_build_failed_close");
            continue;
        }

        // SEND MODEL
        conn.output = std::move(result.output);
        conn.sent = 0;
        conn.processing = false;
        conn.responding = true;

        uint32_t events = EPOLLOUT | TriggerMode();

        if (!conn.peerWriteClosed)
            events |= EPOLLRDHUP;

        if (!epoller_.MOD(result.fd, events))
        {
            perror("epoll apply response");
            CloseClient(result.fd);
            continue;
        }
    }
    return true;
}

uint32_t Server::TriggerMode() const
{
    return userET_ ? static_cast<uint32_t>(EPOLLET) : 0u;    
}

bool Server::SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

void Server::CloseClient(int fd, const char* reason, int error)
{
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    const auto id = it->second.id;

    const auto level = error != 0 ? LogLevel::ERROR : LogLevel::INFO;

    Log(level,reason, fd, id, error);

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

    constexpr size_t MAXLISTENFDSIZE = 8;

    if (listen(listenfd_, MAXLISTENFDSIZE) < 0)
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

    if (!epoller_.ADD(listenfd_, EPOLLIN | TriggerMode()))
    {
        perror("epoll add listen");
        return false;
    }
    
    if (!InitWorkers()) return false;
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
        if (!epoller_.ADD(clientfd, EPOLLIN | EPOLLRDHUP | TriggerMode()))
        {
            perror("epoll ctl add client");
            close(clientfd);
            continue;
        }

        Connection conn;
        conn.id = nextConnId_ ++ ;
        const auto id = conn.id;
        connections_.emplace(clientfd, std::move(conn));

        Log(LogLevel::INFO, "accepted", clientfd, id);
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

    // 工作线程只持有请求副本，完整挂断时可以立即删除连接。
    if (conn.processing && (ready & EPOLLHUP))
    {
        CloseClient(fd);
        return;
    }

    if (!conn.peerWriteClosed && (ready & (EPOLLRDHUP | EPOLLHUP)))
    {
        conn.peerWriteClosed = true;

        if (conn.responding || conn.processing)
        {
            uint32_t events = TriggerMode();
            if (conn.responding)
                events |= EPOLLOUT;

            // 已记录半关闭，取消 RDHUP 关注，防止 LT 持续唤醒。
            if (!epoller_.MOD(fd, events))
            {
                perror("epoll modify after half-close");
                CloseClient(fd);
                return;
            }
        }
    }

    // 等待结果时不再读取或重复提交同一个请求。
    if (conn.processing)
        return;

    // CHECK did at least one of these events occur?
    if (!conn.responding && (ready & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)))
    {
        char buf[BUFFER_SIZE];

        while (true)
        {
            const ssize_t n = recv(fd , buf, BUFFER_SIZE, 0);
            if (n > 0)
            {
                conn.lastActive = Connection::Clock::now();
                conn.input.Append(buf, static_cast<size_t>(n));
                
                if (!PrepareResponse(fd, conn)) return;
                if (conn.processing) return;

                // 请求尚未完整，继续读取至 EAGAIN。
                continue;
            }
            if (n == 0)
            {
                CloseClient(fd);
                return;
            }
            
            if (errno == EINTR) continue;
            
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            // perror("recv");
            const int error = errno;
            CloseClient(fd, "recv_error", error);
            return;
        }
    }
    
    // SENDING STAGE ...
    if (conn.responding && (ready & (EPOLLOUT | EPOLLHUP)))
    {
        while (conn.sent < conn.output.size())
        {
            const ssize_t n = send(fd, conn.output.data() + conn.sent
                        , conn.output.size() - conn.sent,
                        MSG_NOSIGNAL);
           
            if (n > 0)
            {
                conn.lastActive = Connection::Clock::now();
                conn.sent += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            const int error = n < 0 ? errno : 0;
            CloseClient(fd, n < 0 ? "send_error" : "send_zero", error);
            return;
        }

        // Current sending finshed
        if (!conn.keepAlive || conn.peerWriteClosed)
        {
            CloseClient(fd);
            return;
        }
        
        // Reset the parser, but do not reset the connection
        conn.parser = HttpParser{};
        conn.output.clear();
        conn.sent = 0;
        conn.responding = false;
        conn.keepAlive = false;

        // Do not clear input because it may next request data.
        if (!PrepareResponse(fd, conn)) return;

        if (conn.processing)
        {
            // 已缓存的下一请求已提交，等待工作线程的完成通知。
            return;
        }

        // 没有完整的后续请求，恢复读事件。
        if (!epoller_.MOD(
                fd,
                EPOLLIN | EPOLLRDHUP | TriggerMode()))
                // SEND MODEL
        {
            perror("epoll modify read");
            CloseClient(fd);
            return;
        }
        return;
    }
}

bool Server::PrepareResponse(int fd, Connection& conn)
{
    const auto result = conn.parser.Parse(conn.input);
    if (result == HttpParseResult::NeedMore) return true;

    ++ conn.completedRequests;

    constexpr size_t MAX_REQUESTS = 16;
    conn.keepAlive = result == HttpParseResult::Complete 
                     && conn.parser.Request().WantsKeepAlive()
                     && !conn.peerWriteClosed
                     && conn.completedRequests < MAX_REQUESTS;

    conn.processing = true;
    conn.responding = false;

    // 暂停普通读写事件，只保留尚未观察到的半关闭通知。
    uint32_t waitingEvents = TriggerMode();
    if (!conn.peerWriteClosed)
        waitingEvents |= EPOLLRDHUP;

    if (!epoller_.MOD(fd, waitingEvents))
    {
        perror("epoll pause client");
        CloseClient(fd);
        return false;
    }

    bool accepted = false;

    try 
    {
        accepted = pool_->Submit(
            [
                this, fd, id = conn.id, request = conn.parser.Request(), result, keepAlive = conn.keepAlive
            ]
            {
                CompletedResponse completed;
                completed.fd = fd;
                completed.id = id;

                try
                {
                    completed.output = BuildResponse(
                        request, result, keepAlive, auth_);
                    completed.ok = true;
                } 
                catch (...) 
                {
                    completed.ok = false;
                }

                PublishCompletion(std::move(completed));
            }
        );
    }
    catch (const std::exception& e)
    {
        std::fprintf(
               stderr,
               "submit response task: %s\n",
               e.what()
           );
    }
    if (!accepted)
    {
        Log(LogLevel::WARN, "task_rejected", fd, conn.id);
        CloseClient(fd, "task_rejected_close");
        return false;
    }

    return true;
}

int Server::run()
{
    std::printf("%s Epoll Server: http://127.0.0.1:8080\n",
                userET_ ? "ET" : "LT");

    while (true)
    {
        const int cnt = epoller_.Wait(1000);

        if (cnt < 0)
        {
            if (errno == EINTR)
            {
                ExpireIdleClients();
                continue;
            }
            perror("epoll_wait");
            return 1;
        }

        for (int i = 0; i < cnt; ++ i)
        {
            const auto& event = epoller_.Event(static_cast<size_t>(i));
            int fd = event.data.fd;

            if (fd == listenfd_) AcceptClients();
            else if (fd == completedFd_)
            {
                if (!HandleCompletions()) return 1;
            }
            else HandlerClientEvent(fd, event.events);
        }

        ExpireIdleClients();
    }
}
