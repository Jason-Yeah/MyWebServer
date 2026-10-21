"""LT 集成验收：自管服务进程，端口占用时退出，不修改现有首页。

python3 MyWebServer/tests/lt_network.py
可选：--send-probe MyWebServer/build/send_probe.so
"""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "build/server"
ADDRESS = ("127.0.0.1", 8080)
GET = b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"


def connect():
    return socket.create_connection(ADDRESS, timeout=3)


def read_response(s):
    data = bytearray()
    while True:
        part = s.recv(65536)
        if not part:
            break
        data.extend(part)
    header, body = bytes(data).split(b"\r\n\r\n", 1)
    fields = dict(line.split(b": ", 1) for line in header.split(b"\r\n")[1:])
    assert len(body) == int(fields[b"Content-Length"])
    assert fields[b"Connection"] == b"close"
    return header.split(b"\r\n", 1)[0], body


def request(data=GET):
    with connect() as s:
        s.sendall(data)
        return read_response(s)


def wait_for(predicate, message):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError(message)


@contextmanager
def server(cwd, env=None):
    # 与服务器相同地复用 TIME_WAIT，但不使用 SO_REUSEPORT 分享监听端口。
    with socket.socket() as check:
        check.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        check.bind(ADDRESS)
    with tempfile.TemporaryFile() as log:
        p = subprocess.Popen([str(SERVER)], cwd=cwd, env=env,
                             stdout=log, stderr=log)
        try:
            def ready():
                assert p.poll() is None, "本次服务器启动失败"
                try:
                    with connect():
                        return True
                except OSError:
                    return False
            wait_for(ready, "服务器未就绪")
            yield p
            assert p.poll() is None, "测试期间服务器异常退出"
        finally:
            if p.poll() is None:
                p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
            log.seek(0)
            output = log.read().decode(errors="replace")
            if output:
                print("服务日志：", output.strip())


def fd_count(p):
    return len(list(Path(f"/proc/{p.pid}/fd").iterdir()))


def ordinary_tests():
    with server(ROOT) as p:
        for name in ("split_request", "header_validation", "bad_request",
                     "body_request", "framing_validation", "form_request"):
            subprocess.run([sys.executable, str(ROOT / "tests" / (name + ".py"))],
                           check=True, timeout=20)
            print("PASS existing:", name, flush=True)
        expected = request()
        assert expected[0] == b"HTTP/1.1 200 OK"
        wait_for(lambda: fd_count(p) == 5, "基线 fd 未回收")
        baseline = fd_count(p)
        for prefix, suffix, result in (
            (GET[:-1], GET[-1:], expected),
            (b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
             b"Content-Type: text/plain\r\nContent-Length: 5\r\n\r\nhe",
             b"llo", (b"HTTP/1.1 200 OK", b"hello")),
        ):
            with connect() as a:
                a.sendall(prefix)
                a.settimeout(0.15)
                try:
                    data = a.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"半请求被提前处理: {data!r}")
                start = time.monotonic()
                assert request() == expected
                assert time.monotonic() - start < 2
                a.settimeout(3)
                a.sendall(suffix)
                assert read_response(a) == result
        print("PASS 慢请求头/正文隔离，B 完成后 A 补齐成功", flush=True)

        # 请求超过单次 recv 的 4096 字节，FIN 不能导致未读数据丢失。
        body = bytes(range(256)) * 256
        echo = (b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
                b"Content-Type: text/plain\r\nContent-Length: 65536\r\n\r\n" + body)
        for data, wanted in ((GET, expected), (echo, (b"HTTP/1.1 200 OK", body))):
            with connect() as s:
                s.sendall(data)
                s.shutdown(socket.SHUT_WR)
                assert read_response(s) == wanted
        for data in (b"GET / HTTP/1.1\r\nHost:", echo[:-1]):
            with connect() as s:
                s.sendall(data)
                s.shutdown(socket.SHUT_WR)
                assert s.recv(4096) == b""
        print("PASS 完整小请求/64KiB 请求后半关闭；截断请求 EOF", flush=True)

        # 低负载顺序连接，不是并发压测。
        for i in range(40):
            with connect() as s:
                if i % 3 == 0:
                    s.sendall(b"GET / HTTP/1.1\r\n")
                elif i % 3 == 1:
                    s.sendall(GET)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        assert request() == expected
        wait_for(lambda: fd_count(p) == baseline, "RST/关闭后 fd 未恢复")
        print(f"PASS RST、空连接、未完成请求回收；fd 恢复 {baseline}", flush=True)


def large_response_tests(probe):
    with tempfile.TemporaryDirectory(prefix="myweb-lt-") as directory:
        root = Path(directory)
        (root / "resources").mkdir()
        body = bytes(range(256)) * (8 * 1024 * 1024 // 256)
        (root / "resources/index.html").write_bytes(body)
        env = os.environ.copy()
        probe_log = root / "send.log"
        if probe:
            env["LD_PRELOAD"] = str(probe)
            env["SEND_PROBE_LOG"] = str(probe_log)
        with server(root, env) as p:
            wait_for(lambda: fd_count(p) == 5, "初始 fd 未回收")
            baseline = fd_count(p)
            with connect() as slow:
                slow.sendall(GET)
                time.sleep(0.25)  # 暂不读取，令大响应滞留在发送阶段。
                assert fd_count(p) == baseline + 1
                start = time.monotonic()
                assert request(b"GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n")[0] == b"HTTP/1.1 404 Not Found"
                assert time.monotonic() - start < 2
                slow.settimeout(5)
                assert read_response(slow) == (b"HTTP/1.1 200 OK", body)
            wait_for(lambda: fd_count(p) == baseline, "大响应完成后 fd 未回收")
            # 大响应未读完即 RST，覆盖有待发送 output 时的关闭。
            with connect() as s:
                s.sendall(GET)
                time.sleep(0.1)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            wait_for(lambda: fd_count(p) == baseline, "大响应 RST 后 fd 未回收")
            assert request(b"GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n")[0] == b"HTTP/1.1 404 Not Found"
        print("PASS 8MiB 响应逐字节相等、慢接收隔离、发送中 RST 和 fd 回收", flush=True)
        if probe:
            evidence = probe_log.read_text()
            assert evidence.count("injected EAGAIN") == 2
            assert "partial send" in evidence
            print("发送探针：真实部分发送次数=", evidence.count("partial send"),
                  "内核 EAGAIN 次数=", evidence.count("kernel EAGAIN"),
                  "注入 EAGAIN 次数=", evidence.count("injected EAGAIN"))
            print("前几条证据：\n" + "\n".join(evidence.splitlines()[:6]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--send-probe", type=lambda s: Path(s).resolve())
    args = parser.parse_args()
    ordinary_tests()
    large_response_tests(args.send_probe)
    print("LT 网络验收全部通过；本次服务进程已回收")
