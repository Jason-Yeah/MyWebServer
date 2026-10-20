import socket

# 请求行故意缺少 HTTP 版本，但请求头结束标记完整。
request = b"GET /\r\nHost: localhost\r\n\r\n"

with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
    s.sendall(request)
    with s.makefile("rb") as stream:
        response = stream.read()

print(response.decode("utf-8", errors="replace"))
assert response.startswith(b"HTTP/1.1 400 Bad Request\r\n")
print("通过：缺少协议版本的请求返回 400")
