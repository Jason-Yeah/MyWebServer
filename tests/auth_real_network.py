"""真实 MySQL + 正式 HTTP 服务验收。
交互输入数据库凭据；不存密码、不执行 DDL/GRANT/DELETE。
会在 mywebserver.users 保留 1 个 real_check_ 前缀测试用户（随机测试密码不输出）。
只在用户执行本脚本时才启动服务和写入测试记录，8080 已占用时退出。
"""
import argparse
import getpass
import os
from pathlib import Path
import secrets
import socket
import sys
import urllib.parse
import uuid

import lt_network as net
from keepalive_network import Responses, req


def verify_response(path, username, password, expected):
    body = urllib.parse.urlencode({"username": username, "password": password}).encode()
    with net.connect() as s:
        s.settimeout(10)
        s.sendall(req(path, body, connection="close",
                      content_type="application/x-www-form-urlencoded"))
        reader = Responses(s)
        status, headers, response = reader.read()
        reader.eof()
    if status != expected:
        # 不打印请求或完整响应，避免异常响应回显密码。
        raise AssertionError(f"{path} 预期 HTTP {expected}，实际 HTTP {status}")
    assert headers.get(b"cache-control") == b"no-store", "认证响应缺少 no-store"
    assert b"set-cookie" not in headers, "本阶段不应创建登录会话"
    assert password.encode() not in response, "响应中出现了提交的密码"
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--user", default="jason", help="MySQL 学习账号（不是网站用户名）")
    parser.add_argument("--server", type=lambda s: Path(s).resolve(), default=net.SERVER)
    args = parser.parse_args()
    if not args.server.is_file():
        parser.error("请先构建正式 server")
    # 在提示密码/进行任何数据库操作之前检查端口；不结束用户自己的服务。
    try:
        with socket.socket() as check:
            check.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            check.bind(net.ADDRESS)
    except OSError:
        parser.error("127.0.0.1:8080 已占用，请先自行停止占用服务")

    print("将通过正式 HTTP 接口写入 1 个测试用户到 mywebserver.users，并重启测试服务检查持久化。")
    print("不建表、不改权限、不删除已有数据；测试记录会保留。")
    database_password = getpass.getpass(f"MySQL 账号 {args.user} 的密码（不显示）: ")
    if not database_password:
        parser.error("数据库密码不能为空")

    env = os.environ.copy()
    env.update(MYWEB_AUTH="1", MYWEB_DB_USER=args.user,
               MYWEB_DB_PASSWORD=database_password)
    # 测试应访问真实 MySQL，不继承之前可能残留的 LD_PRELOAD 探针。
    env.pop("LD_PRELOAD", None)
    net.SERVER = args.server
    net.EXPECTED_MODE = "ET"
    username = "real_check_" + uuid.uuid4().hex[:20]
    password = secrets.token_urlsafe(24)
    logs = []
    try:
        with net.server(net.ROOT, env, logs=logs):
            verify_response("/register", username, password, 201)
            print("PASS 注册成功：HTTP 201")
            verify_response("/register", username, password, 409)
            print("PASS 重复注册：HTTP 409")
            verify_response("/login", username, password, 200)
            print("PASS 正确密码：HTTP 200")
            wrong = verify_response("/login", username, password + "x", 401)
            missing = verify_response("/login", "missing_" + uuid.uuid4().hex[:20], password, 401)
            assert wrong == missing, "错误密码与不存在用户响应不一致"
            print("PASS 错误密码/不存在用户：相同 HTTP 401 响应")
            assert net.request()[0] == b"HTTP/1.1 200 OK", "原首页接口异常"
        # 不是内存中假装注册：创建全新正式服务，之前用户仍应可以验证。
        with net.server(net.ROOT, env, logs=logs):
            verify_response("/login", username, password, 200)
            print("PASS 重启正式服务后仍可验证：真实数据库持久化")
        text = "".join(logs)
        assert password not in text, "服务日志中出现网站测试密码"
        print("真实注册登录验收通过，脚本启动的服务已回收。")
        print(f"保留的测试用户名：{username}（随机测试密码不输出）")
    except Exception:
        print("验收未通过；此前成功写入的测试用户可能仍保留，脚本不会自动删除。", file=sys.stderr)
        for log in logs:
            safe = log.replace(database_password, "<redacted-db-password>")
            safe = safe.replace(password, "<redacted-test-password>")
            if safe.strip():
                print(safe.strip(), file=sys.stderr)
        raise
    finally:
        env.pop("MYWEB_DB_PASSWORD", None)


if __name__ == "__main__":
    main()
