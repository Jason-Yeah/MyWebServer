#include "buffer/buffer.h"
#include "http/httprequest.h"
#include "http/httpparser.h"
#include "http/formparser.h"

#include <arpa/inet.h>
#include <cstddef>
#include <ios>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstdio>

constexpr size_t BUFFER_SIZE = 1024 * 4;

int main()
{
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        // perror 根据失败时的 errno 输出具体原因到标准错误，例如 "socket: Too many open files"。
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    // INADDR_LOOPBACK 表示本机回环地址 127.0.0.1，仅允许本机访问；htonl 转为网络字节序。
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);



    // Address Multi_Use
    int reuse = 1; // 1: open
    if (setsockopt(listenfd, 
        SOL_SOCKET, SO_REUSEADDR,  // SO_REUSEADDR 地址复用允许
        &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt SO_REUSEADDR");
        close(listenfd);
        return 1;
    }
    // When server is quitted, TCP_CONNECTION may still in `TIME_WAIT`
    // 此时重启可能报错：Address already in use
    // SO_REUSEADDR 可以复用


    
    // bind 接收通用地址指针 sockaddr*；这里将 IPv4 地址 sockaddr_in 的指针转换为该类型。
    // reinterpret_cast 只改变指针的类型，不改变 addr 的内容；系统根据 sin_family 识别 IPv4。
    if (bind(listenfd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        perror("bind");
        close(listenfd);
        return 1;
    }

    if (listen(listenfd, 8) < 0)
    {
        perror("listen");
        close(listenfd);
        return 1;
    }

    std::puts("Waiting for client connecting... 127.0.0.1:8080");

    while (true)
    {
        // 后两个参数用于输出客户端地址及地址长度；这里不需要这些信息，因此都传 nullptr。
        // 仍正常接受连接，成功后返回用于与客户端通信的新文件描述符。
        int clientfd = accept(listenfd, nullptr, nullptr);
        if (clientfd < 0)
        {
            if (errno == EINTR) continue;

            perror("accept");
            close(listenfd);
            return 1;
        }

        std::puts("Client connecting successful.");


        // std::string req;
        
        char buffer[BUFFER_SIZE]; // 本次接收的临时空间
        Buffer readBuffer; // 保存累计收到的数据
        HttpParser parser;

        HttpParseResult parseResult = HttpParseResult::NeedMore;
        bool connectionFailed = false;

        while (1)
        {
            parseResult = parser.Parse(readBuffer);
            if (parseResult != HttpParseResult::NeedMore) break;

            ssize_t recvd = recv(clientfd, buffer, BUFFER_SIZE, 0);

            if (recvd < 0)
            {
                if (errno == EINTR) continue;
                perror("recv");
                connectionFailed = true;
                break;
            }
            if (recvd == 0)
            {
                // 此处解析器仍返回 NeedMore：头部或正文尚未收齐。
                std::fputs("The client has finished sending, but the request header is not yet complete\n", stderr);
                connectionFailed = true;
                break;
            }

            readBuffer.Append(buffer, static_cast<size_t>(recvd));
        }

        if (connectionFailed)
        {
            close(clientfd);
            continue;
        }

        const HttpRequest& request = parser.Request();

        if (parseResult == HttpParseResult::Complete)
        {
            std::printf(
                "[REQUEST] method=%s path=%s body=%zu bytes\n",
                request.Method().c_str(),
                request.Path().c_str(),
                request.Body().size());
        }

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

        const auto total = resp.size();
        // 累计成功交给本机 socket 发送缓冲区的字节数，不代表对方已收到。
        size_t sent = 0;

        // 阻塞 socket 的一次 send 也可能只发送部分数据，需要循环发送剩余部分。
        while (sent < total)
        {
            // resp + sent 跳过已发送的字节；total - sent 是剩余字节数。
            // MSG_NOSIGNAL 避免 SIGPIPE 信号终止进程，发送错误仍通过返回值报告。
            ssize_t n = send(clientfd, 
                // string.data() return the original pointer....
                resp.data() + sent, total - sent,
                MSG_NOSIGNAL);
            
            if (n < 0)
            {
                // EINTR：被信号中断且本次未发送数据，保持 sent 不变并重试。
                if (errno == EINTR) 
                {   
                    continue;
                }
                // 其他错误：放弃当前响应，随后关闭当前连接。
                perror("send");  
                break; // 放弃当前响应 跳出发送循环              
                // close(clientfd);
                // close(listenfd);
                // return 1;
            }

            if (n == 0)
            {
                // 还有数据却无发送进展，退出避免死循环；stderr 是标准错误输出流。
                std::fputs("No progres made on SEND\n", stderr);
                break;
                // close(clientfd);
                // close(listenfd);
                // return 1;
            }

            // 此处 n > 0，可安全转为 size_t，累加本次发送量。
            // 例如共 100 字节，本次发送 40，下次从 resp+40 开始发送剩余 60 字节。
            sent += static_cast<size_t>(n);
        }
    
        close(clientfd);
    }
    
    close(listenfd);
    return 0;
}
