/**
 *  Class Epoller
 *  
 *  Manage epoll fd, epoll_events
 *  Manage epoll all operation: ADD MOD DEL Wait
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sys/epoll.h>

#define EPOLL_EVENTS_SIZE 64

class Epoller
{
public:
    Epoller() = default;
    ~Epoller();

    bool init();

    bool ADD(int fd, uint32_t events);
    bool MOD(int fd, uint32_t events);
    bool DEL(int fd);

    int Wait(int timeoutMs = -1);

    const epoll_event& Event(size_t index) const;

private:
    bool Control(int operation, int fd, uint32_t events);

    int epollFd_ = -1;
    std::array<epoll_event, EPOLL_EVENTS_SIZE> events_{};
    int readyCnt_ = 0;

private:
    Epoller(const Epoller&) = delete;
    Epoller& operator= (const Epoller&) = delete;
};
