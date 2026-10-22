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

class Server
{
    // field
    int listenfd_{-1};
    Epoller epoller_;
    std::unordered_map<int, Connection> connections_;
    bool userET_{false};

public:
    // Server() = default;
    explicit Server(bool userET = false) : userET_(userET) {} 
    ~Server();

    bool Init();
    int run();

private:
    static bool SetNonBlocking(int fd);

    void AcceptClients();

    void HandlerClientEvent(int fd, uint32_t ready);

    void CloseClient(int fd);

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

        std::string output;
        bool ok = false;
    };

    bool InitWorkers();

    void PublishCompletion(CompletedResponse result) noexcept;

    bool HandleCompletions();

    std::uint64_t nextConnId_{1};

    int completedFd_{-1};

    std::mutex completedMtx_;
    std::queue<CompletedResponse> completed_;

    std::unique_ptr<ThreadPool> pool_;
};
