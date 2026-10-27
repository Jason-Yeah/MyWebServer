#pragma once

#include "../buffer/buffer.h"
#include "../http/httpparser.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <string>

enum class ConnectionState
{
    READING,
    PROCESSING,
    WRITING
};

struct Connection
{
    Buffer input;
    HttpParser parser;

    std::string output;
    std::size_t sent = 0;

    ConnectionState state = ConnectionState::READING;

    // Whether to continue using the connection after the `current response` has been sent.
    bool keepAlive = false;         
    // Whether to has been observed that the peer has closed its sending direction.
    bool peerWriteClosed = false;
    // The number of requests for which parsing has been completed on this connection.
    size_t completedRequests{0};

    // Distinguish between different connections before and after an fd is reused.
    std::uint64_t id = 0;

    /*
        Monotonic clock:
        Time only advances forward and is unaffected by manual changes to the system
        date or similar adjustments.
    */
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastActive = Clock::now();
};