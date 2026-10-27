// 定向集成测试：真实 Server/ThreadPool/epoll/eventfd/socketpair。
// 仅本目标用 -fno-access-control 访问状态，并链接可控制的 BuildResponse 测试替身。
// 不修改生产接口，不替换正式服务器中的 HTTP handler。
#include "../code/server/server.h"
#include "../code/http/httphandler.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <future>
#include <cerrno>
#include <map>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono;

static int injectedWaits = 0;
extern "C" int __real_epoll_wait(int, epoll_event*, int, int);
extern "C" int __wrap_epoll_wait(int fd, epoll_event* events, int capacity, int timeout)
{
    if (injectedWaits > 0) {
        errno = injectedWaits-- == 2 ? EINTR : EIO;
        return -1;
    }
    return __real_epoll_wait(fd, events, capacity, timeout);
}

namespace {
std::mutex gateMutex;
std::condition_variable gateCv;
bool released = false;
int slowStarted = 0;
int totalCalls = 0;
std::thread::id eventThread;

void ResetGate()
{
    std::lock_guard<std::mutex> lock(gateMutex);
    released = false;
    slowStarted = totalCalls = 0;
    eventThread = std::this_thread::get_id();
}
void Release()
{
    { std::lock_guard<std::mutex> lock(gateMutex); released = true; }
    gateCv.notify_all();
}
void AwaitSlow(int count)
{
    std::unique_lock<std::mutex> lock(gateMutex);
    assert(gateCv.wait_for(lock, seconds(3), [=] { return slowStarted >= count; }));
}
int Fds()
{
    DIR* dir = opendir("/proc/self/fd");
    assert(dir);
    int result = 0;
    while (const auto* entry = readdir(dir))
        if (entry->d_name[0] != '.') ++result;
    closedir(dir);
    return result;
}
struct Peer {
    int serverFd;
    int clientFd;
    Peer(int server, int client) : serverFd(server), clientFd(client) {}
    Peer(const Peer&) = delete;
    Peer(Peer&& other) noexcept : serverFd(other.serverFd), clientFd(other.clientFd)
    { other.clientFd = -1; }
    ~Peer() { if (clientFd >= 0) close(clientFd); } // 服务端 fd 由 Server 拥有。
};
void Init(Server& s, std::size_t threads = 2, std::size_t capacity = 2)
{
    // 同时覆盖日志不能把 64 位连接 id 窄化为 int。
    s.nextConnId_ = (std::uint64_t{1} << 32) + 1;
    assert(s.epoller_.init());
    assert(s.InitWorkers());
    // 缩小容量以用少量连接稳定覆盖饱和，不进行高并发压测。
    s.pool_.reset(new ThreadPool(threads, capacity));
}
Peer Add(Server& s, int reuseFd = -1)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0);
    if (reuseFd >= 0 && pair[0] != reuseFd) {
        if (pair[1] == reuseFd) std::swap(pair[0], pair[1]);
        else {
            assert(dup2(pair[0], reuseFd) == reuseFd);
            close(pair[0]);
            pair[0] = reuseFd;
        }
    }
    assert(s.epoller_.ADD(pair[0], EPOLLIN | EPOLLRDHUP | s.TriggerMode()));
    Connection conn;
    assert(conn.state == ConnectionState::READING);
    conn.id = s.nextConnId_++;
    assert(s.connections_.emplace(pair[0], std::move(conn)).second);
    return {pair[0], pair[1]};
}
void Submit(Server& s, const Peer& peer, const std::string& body)
{
    const std::string req = "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Type: text/plain\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
    assert(send(peer.clientFd, req.data(), req.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(req.size()));
    s.HandlerClientEvent(peer.serverFd, EPOLLIN);
    const auto it = s.connections_.find(peer.serverFd);
    if (it != s.connections_.end())
        assert(it->second.state == ConnectionState::PROCESSING);
}
void Pump(Server& s)
{
    const int n = s.epoller_.Wait(20);
    assert(n >= 0);
    for (int i = 0; i < n; ++i) {
        const auto ev = s.epoller_.Event(i);
        if (ev.data.fd == s.completedFd_) assert(s.HandleCompletions());
        else s.HandlerClientEvent(ev.data.fd, ev.events);
    }
}
template<class Predicate>
void Until(Server& s, Predicate done)
{
    const auto deadline = steady_clock::now() + seconds(3);
    while (!done()) {
        assert(steady_clock::now() < deadline);
        Pump(s);
    }
}
void Response(Server& s, const Peer& peer, const std::string& body)
{
    Until(s, [&] { return !s.connections_.count(peer.serverFd); });
    std::string data;
    char buf[4096];
    while (true) {
        const auto n = recv(peer.clientFd, buf, sizeof(buf), 0);
        if (n == 0) break;
        assert(n > 0);
        data.append(buf, n);
    }
    const auto split = data.find("\r\n\r\n");
    assert(split != std::string::npos);
    assert(data.substr(split + 4) == body);
}
} // namespace

