#pragma once

#include "httprequest.h"
#include <cstddef>

class Buffer;

enum class HttpParseResult
{
    NeedMore,
    Complete,
    BadRequest,
    MethodNotAllowed,
    HeaderTooLarge,
    BodyTooLarge,
    UnsupportedTransferEncoding
};

class HttpParser
{
public:
    HttpParseResult Parse(Buffer& buf);

    const HttpRequest& Request() const;

private:
    // State Machine
    enum class State
    {
        Headers,
        Body,
        Complete,
        Failed
    };

    HttpParseResult Fail(HttpParseResult error);

    State state_{State::Headers};
    HttpParseResult result_{HttpParseResult::NeedMore};

    HttpRequest request_;
    size_t contentLength_{0};

};

