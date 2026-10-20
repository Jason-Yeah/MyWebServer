#pragma once

#include <string> 
#include <unordered_map>
#include <cstddef>

enum class RequestLineResult
{
    Ok,
    BadRequest,
    MethodNotAllowed
};

enum class BodyFramingResult
{
    Ok,
    BadRequest,
    UnsupportedTransferEncoding,
    TooLarge
};

class Buffer;

enum class BodyParseResult
{
    NeedMore,
    Complete
};

class HttpRequest
{
public:
    BodyParseResult ParseBody(Buffer& buffer, size_t length);

    const std::string& Body() const;

    BodyFramingResult CheckBodyFraming(size_t& length) const;

    bool ParseHeaderLine(const std::string& line);

    // lowercase letters
    std::string Header(const std::string& name) const;

    bool HasHeader(const std::string& name) const;

    bool ParseHeaders(const std::string& block);

    bool TryGetContentLength(size_t& length) const;

    RequestLineResult ParseRequestLine(const std::string& line);

    const std::string& Method() const;

    const std::string& Path() const;

    const std::string& Version() const;

private:
    std::unordered_map<std::string, std::string> headers_;

    std::string method_;
    std::string path_;
    std::string version_;

    std::string body_;
};

constexpr static size_t DELIMITERSIZE = 2;
