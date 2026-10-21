/**
 *  Class Server
 *  
 *  Manage listen socket
 *  Manage epoll fd and ready event array
 *  Manage client fd and its status
 */

#pragma once

#include "connection.h"
#include "epoller.h"

#include <cstdint>
#include <unordered_map>

class Server
{
    // field
    int listenfd_{-1};
    Epoller epoller_;
    std::unordered_map<int, Connection> connections_;

public:
    Server() = default;
    ~Server();

    bool Init();
    int run();

private:
    static bool SetNonBlocking(int fd);

    void AcceptClients();

    void HandlerClientEvent(int fd, uint32_t ready);

    void CloseClient(int fd);

private:
    Server(const Server&) = delete;
    Server& operator= (const Server&) = delete;
};
