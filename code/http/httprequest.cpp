#include "httprequest.h"
#include "../buffer/buffer.h"

#include <cstddef>
#include <sstream>

BodyParseResult HttpRequest::ParseBody(Buffer& buffer, size_t length)
{
    if (buffer.ReadableBytes() < length) return BodyParseResult::NeedMore;
    
    body_.assign(buffer.Peek(), length);
    buffer.Retrieve(length);

    return BodyParseResult::Complete;
}

const std::string& HttpRequest::Body() const
{
    return body_;
}

BodyFramingResult HttpRequest::CheckBodyFraming(size_t& length) const
{
    // Only support "Content-Length"
    if (HasHeader("transfer-encoding"))
    {
        // 两种方式都出现都拒绝
        if (HasHeader("content-length")) return BodyFramingResult::BadRequest;

        if (Header("transfer-encoding").empty()) return BodyFramingResult::BadRequest;
        
        return BodyFramingResult::UnsupportedTransferEncoding;
    }

    size_t parsedLength = 0;

    if (!TryGetContentLength(parsedLength)) return BodyFramingResult::BadRequest;

    constexpr size_t maxBodySize = 64 * 1024; // 64KB

    if (parsedLength > maxBodySize) return BodyFramingResult::TooLarge;

    length = parsedLength;
    return BodyFramingResult::Ok;
}

// Parse the header fields
// Host: localhost:8080
bool HttpRequest::ParseHeaderLine(const std::string& line)
{
    // colon 冒号
    const auto colon = line.find(':');

    // 找不到或没字段名都不对
    if (colon == std::string::npos || colon == 0) return false;

    /*
        0x00 ~ 0x1f ASCII 控制字符
        0x09 制表符 \t 允许
        0x7f DEL 拒绝
    */
    auto len = line.size();
    for (size_t i = colon + 1; i < len; ++ i)
    {
        const auto ch = static_cast<unsigned char>(line[i]);
        if ((ch < 0x20 && ch != '\t') || ch == 0x7f) return false;
    }

    std::string key = line.substr(0, colon);

    // no SPACE character
    const std::string allowed =  
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789!#$%&'*+-.^_`|~";
    
    for (char& c: key)
    {
        if (allowed.find(c) == std::string::npos) return false;

        // To LOWERCASE
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    // colon后开始，find第一个不是空格+制表符的位置
    const auto first = line.find_first_not_of(" \t", colon + 1);
    std::string value;

    if (first != std::string::npos)
    {
        const auto last = line.find_last_not_of(" \t");
        value = line.substr(first, last - first + 1);
    }

    // 暂不支持重复字段
    if (headers_.find(key) != headers_.end())  return false;

    headers_.emplace(key, value);
    return true;
}

std::string HttpRequest::Header(const std::string& name) const
{
    const auto it = headers_.find(name);
    return (it == headers_.end()) ? "" : it->second;
}

/*
    Host: localhost
    
    Host: localhost
    Transfer-Encoding:

    调用 Header("transfer-encoding") 效果一致都是 return "";

    HasHeader() 还可以表示如果有字段但 value=="" 也为 true 而不是都是 empty string
*/
bool HttpRequest::HasHeader(const std::string& name) const
{
    return headers_.find(name) != headers_.end();
}

/*
    Host: localhost\r\n
    User-Agent: curl\r\n
    \r\n

    Not contain the HTTP Request Line.
*/
bool HttpRequest::ParseHeaders(const std::string& block)
{
    headers_.clear();

    size_t st = 0;
    size_t n = block.size();
    while (st < n)
    {
        const auto ed = block.find("\r\n", st);
        if (ed == std::string::npos) return false;

        if (ed == st) // \r\n Parse the Last line
        {
            // 当前要求：空行后不能再有数据，Host 必须非空
            return ed + DELIMITERSIZE  == block.size() && !Header("host").empty();
        }

        std::string line = block.substr(st, ed - st);
        if (!ParseHeaderLine(line)) return false;

        st = ed + DELIMITERSIZE;
    }
    return false;
}

/**
 * Content-Length: 5
 *
 * No `Content-Lenght` header: true, length is 0; 
 * Exist but empty: false;
 * Output parameters remain unmodified upon failure.
 */
bool HttpRequest::TryGetContentLength(size_t& length) const
{
    // Every characters in `header_` is lowercase.
    const auto it = headers_.find("content-length");

    // 没有 Content-Length 时，按当前约定成功返回长度 0。
    if (it == headers_.end())
    {
        length = 0;
        return true;
    }

    const std::string& value = it->second;

    if (value.empty()) return false;

    for (char c: value)
        if (c < '0' || c > '9') return false;

    size_t parsed = 0;
    std::istringstream input(value);

    if (!(input >> parsed)) return false;

    length = parsed;
    return true;
}


RequestLineResult HttpRequest::ParseRequestLine(const std::string& line)
{
    for (unsigned char ch: line)
        if (ch < 0x20 || ch == 0x7f) return RequestLineResult::BadRequest;
    
    const auto firstSpace = line.find(' ');

    // 没找到或开头
    if (firstSpace == std::string::npos || firstSpace == 0)
        return RequestLineResult::BadRequest;
    // method 1 path 2 version
    const auto secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos || secondSpace == firstSpace + 1 ||
        secondSpace + 1 == line.size())
        return RequestLineResult::BadRequest;
    
    if (line.find(' ', secondSpace + 1) != std::string::npos) 
        return RequestLineResult::BadRequest;

    std::string mtd = line.substr(0, firstSpace);
    std::string pth = line.substr(
        firstSpace + 1, secondSpace - firstSpace - 1
    );
    std::string version = line.substr(secondSpace + 1);

    const std::string allowed =
           "abcdefghijklmnopqrstuvwxyz"
           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
           "0123456789!#$%&'*+-.^_`|~";

    for (auto ch: mtd)
        if (allowed.find(ch) == std::string::npos)
            return RequestLineResult::BadRequest;

    if (version != "HTTP/1.1") return RequestLineResult::BadRequest;

    // # 表示片段标识符（fragment）的开始，常用于跳到页面中的某个位置
    // But 这部分由浏览器处理，不会放进 HTTP 请求目标中发送给服务器
    if (pth.empty() || pth[0] != '/' || pth.find('#') != std::string::npos)
        return RequestLineResult::BadRequest;

    if (mtd != "GET" && mtd != "POST")
    {
        return RequestLineResult::MethodNotAllowed;
    }

    method_ = mtd;
    path_ = pth;
    version_ = version;

    return RequestLineResult::Ok;
}

const std::string& HttpRequest::Method() const
{
    return method_;
}

const std::string& HttpRequest::Path() const
{
    return path_;
}

const std::string& HttpRequest::Version() const
{
    return version_;
}
