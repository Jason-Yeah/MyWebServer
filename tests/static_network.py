"""正式 LT/ET 静态资源验收；文件/链接均在 build 下临时目录，不改用户资源或数据库。"""
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser
import os
from pathlib import Path
import shutil
import socket
import tempfile
import time

import lt_network as net
from keepalive_network import Responses, req


class Assets(HTMLParser):
    def __init__(self):
        super().__init__()
        self.paths = []
        self.inline = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "style" or (tag == "script" and "src" not in attrs):
            self.inline = True
        path = attrs.get("src") if tag in ("script", "img") else attrs.get("href") if tag == "link" else None
        if path:
            assert path.startswith("/static/"), path
            self.paths.append(path)


def response(target, expected=200, body=None):
    with net.connect() as s:
        s.sendall(req(target, body, connection="close"))
        reader = Responses(s)
        status, headers, data = reader.read()
        reader.eof()
    assert status == expected, (target, status, expected)
    assert headers[b"connection"] == b"close"
    if target.startswith("/static/"):
        assert headers[b"x-content-type-options"] == b"nosniff"
        assert headers[b"cache-control"] == b"no-store"
    return headers, data


def mode(et):
    net.SERVER = net.ROOT / ("build/server" if et else "build/server_lt_test")
    net.EXPECTED_MODE = "ET" if et else "LT"
    env = os.environ.copy()
    env["MYWEB_AUTH"] = "0"
    for key in ("MYWEB_DB_USER", "MYWEB_DB_PASSWORD", "LD_PRELOAD"):
        env.pop(key, None)
    with tempfile.TemporaryDirectory(prefix="static-network-", dir=net.ROOT / "build") as tmp:
        root = Path(tmp)
        resources = root / "resources"
        assets = resources / "static"
        assets.mkdir(parents=True)
        shutil.copyfile(net.ROOT / "resources/index.html", resources / "index.html")
        for name in ("style.css", "app.js", "pipeline.png"):
            shutil.copyfile(net.ROOT / "resources/static" / name, assets / name)
        binary = bytes(range(256)) * 4
        (assets / "sub").mkdir()
        (assets / "sub/sample.PNG").write_bytes(binary)
        (assets / "empty.txt").write_bytes(b"")
        (assets / "private.txt").write_text("private fixture")
        (assets / "private.txt").chmod(0)
        (assets / "folder.txt").mkdir()
        (root / "secret.txt").write_text("must-not-leak")
        (root / "outside").mkdir()
        (root / "outside/secret.txt").write_text("must-not-leak")
        (assets / "link.txt").symlink_to("../../secret.txt")
        (assets / "linked").symlink_to("../../outside", target_is_directory=True)
        (assets / "loop.txt").symlink_to("loop.txt")
        os.mkfifo(assets / "pipe.txt")
        large = bytes(range(256)) * (8 * 1024 * 1024 // 256)
        (assets / "large.png").write_bytes(large)
        with (assets / "too-large.png").open("wb") as file:
            file.truncate(len(large) + 1)
        logs = []
        with net.server(root, env, logs=logs) as p:
            net.check_event_mode(p)
            headers, index = response("/")
            assert index == (resources / "index.html").read_bytes()
            parser = Assets()
            parser.feed(index.decode())
            assert not parser.inline and len(parser.paths) == 3
            types = {".css": b"text/css; charset=utf-8", ".js": b"text/javascript; charset=utf-8", ".png": b"image/png"}
            for target in parser.paths:
                headers, data = response(target)
                path = resources / target.lstrip("/")
                assert data == path.read_bytes()
                assert headers[b"content-type"] == types[path.suffix]
            assert response("/static/style.css?v=1")[1] == (assets / "style.css").read_bytes()
            assert response("/static/sub/sample.PNG")[1] == binary
            assert response("/static/empty.txt")[1] == b""
            print("PASS", net.EXPECTED_MODE, "首页真实引用 CSS/JS/PNG，内容/类型/长度正确，二进制/空文件/查询参数")

            for target, status in (("/static/missing.css", 404), ("/static/../secret.txt", 400),
                    ("/static/sub/../../secret.txt", 400), ("/static/%2e%2e/secret.txt", 400),
                    ("/static/%252e%252e/secret.txt", 400), ("/static/.secret.txt", 400),
                    ("/static/a\\b.txt", 400), ("/static/", 400), ("/static//style.css", 400),
                    ("/static/link.txt", 403), ("/static/linked/secret.txt", 404),
                    ("/static/loop.txt", 403), ("/static/folder.txt", 403),
                    ("/static/pipe.txt", 403), ("/static/config.env", 403),
                    ("/static/too-large.png", 403), ("/static/" + "a" * 1013 + ".txt", 414)):
                _, data = response(target, status)
                assert b"must-not-leak" not in data
            if os.geteuid() != 0:
                response("/static/private.txt", 403)
            headers, _ = response("/static/style.css", 405, b"x")
            assert headers[b"allow"] == b"GET"
            print("PASS", net.EXPECTED_MODE, "原始 socket 路径安全、符号链接/FIFO/目录/大小限制、POST 405")

            with net.connect() as s:
                s.sendall(req("/static/style.css") + req("/static/sub/sample.PNG")
                          + req("/echo", b"after-static", connection="close"))
                reader = Responses(s)
                status, _, data = reader.read()
                assert status == 200 and data == (assets / "style.css").read_bytes()
                status, _, data = reader.read()
                assert status == 200 and data == binary
                assert reader.read()[2] == b"after-static"
                reader.eof()
            with net.connect() as s:
                s.sendall(req("/static/sub/sample.PNG"))
                s.shutdown(socket.SHUT_WR)
                reader = Responses(s)
                status, headers, data = reader.read()
                assert status == 200 and headers[b"connection"] == b"close" and data == binary
                reader.eof()
            # 一个大响应的客户端暂不读取，另一个客户端仍应正常完成。
            with net.connect() as slow:
                slow.settimeout(10)
                slow.sendall(req("/static/large.png", connection="close"))
                time.sleep(0.05)
                assert response("/static/sub/sample.PNG")[1] == binary
                reader = Responses(slow)
                assert reader.read()[2] == large
                reader.eof()
            def get_asset(_):
                return response("/static/sub/sample.PNG")[1]
            with ThreadPoolExecutor(max_workers=3) as pool:
                assert all(data == binary for data in pool.map(get_asset, range(6)))
            net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "静态资源后 fd 未恢复")
            print("PASS", net.EXPECTED_MODE, "Keep-Alive/预收请求/半关闭/8MiB 一致性/少量并发及 fd 回收")
        # TemporaryDirectory 删除普通文件不需要读文件权限。
    print(net.EXPECTED_MODE, "静态资源网络验收完成，测试进程与临时资源已回收")


if __name__ == "__main__":
    mode(True)
    mode(False)
