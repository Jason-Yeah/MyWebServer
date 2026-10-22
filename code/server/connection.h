#pragma once

#include "../buffer/buffer.h"
#include "../http/httpparser.h"

#include <cstdint>
#include <cstddef>
#include <string>

struct Connection
{
    Buffer input;
    HttpParser parser;

    std::string output;
    std::size_t sent = 0;

    /*
        processing responding
        false      false      receving request
        true       false      waiting worker thread constructing response
        false      true       sending response
    */
    bool responding = false;

    // Whether to continue using the connection after the `current response` has been sent.
    bool keepAlive = false;         
    // Whether to has been observed that the peer has closed its sending direction.
    bool peerWriteClosed = false;
    // The number of requests for which parsing has been completed on this connection.
    size_t completedRequests{0};

    // Whether the current connection has a background response-construction task.
    bool processing = false;

    // Distinguish between different connections before and after an fd is reused.
    std::uint64_t id = 0;
};