#include "httpparser.h"
#include "../buffer/buffer.h"
#include "httprequest.h"

#include <algorithm>
#include <cstddef>
#include <string>

HttpParseResult HttpParser::Fail(HttpParseResult error)
{
    state_ = State::Failed;
    result_ = error;
    return result_;
}

const HttpRequest& HttpParser::Request() const
{
    return request_;
}

HttpParseResult HttpParser::Parse(Buffer& buf)
{
    if (state_ == State::Complete || state_ == State::Failed) return result_;

    if (state_ == State::Headers)
    {
        constexpr size_t MAXHEADERSIZE = 1024 * 8;
        const char delimiter[] = "\r\n\r\n";

        const char* st = buf.Peek();
        const char* ed = st + buf.ReadableBytes();

        const char* found = std::search(
            st, ed, delimiter, delimiter + DELIMITERSIZE + DELIMITERSIZE
        );
        // 没找到结束标记
        if (found == ed)
        {
            // 已经收到的达到了上限但还没找到结束标记，如果再去找必定超过上限
            if (buf.ReadableBytes() >= MAXHEADERSIZE)
                return Fail(HttpParseResult::HeaderTooLarge);
            // 没到上限，等更多数据
            return HttpParseResult::NeedMore;
        }

        const size_t headerBytes = static_cast<size_t>(found - st) + 4;

        if (headerBytes > MAXHEADERSIZE)
        {
            return Fail(HttpParseResult::HeaderTooLarge);
        }

        // Parse Headers Begin..........
        std::string headerText(st, headerBytes);
        // HTTP/1.1 /xxx GET
        const auto lindEnd = headerText.find("\r\n");

        const auto lineResult = request_.ParseRequestLine(
            headerText.substr(0, lindEnd));
        
        if (lineResult == RequestLineResult::BadRequest)
            return Fail(HttpParseResult::BadRequest);
        
        if (!request_.ParseHeaders(headerText.substr(lindEnd + DELIMITERSIZE)))
            return Fail(HttpParseResult::BadRequest);
        
        const auto framing = request_.CheckBodyFraming(contentLength_);

        if (framing == BodyFramingResult::BadRequest)
            return Fail(HttpParseResult::BadRequest);
        if (framing == BodyFramingResult::TooLarge)
            return Fail(HttpParseResult::BodyTooLarge);
        if (framing == BodyFramingResult::UnsupportedTransferEncoding)
            return Fail(HttpParseResult::UnsupportedTransferEncoding);
        
        if (lineResult == RequestLineResult::MethodNotAllowed)
            return Fail(HttpParseResult::MethodNotAllowed);

        buf.Retrieve(headerBytes);
        // Parse Headers End...........

        // Parse Body Begin............
        state_ = State::Body;
    }

    if (state_ == State::Body)
    {
        if (request_.ParseBody(buf, contentLength_) == BodyParseResult::NeedMore)
            return HttpParseResult::NeedMore;
        
        state_ = State::Complete;
        result_ = HttpParseResult::Complete;
        return result_;
    }
    
    // 仅仅兜底，有个返回值
    return result_;
}