#include "logger.h"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <mutex>

bool Log(LogLevel level, const char* event,
         int fd, std::uint64_t id, int error)
{
    const int savedErrno = errno;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    const char* levelName = "UNKNOWN";

    switch (level) 
    {
        case LogLevel::INFO:
            levelName = "INFO";
            break;
        case LogLevel::WARN:
            levelName = "WARN";
            break;
        case LogLevel::ERROR:
            levelName = "ERROR";
            break;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};

    if (now == static_cast<std::time_t>(-1) || localtime_r(&now, &local) == nullptr)
    {
        errno = savedErrno;
        return false;
    }

    char timestamp[32];
    if (std::strftime(timestamp, sizeof(timestamp), 
        "%Y-%m-%d %H:%M:%S", &local) == 0)
    {
        errno = savedErrno;
        return false;
    }

    const int written = std::fprintf(
        stderr,
        "[%s] [%s] event=%s fd=%d id=%llu errno=%d\n",
        timestamp, levelName,event ? event: "unknown", fd, static_cast<unsigned long long>(id), error
    );

    const bool ok = written >= 0 && std::fflush(stderr) == 0;

    errno = savedErrno;
    return ok;
}
