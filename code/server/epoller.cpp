#include "epoller.h"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <sys/epoll.h>
#include <unistd.h>

Epoller::~Epoller()
{
    if (epollFd_ >= 0) close(epollFd_);
}

bool Epoller::init()
{
    if (epollFd_ >= 0)
    {
        errno = EALREADY;
        return false;
    }

    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    return epollFd_ >= 0;
}

bool Epoller::Control(int operation, int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epollFd_, operation, fd, &ev) == 0;
}

bool Epoller::ADD(int fd, uint32_t events)
{
    return Control(EPOLL_CTL_ADD, fd, events);
}

bool Epoller::MOD(int fd, uint32_t events)
{
    return Control(EPOLL_CTL_MOD, fd, events);
}

bool Epoller::DEL(int fd)
{
    return Control(EPOLL_CTL_DEL, fd, 0);
}

int Epoller::Wait(int timeoutMs)
{
    int cnt = epoll_wait(epollFd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);

    readyCnt_ = cnt > 0 ? cnt : 0;
    return cnt;
}

const epoll_event& Epoller::Event(size_t index) const
{
    // Out-of-bounds check
    assert(index < static_cast<size_t>(readyCnt_));
    return events_[index];
}
