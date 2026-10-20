#include "../code/http/httprequest.h"
#include "../code/buffer/buffer.h"

#include <cassert>
#include <cstdio>

int main()
{
    HttpRequest request;

        assert(request.ParseRequestLine("GET /index.html HTTP/1.1")
            == RequestLineResult::Ok);
    assert(request.Method() == "GET");
    assert(request.Path() == "/index.html");
    assert(request.Version() == "HTTP/1.1");

    assert(request.ParseRequestLine("GET /")
            == RequestLineResult::BadRequest);

    assert(request.ParseRequestLine("GET / HTTP/1.1 extra")
            == RequestLineResult::BadRequest);

    assert(request.ParseRequestLine("GET / WRONG")
            == RequestLineResult::BadRequest);

    assert(request.ParseRequestLine("GET index.html HTTP/1.1")
            == RequestLineResult::BadRequest);

    assert(request.ParseRequestLine("DELETE / HTTP/1.1")
            == RequestLineResult::MethodNotAllowed);

    // 解析失败不应该覆盖之前成功保存的结果。
    assert(request.Method() == "GET");
    assert(request.Path() == "/index.html");
    assert(request.Version() == "HTTP/1.1");

    HttpRequest headers;
    assert(headers.ParseHeaderLine("Host: localhost:8080"));
    assert(headers.Header("host") == "localhost:8080");

    assert(headers.ParseHeaderLine("Content-Type: \ttext/plain \t"));
    assert(headers.Header("content-type") == "text/plain");

    assert(!headers.ParseHeaderLine("没有冒号"));
    assert(!headers.ParseHeaderLine(": value"));
    assert(!headers.ParseHeaderLine("Bad Name: value"));

    // 同名字段即使大小写不同，也属于重复。
    assert(!headers.ParseHeaderLine("HOST: other"));
    assert(headers.Header("host") == "localhost:8080");

    HttpRequest wholeHeaders;

    assert(wholeHeaders.ParseHeaders(
        "Host: localhost:8080\r\n"
        "User-Agent: learning-client\r\n"
        "\r\n"));

    assert(wholeHeaders.Header("host") == "localhost:8080");
    assert(wholeHeaders.Header("user-agent") == "learning-client");

    // 缺少 Host。
    assert(!wholeHeaders.ParseHeaders(
        "User-Agent: learning-client\r\n"
        "\r\n"));

    // Host 为空。
    assert(!wholeHeaders.ParseHeaders(
        "Host: \r\n"
        "\r\n"));

    // 缺少最终空行。
    assert(!wholeHeaders.ParseHeaders(
        "Host: localhost\r\n"));

    // 重复 Host。
    assert(!wholeHeaders.ParseHeaders(
        "Host: localhost\r\n"
        "HOST: other\r\n"
        "\r\n"));

    HttpRequest lengthRequest;
    size_t length = 999;

    // 合法长度。
    assert(lengthRequest.ParseHeaders(
        "Host: localhost\r\nContent-Length: 5\r\n\r\n"));
    assert(lengthRequest.TryGetContentLength(length));
    assert(length == 5);

    // 没有长度字段，当前约定为 0。
    assert(lengthRequest.ParseHeaders(
        "Host: localhost\r\n\r\n"));
    assert(lengthRequest.TryGetContentLength(length));
    assert(length == 0);

    // 字段本身可以拆分，但长度值不合法。
    assert(lengthRequest.ParseHeaders(
        "Host: localhost\r\nContent-Length: -1\r\n\r\n"));

    length = 999;
    assert(!lengthRequest.TryGetContentLength(length));
    assert(length == 999);

    // 空值也不合法。
    assert(lengthRequest.ParseHeaders(
        "Host: localhost\r\nContent-Length: \r\n\r\n"));
    assert(!lengthRequest.TryGetContentLength(length));

    // 超出整数范围。
    assert(lengthRequest.ParseHeaders(
        "Host: localhost\r\n"
        "Content-Length: 9999999999999999999999999999999999999999\r\n"
        "\r\n"));
    assert(!lengthRequest.TryGetContentLength(length));

    HttpRequest encodingRequest;
    // 字段不存在。
    assert(encodingRequest.ParseHeaders(
        "Host: localhost\r\n\r\n"));
    assert(!encodingRequest.HasHeader("transfer-encoding"));

    // 字段存在，且名称大小写混合。
    assert(encodingRequest.ParseHeaders(
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"));
    assert(encodingRequest.HasHeader("transfer-encoding"));
    assert(encodingRequest.Header("transfer-encoding") == "chunked");

    // 字段存在但为空，仍然能够识别其存在。
    assert(encodingRequest.ParseHeaders(
        "Host: localhost\r\n"
        "Transfer-Encoding: \r\n"
        "\r\n"));
    assert(encodingRequest.HasHeader("transfer-encoding"));
    assert(encodingRequest.Header("transfer-encoding").empty());

    HttpRequest framing;
    size_t bodyLength = 999;

    // 正常长度。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nContent-Length: 5\r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::Ok);
    assert(bodyLength == 5);

    // 非法数字。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nContent-Length: abc\r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::BadRequest);
    assert(bodyLength == 5); // 失败不修改输出参数。

    // 超过正文上限。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nContent-Length: 65537\r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::TooLarge);

    // 不支持 chunked。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::UnsupportedTransferEncoding);

    // 两种机制同时存在，优先判为请求错误。
    assert(framing.ParseHeaders(
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Content-Length: 5\r\n"
        "\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::BadRequest);
    // 显式零长度合法。
   assert(framing.ParseHeaders(
       "Host: localhost\r\nContent-Length: 0\r\n\r\n"));
   assert(framing.CheckBodyFraming(bodyLength)
          == BodyFramingResult::Ok);
   assert(bodyLength == 0);

    // 恰好达到上限，仍然合法。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nContent-Length: 65536\r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::Ok);
    assert(bodyLength == 65536);

    // 空传输编码是错误，且不能修改输出参数。
    assert(framing.ParseHeaders(
        "Host: localhost\r\nTransfer-Encoding: \r\n\r\n"));
    assert(framing.CheckBodyFraming(bodyLength)
            == BodyFramingResult::BadRequest);
    assert(bodyLength == 65536);


    HttpRequest bodyRequest;
    Buffer bodyBuffer;

    // 只收到 2 字节，还需要数据。
    bodyBuffer.Append("he", 2);

    assert(bodyRequest.ParseBody(bodyBuffer, 5)
            == BodyParseResult::NeedMore);

    // 未完成时，不能把已有的 he 消费掉。
    assert(bodyBuffer.ReadableBytes() == 2);
    assert(bodyRequest.Body().empty());

    // 补齐剩余数据，并故意多放一个字节。
    bodyBuffer.Append("llo!", 4);

    assert(bodyRequest.ParseBody(bodyBuffer, 5)
            == BodyParseResult::Complete);

    assert(bodyRequest.Body() == "hello");

    // 只消费指定的 5 字节，不能吞掉额外的 !。
    assert(bodyBuffer.ReadableBytes() == 1);
    assert(*bodyBuffer.Peek() == '!');

    HttpRequest postRequest;

    assert(postRequest.ParseRequestLine("POST /echo HTTP/1.1")
            == RequestLineResult::Ok);

    assert(postRequest.Method() == "POST");
    assert(postRequest.Path() == "/echo");

    // 严格请求行规则：拒绝控制字符、多余空格、非法方法字符和片段。
    HttpRequest strictRequest;
    const char* invalidLines[] = {
        " GET / HTTP/1.1",
        "GET  / HTTP/1.1",
        "GET / HTTP/1.1 ",
        "GET\t/ HTTP/1.1",
        "GET / HTTP/1.1\r",
        "G@T / HTTP/1.1",
        "GET /#fragment HTTP/1.1"
    };
    for (const char* line : invalidLines)
    {
        assert(strictRequest.ParseRequestLine(line)
               == RequestLineResult::BadRequest);
    }
    assert(strictRequest.ParseRequestLine("GET /?q=test HTTP/1.1")
           == RequestLineResult::Ok);
    assert(strictRequest.ParseRequestLine("DELETE / HTTP/1.1")
           == RequestLineResult::MethodNotAllowed);

    HttpRequest controlRequest;
    assert(!controlRequest.ParseHeaderLine("X-Test: a\nb"));
    assert(!controlRequest.ParseHeaderLine("X-Test: a\rb"));
    std::string nulValue = "X-Test: a";
    nulValue.push_back('\0');
    nulValue += 'b';
    assert(!controlRequest.ParseHeaderLine(nulValue));
    std::string delValue = "X-Test: a";
    delValue.push_back(static_cast<char>(0x7f));
    assert(!controlRequest.ParseHeaderLine(delValue));
    assert(controlRequest.ParseHeaderLine("X-Test: \tok\t"));
    assert(controlRequest.Header("x-test") == "ok");

    std::puts("HTTP 请求解析测试通过");
}