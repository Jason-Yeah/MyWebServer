"""真实等待 30 秒的网络超时验收，不修改服务器时间常量。每模式约 32 秒。"""
import argparse
from contextlib import ExitStack
from pathlib import Path
import socket
import time

import lt_network as net
from keepalive_network import Responses, req, expect


def run():
    with net.server(net.ROOT) as p, ExitStack() as stack:
        sockets = [stack.enter_context(net.connect()) for _ in range(5)]
        idle, partial, keep, active, renewed = sockets
        partial.sendall(b"GET / HTTP/1.1\r\nHost:")
        reader = Responses(keep)
        keep.sendall(req("/echo", b"initial"))
        expect(reader, 200, "keep-alive", b"initial")
        active_reader = Responses(active)
        active.sendall(req("/echo", b"initial"))
        expect(active_reader, 200, "keep-alive", b"initial")
        renewed.sendall(b"GET /missing HTTP/1.1\r\nHost:")
        net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS + 5, "连接未全部接受")
        start = time.monotonic()
        # 在到期前检查，不靠 recv 等待至 EOF；防止过早关闭被当成成功。
        time.sleep(12)
        for s in (idle, partial, keep):
            s.settimeout(0.05)
            try:
                data = s.recv(1)
            except socket.timeout:
                pass
            else:
                raise AssertionError(("30 秒前提前关闭/发送", data))
            s.settimeout(4)
        renewed.sendall(b" local")
        active.sendall(req("/echo", b"renew"))
        expect(active_reader, 200, "keep-alive", b"renew")
        # 活跃及续传连接此时距上次 I/O 约 20 秒，其他三个约 32 秒。
        time.sleep(max(0, 32 - (time.monotonic() - start)))
        for s in (idle, partial, keep):
            assert s.recv(1) == b"", "超过无进展期限未关闭"
        active.sendall(req("/echo", b"still-alive", connection="close"))
        expect(active_reader, 200, "close", b"still-alive")
        active_reader.eof()
        renewed.sendall(b"host\r\nConnection: close\r\n\r\n")
        renewed_reader = Responses(renewed)
        expect(renewed_reader, 404, "close")
        renewed_reader.eof()
        assert net.request()[0] == b"HTTP/1.1 200 OK"
        try:
            net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "超时后 fd 未恢复")
        except AssertionError:
            print("fd 诊断：", {fd.name: str(fd.readlink())
                  for fd in Path(f"/proc/{p.pid}/fd").iterdir()})
            raise
        print("PASS 真实 30 秒策略：空连接/半请求/Keep-Alive 超时，收发续期，其他连接不受影响")
    print(net.EXPECTED_MODE, "超时网络验收通过，测试进程已回收")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=lambda s: Path(s).resolve(), default=net.SERVER)
    parser.add_argument("--mode", choices=("LT", "ET"), required=True)
    args = parser.parse_args()
    net.SERVER = args.server
    net.EXPECTED_MODE = args.mode
    run()
