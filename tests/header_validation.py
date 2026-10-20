import socket

cases = [
    ("正常 Host",
    b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
    b"HTTP/1.1 200 OK"),

    ("小写 host",
    b"GET / HTTP/1.1\r\nhost: localhost\r\n\r\n",
    b"HTTP/1.1 200 OK"),

    ("缺少 Host",
    b"GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n",
    b"HTTP/1.1 400 Bad Request"),

    ("重复 Host",
    b"GET / HTTP/1.1\r\nHost: localhost\r\nHOST: other\r\n\r\n",
    b"HTTP/1.1 400 Bad Request"),
]

for name, request, expected in cases:
    with socket.create_connection(("127.0.0.1", 8080), timeout=3) as s:
        s.sendall(request)
        with s.makefile("rb") as stream:
            response = stream.read()

    status = response.split(b"\r\n", 1)[0]
    assert status == expected, (name, status)
    print("通过：" + name)