// 可控替身：指定任务停在屏障，其他任务仍可完成；检查不是事件线程执行。
std::string BuildResponse(const HttpRequest& request, HttpParseResult result, bool, AuthService*)
{
    assert(result == HttpParseResult::Complete);
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        assert(std::this_thread::get_id() != eventThread);
        ++totalCalls;
        if (request.Body().compare(0, 4, "slow") == 0) {
            ++slowStarted;
            gateCv.notify_all();
            gateCv.wait(lock, [] { return released; });
        }
    }
    if (request.Body() == "throw") throw std::runtime_error("controlled failure");
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(request.Body().size())
        + "\r\nConnection: close\r\n\r\n" + request.Body();
}

static void Isolation(bool et)
{
    ResetGate();
    Server s(et); Init(s);
    auto slow = Add(s);
    Submit(s, slow, "slow");
    AwaitSlow(1);
    // 重复就绪事件不能再次提交同一个已完成解析的请求。
    s.HandlerClientEvent(slow.serverFd, EPOLLIN);
    s.HandlerClientEvent(slow.serverFd, EPOLLOUT);
    auto fast = Add(s);
    Submit(s, fast, "fast");
    Response(s, fast, "fast");
    assert(s.connections_.at(slow.serverFd).state == ConnectionState::PROCESSING);
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        assert(totalCalls == 2 && !released);
    }
    Release();
    Response(s, slow, "slow");
    std::puts("PASS 慢构造屏障未释放时快响应完成，单连接不重复提交");
}

static void Saturation(bool et)
{
    ResetGate();
    Server s(et); Init(s);
    auto a = Add(s); Submit(s, a, "slow");
    auto b = Add(s); Submit(s, b, "slow");
    AwaitSlow(2);
    auto c = Add(s); Submit(s, c, "queued1");
    auto d = Add(s); Submit(s, d, "queued2");
    auto rejected = Add(s); Submit(s, rejected, "reject");
    assert(!s.connections_.count(rejected.serverFd));
    char byte;
    assert(recv(rejected.clientFd, &byte, 1, 0) == 0);
    Release();
    Response(s, a, "slow"); Response(s, b, "slow");
    Response(s, c, "queued1"); Response(s, d, "queued2");
    { std::lock_guard<std::mutex> lock(gateMutex); assert(totalCalls == 4); }
    std::puts("PASS 2 个运行任务+2 个排队任务，额外提交被关闭拒绝，无同步兜底");
}

static void StaleAndFailure(bool et)
{
    ResetGate();
    Server s(et); Init(s);
    auto old = Add(s); Submit(s, old, "slow"); AwaitSlow(1);
    const auto oldId = s.connections_.at(old.serverFd).id;
    // 模拟 epoll 报告完整挂断，强制复用完全相同的 fd 数值。
    s.HandlerClientEvent(old.serverFd, EPOLLHUP);
    assert(!s.connections_.count(old.serverFd));
    auto fresh = Add(s, old.serverFd);
    assert(s.connections_.at(fresh.serverFd).id != oldId);
    // 新连接也处于 PROCESSING：旧结果必须靠 id 而不是阶段检查拒绝。
    Submit(s, fresh, "slow-new"); AwaitSlow(2);
    // 确定性注入一份旧 id 完成结果，避免两个真实任务完成先后掩盖 id 缺陷。
    Server::CompletedResponse stale;
    stale.fd = fresh.serverFd;
    stale.id = oldId;
    stale.output = "must-not-reach-new-client";
    stale.ok = true;
    s.PublishCompletion(std::move(stale));
    assert(s.HandleCompletions());
    assert(s.connections_.at(fresh.serverFd).state == ConnectionState::PROCESSING);
    assert(s.connections_.at(fresh.serverFd).output.empty());
    Release();
    s.pool_.reset(); // 等两份结果均已入队，再同时处理。
    assert(s.HandleCompletions());
    assert(s.connections_.at(fresh.serverFd).state == ConnectionState::WRITING);
    Response(s, fresh, "slow-new");
    s.pool_.reset(new ThreadPool(1, 2));
    auto failed = Add(s); Submit(s, failed, "throw");
    Until(s, [&] { return !s.connections_.count(failed.serverFd); });
    auto next = Add(s); Submit(s, next, "survived");
    Response(s, next, "survived");
    std::puts("PASS 后台任务挂断/同 fd 新 id、晚到结果过滤、构造异常后仍可服务");
}

