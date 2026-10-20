#include "../code/http/httpparser.h"
#include "../code/buffer/buffer.h"

#include <cassert>
#include <cstdio>
#include <string>

int main()
{
    // 1. 空数据、分段请求头、分段正文。
    HttpParser parser;
    Buffer buffer;

    assert(parser.Parse(buffer) == HttpParseResult::NeedMore);

    buffer.Append("GET /ind", 8);
    assert(parser.Parse(buffer) == HttpParseResult::NeedMore);
    assert(buffer.ReadableBytes() == 8); // 未完成时不消费。

    const std::string rest =
        "ex.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "he";

    buffer.Append(rest.data(), rest.size());

    assert(parser.Parse(buffer) == HttpParseResult::NeedMore);
    assert(buffer.ReadableBytes() == 2); // 头部已消费，留下 he。

    buffer.Append("llo!", 4);

    assert(parser.Parse(buffer) == HttpParseResult::Complete);
    assert(parser.Request().Path() == "/index.html");
    assert(parser.Request().Body() == "hello");

    assert(buffer.ReadableBytes() == 1);
    assert(*buffer.Peek() == '!');

    // 完成后重复调用，不能消费额外字节。
    assert(parser.Parse(buffer) == HttpParseResult::Complete);
    assert(buffer.ReadableBytes() == 1);

    // 2. 没有正文的完整请求，一次调用即可完成。
    HttpParser emptyBodyParser;
    Buffer emptyBodyBuffer;

    const std::string get =
        "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

    emptyBodyBuffer.Append(get.data(), get.size());

    assert(emptyBodyParser.Parse(emptyBodyBuffer)
            == HttpParseResult::Complete);
    assert(emptyBodyParser.Request().Body().empty());

    // 3. 缺少 Host，进入失败状态。
    HttpParser badParser;
    Buffer badBuffer;

    const std::string bad = "GET / HTTP/1.1\r\n\r\n";
    badBuffer.Append(bad.data(), bad.size());

    assert(badParser.Parse(badBuffer)
            == HttpParseResult::BadRequest);

    // 失败后重复调用，结果保持不变。
    assert(badParser.Parse(badBuffer)
            == HttpParseResult::BadRequest);

    // 4. 请求头达到上限但没有结束标记。
    HttpParser largeParser;
    Buffer largeBuffer;

    const std::string large(8192, 'x');
    largeBuffer.Append(large.data(), large.size());

    assert(largeParser.Parse(largeBuffer)
            == HttpParseResult::HeaderTooLarge);

    std::puts("HTTP 增量解析器测试通过");
}