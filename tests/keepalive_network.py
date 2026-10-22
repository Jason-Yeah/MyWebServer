"""最小 Keep-Alive 验收，复用 lt_network 的进程管理，不修改用户资源。
python3 tests/keepalive_network.py --mode ET --send-probe build/send_probe.so
LT 对照另加 --server build/server_lt_test --mode LT。
"""
import argparse
from pathlib import Path
import re
import socket
import struct
import tempfile
import time

import lt_network as net


class Responses:
    """按 Content-Length 切分响应，保留一次 recv 中的后续响应字节。"""
    def __init__(self, s):
        self.s = s
        self.buffer = bytearray()

    def more(self):
        data = self.s.recv(65536)
        assert data, "响应未收完整就断开"
        self.buffer.extend(data)

    def read(self):
        while b"\r\n\r\n" not in self.buffer:
            self.more()
        raw, rest = bytes(self.buffer).split(b"\r\n\r\n", 1)
        self.buffer = bytearray(rest)
        lines = raw.split(b"\r\n")
        fields = dict((k.lower(), v.strip()) for k, v in
                      (line.split(b":", 1) for line in lines[1:]))
        length = int(fields[b"content-length"])
        while len(self.buffer) < length:
            self.more()
        body = bytes(self.buffer[:length])
        del self.buffer[:length]
        return int(lines[0].split()[1]), fields, body

    def eof(self):
        assert not self.buffer, "关闭前出现未预期响应字节"
        assert self.s.recv(1) == b"", "未按约定关闭连接"


def req(path="/", body=None, connection="keep-alive", content_type="text/plain"):
    method = "GET" if body is None else "POST"
    data = f"{method} {path} HTTP/1.1\r\nHost: localhost\r\n".encode()
    if connection is not None:
        data += f"Connection: {connection}\r\n".encode()
    if body is not None:
        data += f"Content-Length: {len(body)}\r\nContent-Type: {content_type}\r\n".encode()
    return data + b"\r\n" + (body if body is not None else b"")


def expect(reader, status, connection, body=None):
    actual, headers, data = reader.read()
    assert actual == status, (actual, status, data[:100])
    if connection is not None:
        assert headers[b"connection"] == connection.encode(), headers
    if body is not None:
        assert data == body, (len(data), len(body))
    return data


def socket_inodes(p):
    result = set()
    for fd in Path(f"/proc/{p.pid}/fd").iterdir():
        try:
            target = fd.readlink().as_posix()
            if target.startswith("socket:"):
                result.add(target)
        except FileNotFoundError:
            pass
    return result


def ordinary():
    with net.server(net.ROOT) as p:
        baseline = net.SERVER_IDLE_FDS
        net.check_event_mode(p)
        with net.connect() as s:
            reader = Responses(s)
            body = bytes(range(256)) * 256
            s.sendall(req("/echo", body))
            expect(reader, 200, "keep-alive", body)
            net.wait_for(lambda: net.fd_count(p) == baseline + 1, "keep-alive fd 不在表中")
            inodes = socket_inodes(p)
            s.sendall(req("/form", b"name=jason&message=C%2B%2B+server",
                          content_type="application/x-www-form-urlencoded"))
            expect(reader, 200, "keep-alive", b"name=jason\nmessage=C++ server\n")
            assert socket_inodes(p) == inodes, "连接复用时 socket inode 发生变化"
            net.check_event_mode(p)
            # 没有旧正文、头字段泄漏；最终请求没有 Content-Length。
            s.sendall(req("/missing", connection="close"))
            expect(reader, 404, "close")
            reader.eof()
        print("PASS 同一 socket inode 连续 echo/form/404，二进制正文及请求状态重置")

        for value, keep in ((None, False), ("Keep-Alive", True),
                            ("Upgrade, KEEP-ALIVE", True),
                            ("keep-alive, close", False),
                            ("CLOSE, Keep-Alive", False),
                            ("upgrade", False), ("", False)):
            with net.connect() as s:
                reader = Responses(s)
                s.sendall(req("/missing", connection=value))
                expect(reader, 404, "keep-alive" if keep else "close")
                if keep:
                    s.sendall(req("/missing", connection="close"))
                    expect(reader, 404, "close")
                reader.eof()
        print("PASS 默认关闭、token 大小写/列表、close 优先")

        with net.connect() as s:
            reader = Responses(s)
            for i in range(1, 17):
                s.sendall(req("/echo", str(i).encode()))
                expect(reader, 200, "close" if i == 16 else "keep-alive", str(i).encode())
            reader.eof()
        print("PASS 第 16 次请求响应后关闭")

        with net.connect() as s:
            reader = Responses(s)
            third = req("/echo", b"third", connection="close")
            # 一次提交多个请求/部分下一请求，检查 input 不能被清空。
            s.sendall(req("/echo", b"first") + req("/echo", b"second") + third[:-2])
            expect(reader, 200, "keep-alive", b"first")
            expect(reader, 200, "keep-alive", b"second")
            s.settimeout(0.15)
            try:
                data = s.recv(1)
            except socket.timeout:
                pass
            else:
                raise AssertionError(("第三个请求尚未完整", data))
            assert net.request()[0] == b"HTTP/1.1 200 OK"
            s.settimeout(3)
            s.sendall(third[-2:])
            expect(reader, 200, "close", b"third")
            reader.eof()
        print("PASS 预收后续请求、部分正文跨响应保留、EAGAIN 后补齐及连接隔离")

        for bad, status in ((b"GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n", 400),
                            (b"POST /echo HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\nContent-Length: 65537\r\n\r\n", 413)):
            with net.connect() as s:
                reader = Responses(s)
                s.sendall(req("/echo", b"ok"))
                expect(reader, 200, "keep-alive", b"ok")
                s.sendall(bad)
                expect(reader, status, "close")
                reader.eof()
        print("PASS 后续请求解析错误/超限，错误响应后关闭")

        for body in (b"hello", bytes(range(256)) * 256):
            with net.connect() as s:
                reader = Responses(s)
                s.sendall(req("/echo", body))
                s.shutdown(socket.SHUT_WR)
                # FIN 可在构造响应后才被观察到，不强求头字段反映未来事件。
                expect(reader, 200, None, body)
                reader.eof()
        for data in (req("/echo", b"hello")[:-1], b"GET / HTTP/1.1\r\nHost:"):
            with net.connect() as s:
                s.sendall(data)
                s.shutdown(socket.SHUT_WR)
                assert s.recv(1) == b""
        print("PASS keep-alive 请求半关闭仍回复；截断 EOF 不成功")

        for reset in (False, True):
            with net.connect() as s:
                reader = Responses(s)
                s.sendall(req("/echo", b"ok"))
                expect(reader, 200, "keep-alive", b"ok")
                if reset:
                    s.sendall(b"GET /")
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        net.wait_for(lambda: net.fd_count(p) == baseline, "复用后连接资源未回收")
        assert net.request()[0] == b"HTTP/1.1 200 OK"
        print("PASS 复用后空闲关闭/RST 回收，服务仍可响应")