static void TimeoutChecks(bool et)
{
    ResetGate();
    Server s(et); Init(s);
    auto idle = Add(s);
    auto active = Add(s);
    const auto old = Connection::Clock::now() - seconds(31);
    s.connections_.at(idle.serverFd).lastActive = old;
    s.connections_.at(active.serverFd).lastActive = old;
    // 实际收到部分请求，必须续期；随后真实 EAGAIN 不应再更新时间。
    const char prefix[] = "GET / HTTP/1.1\r\nHost:";
    assert(send(active.clientFd, prefix, sizeof(prefix) - 1, MSG_NOSIGNAL) == sizeof(prefix) - 1);
    s.HandlerClientEvent(active.serverFd, EPOLLIN);
    assert(s.connections_.at(active.serverFd).state == ConnectionState::READING);
    const auto refreshed = s.connections_.at(active.serverFd).lastActive;
    assert(refreshed > old);
    s.HandlerClientEvent(active.serverFd, EPOLLIN);
    assert(s.connections_.at(active.serverFd).lastActive == refreshed);
    s.nextTimeoutCheck_ = Connection::Clock::time_point{};
    s.ExpireIdleClients();
    assert(!s.connections_.count(idle.serverFd));
    assert(s.connections_.count(active.serverFd));
    // 同一秒不重复扫描；到下一轮才删除。
    s.connections_.at(active.serverFd).lastActive = old;
    s.ExpireIdleClients();
    assert(s.connections_.count(active.serverFd));
    s.nextTimeoutCheck_ = Connection::Clock::time_point{};
    s.ExpireIdleClients();
    assert(!s.connections_.count(active.serverFd));

    auto writer = Add(s);
    auto& conn = s.connections_.at(writer.serverFd);
    conn.lastActive = old;
    conn.state = ConnectionState::WRITING;
    conn.keepAlive = true;
    conn.output.assign(8 * 1024 * 1024, 'x');
    const int capacity = 4096;
    assert(setsockopt(writer.serverFd, SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity)) == 0);
    s.HandlerClientEvent(writer.serverFd, EPOLLOUT);
    assert(conn.sent > 0 && conn.sent < conn.output.size());
    const auto written = conn.lastActive;
    assert(written > old);
    s.HandlerClientEvent(writer.serverFd, EPOLLOUT); // 对端未读取，真实 EAGAIN。
    assert(conn.lastActive == written);
    conn.lastActive = old;
    s.nextTimeoutCheck_ = Connection::Clock::time_point{};
    s.ExpireIdleClients();
    assert(!s.connections_.count(writer.serverFd));

    auto slow = Add(s); Submit(s, slow, "slow"); AwaitSlow(1);
    s.connections_.at(slow.serverFd).lastActive = old;
    s.nextTimeoutCheck_ = Connection::Clock::time_point{};
    s.ExpireIdleClients();
    assert(!s.connections_.count(slow.serverFd));
    auto fresh = Add(s, slow.serverFd);
    Submit(s, fresh, "slow-new"); AwaitSlow(2);
    Release();
    s.pool_.reset();
    assert(s.HandleCompletions());
    Response(s, fresh, "slow-new");
    std::puts("PASS 注入过期时间：接收/发送续期、EAGAIN 不续期、扫描节流、后台超时及旧结果过滤");
}

static void InterruptedTimeout(bool et)
{
    ResetGate();
    Server s(et); Init(s);
    auto peer = Add(s);
    s.connections_.at(peer.serverFd).lastActive = Connection::Clock::now() - seconds(31);
    s.nextTimeoutCheck_ = Connection::Clock::time_point{};
    injectedWaits = 2; // 第一次 EINTR，第二次 EIO 让 run 可控退出。
    assert(s.run() == 1);
    assert(!s.connections_.count(peer.serverFd));
    std::puts("PASS 注入 EINTR 路径也检查超时");
}

static void Shutdown(bool et)
{
    ResetGate();
    std::promise<void> leaving;
    auto leavingFuture = leaving.get_future();
    auto owner = std::async(std::launch::async, [&] {
        { std::lock_guard<std::mutex> lock(gateMutex); eventThread = std::this_thread::get_id(); }
        Server s(et); Init(s, 1, 2);
        auto a = Add(s); Submit(s, a, "slow");
        AwaitSlow(1);
        auto b = Add(s); Submit(s, b, "queued");
        leaving.set_value();
        // Peer 先销毁、Server 随后析构；仍需 join 两个后台任务后才关 eventfd。
    });
    assert(leavingFuture.wait_for(seconds(3)) == std::future_status::ready);
    assert(owner.wait_for(milliseconds(50)) == std::future_status::timeout);
    Release();
    assert(owner.wait_for(seconds(3)) == std::future_status::ready);
    owner.get();
    { std::lock_guard<std::mutex> lock(gateMutex); assert(totalCalls == 2); }
    std::puts("PASS 后台运行+排队任务存在时析构，join 完成后回收通知资源");
}

int main(int argc, char** argv)
{
    const bool et = argc > 1 && std::strcmp(argv[1], "ET") == 0;
    const int baseline = Fds();
    Isolation(et); assert(Fds() == baseline);
    Saturation(et); assert(Fds() == baseline);
    StaleAndFailure(et); assert(Fds() == baseline);
    TimeoutChecks(et); assert(Fds() == baseline);
    InterruptedTimeout(et); assert(Fds() == baseline);
    Shutdown(et); assert(Fds() == baseline);
}
