/* 仅用于网络测试的 LD_PRELOAD 探针，不链接进服务器。
 * 默认在零偏移和已有进度时各注入 EAGAIN（仅 LT）。
 * SEND_PROBE_OBSERVE=1 禁止注入，用于 ET 真实部分发送及 EAGAIN 验证。
 * cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

static void record(const char *kind, size_t requested, ssize_t actual)
{
    int saved = errno;
    const char *path = getenv("SEND_PROBE_LOG");
    FILE *log = path ? fopen(path, "a") : NULL;
    if (log) {
        fprintf(log, "%s requested=%zu actual=%zd\n", kind, requested, actual);
        fclose(log);
    }
    errno = saved;
}

ssize_t send(int fd, const void *data, size_t size, int flags)
{
    static ssize_t (*real_send)(int, const void *, size_t, int);
    static int injected;
    static int progressed;
    if (!real_send) real_send = dlsym(RTLD_NEXT, "send");
    if (!real_send) abort();
    if (size > 1024 * 1024) {
        int capacity = 16384;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity)) < 0)
            abort();
        if (!getenv("SEND_PROBE_OBSERVE") &&
            (!injected || (injected == 1 && progressed))) {
            ++injected;
            errno = EAGAIN;
            record("injected EAGAIN", size, -1);
            return -1;
        }
    }
    ssize_t n = real_send(fd, data, size, flags);
    if (n > 0 && (size_t)n < size) {
        progressed = 1;
        record("partial send", size, n);
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        record("kernel EAGAIN", size, n);
    return n;
}

ssize_t recv(int fd, void *data, size_t size, int flags)
{
    static ssize_t (*real_recv)(int, void *, size_t, int);
    if (!real_recv) real_recv = dlsym(RTLD_NEXT, "recv");
    if (!real_recv) abort();
    ssize_t n = real_recv(fd, data, size, flags);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        record("kernel recv EAGAIN", size, n);
    return n;
}
