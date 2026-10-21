#include "httphandler.h"
#include "formparser.h"
#include "httpparser.h"

#include <cassert>
#include <fstream>
#include <sstream>

std::string BuildResponse(const HttpRequest &request, HttpParseResult parseResult)
{
    assert(parseResult != HttpParseResult::NeedMore);

    // 服务器准备返回的正文
        std::string body; // Illegal number: non-unsigned integers, empty string
                            // numbers outside the integer range.
        std::string status;
        std::string extraHeaders;

        std::string contentType = "text/html; charset=utf-8";

        // GET / HTTP/1.1\r\n
        if (parseResult == HttpParseResult::BadRequest)
        {
            status = "400 Bad Request";
            body = "<h1>400 Bad Request</h1>\n";
        }
        else if (parseResult == HttpParseResult::HeaderTooLarge)
        {
            status = "431 Request Header Fields Too Large";
            body = "<h1>431 Request Header Fields Too Large</h1>\n";
        }
        else if (parseResult == HttpParseResult::BodyTooLarge)
        {
            status = "413 Content Too Large";
            body = "<h1>413 Content Too Large</h1>\n";
        }
        else if (parseResult == HttpParseResult::UnsupportedTransferEncoding)
        {
            status = "501 Not Implemented";
            body = "<h1>Transfer-Encoding is not supported</h1>\n";
        }
        else if (parseResult == HttpParseResult::MethodNotAllowed)
        {
            status = "501 Not Implemented";
            body = "<h1>Request method is not implemented</h1>\n";
        }
        else if (request.Path() == "/echo")
        {
            if (request.Method() != "POST")
            {
                status = "405 Method Not Allowed";
                body = "<h1>405 Method Not Allowed</h1>\n";
                extraHeaders = "Allow: POST\r\n";
            }
            else if (request.Header("content-type") != "text/plain")
            {
                status = "415 Unsupported Media Type";
                body = "<h1>Expected Content-Type: text/plain</h1>\n";
            }
            else
            {
                status = "200 OK";
                contentType = "text/plain; charset=utf-8";

                // 请求正文作为响应正文，复制到响应字符串。
                body = request.Body();

                extraHeaders = "X-Content-Type-Options: nosniff\r\n";
            }
        }
        else if (request.Path() == "/form")
        {
            if (request.Method() != "POST")
            {
                status = "405 Method Not Allowed";
                body = "<h1>405 Method Not Allowed</h1>\n";
                extraHeaders = "Allow: POST\r\n";
            }
            else if (request.Header("content-type") !=
                        "application/x-www-form-urlencoded")
            {
                status = "415 Unsupported Media Type";
                body = "<h1>Expected URL-encoded form</h1>\n";
            }
            else
            {
                FormFields fields;

                if (!ParseUrlEncoded(request.Body(), fields))
                {
                    status = "400 Bad Request";
                    body = "<h1>Invalid form encoding</h1>\n";
                }
                else if (fields.find("name") == fields.end() ||
                            fields.find("message") == fields.end())
                {
                    status = "400 Bad Request";
                    body = "<h1>Missing name or message</h1>\n";
                }
                else
                {
                    status = "200 OK";
                    contentType = "text/plain; charset=utf-8";
                    extraHeaders = "X-Content-Type-Options: nosniff\r\n";

                    body = "name=" + fields.at("name")
                            + "\nmessage=" + fields.at("message")
                            + "\n";
                }
            }
        }
        else if (request.Path() == "/" || request.Path() == "/index.html")
        {
            if (request.Method() != "GET")
            {
                status = "405 Method Not Allowed";
                body = "<h1>405 Method Not Allowed</h1>\n";
                extraHeaders = "Allow: GET\r\n";
            }
            else
            {
                // 按原始字节读取，不进行文本换行转换；HTTP 长度按字节计算
                std::ifstream file("resources/index.html", std::ios::binary);

                if (!file.is_open())
                {
                    status = "500 Internal Server Error";
                    body = "<h1>500 Internal Server Error</h1>\n";
                }
                else
                {
                    std::ostringstream content;
                    content << file.rdbuf();
                    
                    // if existing some r/w error...
                    if (file.bad() || content.bad())
                    {
                        status = "500 Internal Server Error";
                        body = "<h1>500 Internal Server Error</h1>\n";
                    }
                    else
                    {
                        status = "200 OK";
                        body = content.str();
                    }
                }
            }
        }
        else
        {
            status = "404 Not Found";
            body = "<h1>404 Not Found</h1>\n";
        }

        std::string resp= 
            std::string("HTTP/1.1 ") + status + "\r\n"
            "Content-Type: " + contentType + "\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            // 告诉客户端：响应发送完后，服务器将关闭连接。
            "Connection: close\r\n"
            // 空行分隔响应头和正文，后面开始是正文。
            + extraHeaders
            + "\r\n"
            + body;

    return resp;
}