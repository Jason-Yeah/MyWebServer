import socket

# 只发送请求头，验证服务器无需等待正文就能拒绝错误规则。
cases = [
    ("非法长度", b"Content-Length: abc\r\n", b"HTTP/1.1 400 Bad Request"),
    ("正文超限", b"Content-Length: 65537\r\n", b"HTTP/1.1 413 Content Too Large"),
    ("不支持的编码", b"Transfer-Encoding: chunked\r\n", b"HTTP/1.1 501 Not Implemented"),
    ("空传输编码", b"Transfer-Encoding: \r\n", b"HTTP/1.1 400 Bad Request"),
    ("长度机制冲突", b"Transfer-Encoding: chunked\r\nContent-Length: 5\r\n", b"HTTP/1.1 400 Bad Request"),
]

for name, fields, expected in cases:
    request = b"GET / HTTP/1.1\r\nHost: localhost\r\n" + fields + b"\r\n"

    with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
        s.sendall(request)
        with s.makefile("rb") as stream:
            response = stream.read()

    status = response.split(b"\r\n", 1)[0]
    assert status == expected, (name, status)
    print("通过：" + name)
