#pragma once

#include <string>

struct StaticFileResult
{
    std::string status;
    std::string contentType;
    std::string body;
};

StaticFileResult LoadStaticFile(const std::string& target);
