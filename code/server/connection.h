#pragma once

#include "../buffer/buffer.h"
#include "../http/httpparser.h"

#include <cstddef>
#include <string>

struct Connection
{
    Buffer input;
    HttpParser parser;

    std::string output;
    std::size_t sent = 0;

    bool responding = false;
};