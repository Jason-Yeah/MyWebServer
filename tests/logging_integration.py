"""检查日志实际接入、64 位连接身份和请求敏感内容不被输出。"""
import re
import subprocess

import lt_network as net


def async_logs():
    for mode in ("LT", "ET"):
        result = subprocess.run([str(net.ROOT / "build/server_async_test"), mode],
                                capture_output=True, text=True, timeout=20, check=True)
        text = result.stderr
        for level, event in (("WARN", "task_rejected"),
                             ("ERROR", "response_build_failed"),
                             ("INFO", "idle_timeout")):
            pattern = rf"\[{level}\] event={event} fd=\d+ id=(\d+) errno=\d+"
            matches = re.findall(pattern, text)
            assert matches and all(int(value) > 2**32 for value in matches), (mode, event, text)
        ids = re.findall(r"event=(?:closed|task_rejected_close|response_build_failed_close) fd=\d+ id=(\d+)", text)
        assert ids and all(int(value) > 2**32 for value in ids), (mode, text)
        print("PASS", mode, "任务拒绝/构造失败/超时日志及关闭日志保留64位id")


def network_logs(mode, executable):
    net.SERVER = executable
    logs = []
    marker = b"synthetic-private-body-do-not-log"
    request = (b"POST /echo HTTP/1.1\r\nHost: localhost\r\n"
               b"Authorization: Bearer synthetic-private-token\r\n"
               b"Cookie: synthetic-private-cookie\r\n"
               b"Content-Type: text/plain\r\nContent-Length: " + str(len(marker)).encode()
               + b"\r\n\r\n" + marker)
    with net.server(net.ROOT, logs=logs) as p:
        assert net.request(request) == (b"HTTP/1.1 200 OK", marker)
        net.wait_for(lambda: net.fd_count(p) == net.SERVER_IDLE_FDS, "连接未回收")
    text = "".join(logs)
    assert "synthetic-private" not in text, text
    accepted = set(re.findall(r"event=accepted fd=(\d+) id=(\d+)", text))
    closed = set(re.findall(r"event=closed fd=(\d+) id=(\d+)", text))
    assert len(accepted) >= 2 and accepted == closed, text
    print("PASS", mode, "接受/关闭可配对，正文/Authorization/Cookie 未记录")


if __name__ == "__main__":
    async_logs()
    network_logs("ET", net.ROOT / "build/server")
    network_logs("LT", net.ROOT / "build/server_lt_test")
