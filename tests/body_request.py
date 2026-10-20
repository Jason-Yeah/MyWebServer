import socket

with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
    # 先发送请求头和部分正文。
    s.sendall(
        b"GET / HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Content-Length: 5\r\n"
        b"\r\n"
        b"he"
    )

    # 正文只有 2 字节，服务器应该继续等待。
    s.settimeout(0.3)
    try:
        data = s.recv(4096)
    except socket.timeout:
        print("通过：正文未收齐，服务器继续等待")
    else:
        raise AssertionError(f"过早响应或关闭：{data!r}")

    # 补齐剩余 3 字节。
    s.sendall(b"llo")
    s.settimeout(3)

    with s.makefile("rb") as stream:
        response = stream.read()

    status = response.split(b"\r\n", 1)[0]
    assert status == b"HTTP/1.1 200 OK", f"实际响应状态：{status!r}"

    with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
       s.sendall(
           b"GET / HTTP/1.1\r\n"
           b"Host: localhost\r\n"
           b"Content-Length: 5\r\n"
           b"\r\n"
           b"hello"
       )

       with s.makefile("rb") as stream:
           response = stream.read()

       assert response.startswith(b"HTTP/1.1 200 OK\r\n")
       print("通过：正文随请求头一起发送，可以正常处理")

    with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
       s.sendall(
           b"GET / HTTP/1.1\r\n"
           b"Host: localhost\r\n"
           b"Content-Length: 5\r\n"
           b"\r\n"
           b"he"
       )

       # 声明：我不会再发送数据，但仍然可以接收数据。
       s.shutdown(socket.SHUT_WR)

       try:
           response = s.recv(4096)
       except ConnectionResetError:
           response = b""

       assert response == b"", f"不应返回成功响应：{response!r}"
       print("通过：正文不足时结束发送，服务器关闭连接")

    print("通过：正文收齐后返回 200")
