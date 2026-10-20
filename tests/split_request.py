import socket

with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
    parts = [
        b"GET /ind",
        b"ex.html HTTP/1.1\r\nHost: localhost\r\n\r",
    ]

    for part in parts:
        s.sendall(part)

        # 请求头还不完整，服务器应该等待，而不是回复或断开。
        s.settimeout(0.3)
        try:
            data = s.recv(4096)
        except socket.timeout:
            print("通过：请求头未完整，服务器继续等待")
        else:
            raise AssertionError(
                f"过早响应或关闭连接：{data!r}"
            )

    # 补上最后一个换行，请求头才完整。
    s.sendall(b"\n")
    s.settimeout(3)

    response = bytearray()
    while True:
        data = s.recv(4096)
        if not data:
            break
        response.extend(data)

    assert response.startswith(b"HTTP/1.1 200 OK\r\n")
    print("通过：请求头完整后返回 200")
