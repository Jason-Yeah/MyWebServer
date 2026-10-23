#pragma once

#include <cstdint>

enum class LogLevel
{
    INFO,
    WARN,
    ERROR
};

bool Log(LogLevel level, const char* event,
         int fd = -1, std::uint64_t id = 0, int error = 0);