def large(probe):
    with tempfile.TemporaryDirectory(prefix="myweb-keepalive-") as directory:
        root = Path(directory)
        (root / "resources").mkdir()
        body = bytes(range(256)) * (8 * 1024 * 1024 // 256)
        (root / "resources/index.html").write_bytes(body)
        log = root / "send.log"
        with net.server(root, net.probe_env(probe, log, observe=True)) as p:
            with socket.socket() as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
                s.settimeout(5)
                s.connect(net.ADDRESS)
                reader = Responses(s)
                s.sendall(req())
                time.sleep(0.2)
                assert net.fd_count(p) == net.SERVER_IDLE_FDS + 1
                net.check_event_mode(p)
                assert net.request(b"GET /missing HTTP/1.1\r\nHost: x\r\n\r\n")[0] == b"HTTP/1.1 404 Not Found"
                expect(reader, 200, "keep-alive", body)
                s.sendall(req("/echo", b"after large", connection="close"))
                expect(reader, 200, "close", b"after large")
                reader.eof()
            net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "大响应复用后未回收")
            # 对端半关闭但仍慢接收时，LT 不能持续关注已确认的 RDHUP，避免空转。
            with socket.socket() as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
                s.settimeout(5)
                s.connect(net.ADDRESS)
                s.sendall(req())
                time.sleep(0.1)
                s.shutdown(socket.SHUT_WR)
                def rdhup_removed():
                    for fd in Path(f"/proc/{p.pid}/fd").iterdir():
                        try:
                            if fd.readlink().as_posix() != "anon_inode:[eventpoll]":
                                continue
                            info = Path(f"/proc/{p.pid}/fdinfo/{fd.name}").read_text()
                            masks = [int(v, 16) for v in re.findall(
                                r"events:\s*([0-9a-fA-F]+)", info)]
                            return bool(masks) and all(not (mask & 0x2000) for mask in masks)
                        except FileNotFoundError:
                            pass
                    return False
                net.wait_for(rdhup_removed, "已确认半关闭后仍关注 RDHUP，LT 可持续空转")
                reader = Responses(s)
                expect(reader, 200, None, body)
                reader.eof()
            net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "半关闭大响应未回收")
            print("PASS 半关闭慢接收大响应完整，确认移除 RDHUP 关注")
        if probe:
            evidence = log.read_text()
            assert "partial send" in evidence and "kernel EAGAIN" in evidence
            assert "injected EAGAIN" not in evidence
            print("PASS 大响应复用真实部分发送=", evidence.count("partial send"),
                  "真实 send EAGAIN=", evidence.count("kernel EAGAIN"))
        print("PASS 8MiB 慢响应完整、其他连接可用、同连接继续 echo 后关闭")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=lambda s: Path(s).resolve(), default=net.SERVER)
    parser.add_argument("--mode", choices=("LT", "ET"), required=True)
    parser.add_argument("--send-probe", type=lambda s: Path(s).resolve())
    args = parser.parse_args()
    net.SERVER = args.server
    net.EXPECTED_MODE = args.mode
    ordinary()
    large(args.send_probe)
    print(args.mode, "Keep-Alive 验收通过，测试进程已回收")
