/**
 *  Class Server
 *  
 *  Manage listen socket
 *  Manage epoll fd and ready event array
 *  Manage client fd and its status
 */

#pragma once

#include "../pool/threadpool.h"
#include "connection.h"
#include "epoller.h"

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>

class AuthService;

class Server
{
    // field
    int listenfd_{-1};
    Epoller epoller_;
    std::unordered_map<int, Connection> connections_;
    bool userET_{false};

    AuthService* auth_{nullptr};

public:
    // Server() = default;
    explicit Server(bool userET = false, AuthService* auth = nullptr) : userET_(userET), auth_(auth) {} 
    ~Server();

    bool Init();
    int run();

private:
    static bool SetNonBlocking(int fd);

    void AcceptClients();

    void HandlerClientEvent(int fd, uint32_t ready);

    void CloseClient(int fd, const char* reason = "closed", int error = 0);

    uint32_t TriggerMode() const;

    bool PrepareResponse(int fd, Connection& conn);

private:
    Server(const Server&) = delete;
    Server& operator= (const Server&) = delete;

private:
    struct CompletedResponse
    {
        int fd = -1;
        std::uint64_t id = 0;

        std::string output; // response
        bool ok = false; // construction process is successful?
    };

    bool InitWorkers();

    void PublishCompletion(CompletedResponse result) noexcept;

    bool HandleCompletions();

    std::uint64_t nextConnId_{1};

    int completedFd_{-1};

    std::mutex completedMtx_;
    std::queue<CompletedResponse> completed_; // Function objects that have already been executed.

    std::unique_ptr<ThreadPool> pool_;

    void ExpireIdleClients();
    Connection::Clock::time_point nextTimeoutCheck_ = Connection::Clock::now();
};
