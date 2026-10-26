"""注册登录专项：只构建测试产物，不改 CMake/业务源码，不写真实数据库。
python3 tests/auth_checks.py
python3 tests/auth_checks.py --sanitize  # 仅业务专项 ASan/UBSan
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shlex
import socket
import subprocess
import urllib.parse

import lt_network as net
from keepalive_network import Responses, req

ROOT = net.ROOT
WRAPS = """mysql_server_init mysql_server_end mysql_init mysql_options mysql_real_connect
mysql_errno mysql_close mysql_thread_init mysql_thread_end mysql_stmt_init mysql_stmt_prepare
mysql_stmt_bind_param mysql_stmt_execute mysql_stmt_affected_rows mysql_stmt_store_result
mysql_stmt_bind_result mysql_stmt_fetch mysql_stmt_errno mysql_stmt_close""".split()
COMMON = ["tests/auth_mysql_stub.cpp", "code/auth/authservice.cpp", "code/auth/passwordhash.cpp",
          "code/pool/sqlconnpool.cpp", "code/http/httphandler.cpp", "code/http/staticfile.cpp",
          "code/http/formparser.cpp",
          "code/http/httpparser.cpp", "code/http/httprequest.cpp", "code/buffer/buffer.cpp"]


def environment(enabled=False):
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(("MYWEB_", "AUTH_TEST_")):
            del env[key]
    if enabled:
        env.update(MYWEB_AUTH="1", MYWEB_DB_USER="test-only", MYWEB_DB_PASSWORD="test-only")
    return env


def build(name, sources, sanitize=False):
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "mysqlclient", "openssl"], text=True))
    command = ["g++", "-std=c++14", "-g", "-O1", "-Wall", "-Wextra", "-UNDEBUG"]
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += sources + COMMON + flags + ["-pthread"]
    command += [f"-Wl,--wrap={symbol}" for symbol in WRAPS]
    command += ["-o", str(ROOT / "build" / name)]
    subprocess.run(command, cwd=ROOT, check=True, timeout=120)
    return ROOT / "build" / name


def form(user="network-user", password="synthetic-network-password"):
    return urllib.parse.urlencode({"username": user, "password": password}).encode()


def packet(path, body):
    return req(path, body, connection="close", content_type="application/x-www-form-urlencoded")


def send(path, body, expected):
    status, result = net.request(packet(path, body))
    assert int(status.split()[1]) == expected, (status, expected, result)
    return result


def startup_checks(fixture):
    for env in (dict(environment(), MYWEB_AUTH="wrong"), dict(environment(), MYWEB_AUTH="1")):
        p = subprocess.run([str(ROOT / "build/server")], cwd=ROOT, env=env,
                           capture_output=True, timeout=5)
        assert p.returncode == 1
    env = environment(True)
    env["AUTH_TEST_NO_TABLE"] = "1"
    p = subprocess.run([str(fixture)], cwd=ROOT, env=env, capture_output=True, timeout=5)
    assert p.returncode == 1 and b"schema is unavailable" in p.stderr
    print("PASS 正式 main 缺失凭据/错误开关退出；替身模拟缺表启动失败")


def network_checks(fixture):
    net.SERVER = fixture
    net.EXPECTED_MODE = "ET"
    logs = []
    with net.server(ROOT, environment(True), logs=logs) as p:
        net.check_event_mode(p)
        send("/register", form(), 201)
        send("/register", form(), 409)
        send("/login", form(), 200)
        wrong = send("/login", form(password="synthetic-wrong-password"), 401)
        missing = send("/login", form("no-such-user", "synthetic-wrong-password"), 401)
        assert wrong == missing
        quoted = "x' OR 1=1 --"
        send("/login", form(quoted), 401)
        send("/register", form(quoted), 201)
        send("/login", form(quoted), 200)
        send("/register", b"username=a&password=short", 400)
        with net.connect() as s:
            reader = Responses(s)
            for path, expected in (("/register", 201), ("/login", 200), ("/register", 409)):
                s.sendall(req(path, form("keep-user"),
                              content_type="application/x-www-form-urlencoded"))
                status, headers, data = reader.read()
                assert status == expected
                assert headers[b"connection"] == b"keep-alive"
                assert headers[b"cache-control"] == b"no-store"
                assert b"set-cookie" not in headers
                assert b"synthetic-network-password" not in data
            s.sendall(req("/missing", connection="close"))
            assert reader.read()[0] == 404
            reader.eof()
        # 半请求不能注册；补齐后才成功，另一连接仍可响应。
        raw = packet("/register", form("split-user"))
        with net.connect() as s:
            s.sendall(raw[:-3])
            s.settimeout(0.1)
            try:
                data = s.recv(1)
            except socket.timeout:
                pass
            else:
                raise AssertionError(("半请求提前响应", data))
            assert net.request()[0] == b"HTTP/1.1 200 OK"
            s.settimeout(5)
            s.sendall(raw[-3:])
            assert net.read_response(s)[0] == b"HTTP/1.1 201 Created"
        with net.connect() as s:
            s.sendall(packet("/register", form("half-close-user")))
            s.shutdown(socket.SHUT_WR)
            assert net.read_response(s)[0] == b"HTTP/1.1 201 Created"
        def duplicate(_):
            status, _ = net.request(packet("/register", form("same-time-user")))
            return int(status.split()[1])
        with ThreadPoolExecutor(max_workers=2) as executor:
            assert sorted(executor.map(duplicate, range(2))) == [201, 409]
        net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "注册登录后 fd 未恢复")
    print("PASS 真实 ET socket/Server/线程池/AuthService/哈希：注册登录、并发同名、Keep-Alive、分段及半关闭")

    env = environment(True)
    env["AUTH_TEST_FAIL_WRITE"] = "1"
    with net.server(ROOT, env, logs=logs):
        send("/register", form("failed-write"), 503)
        send("/login", form("failed-write"), 401)
    print("PASS 替身注入写入失败，正式 HTTP 返回 503 而非注册成功")

    net.SERVER = ROOT / "build/server"
    with net.server(ROOT, environment(), logs=logs):
        send("/register", form(), 503)
        send("/login", form(), 503)
    assert "synthetic-" not in "".join(logs)
    assert quoted not in "".join(logs)
    print("PASS 正式 server 未开启认证返回 503；请求密码和引号用户名未进入日志")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    binary = build("auth_business_test_asan" if args.sanitize else "auth_business_test",
                   ["tests/auth_business_test.cpp"], args.sanitize)
    env = environment()
    if args.sanitize:
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1")
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True, timeout=45)
    if not args.sanitize:
        fixture = build("auth_server_fixture", ["code/main.cpp", "code/server/server.cpp",
                        "code/server/epoller.cpp", "code/pool/threadpool.cpp", "code/log/logger.cpp"])
        startup_checks(fixture)
        network_checks(fixture)
    print("专项通过。MySQL 函数为测试替身，没有读写真实数据库；业务源码和 CMake 未修改。")
