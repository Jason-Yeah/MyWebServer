# EPOLL

## EPOLL Marco

EPOLLIN     Can try to read
EPOLLOUT    Can try to write
EPOLLRDHUP  The remote end has closed the sending direction
EPOLLHUP    Connection hang up
EPOLLERR    error

## EPOLL LT

As long as the readability condition remains met, subsequent `epoll_wait` calls can continue to provide notifications.

## EPOLL ET

ET does not guarantee repeated notifications for a socket that remains in a ready state.

### Basic handling method

1. Receive `Can Read` notification.
2. recv LOOP
3. `EAGIN` when no data come.
