import http.client
from urllib.parse import urlencode

cases = [
    ("普通表单", "name=jason&message=hello+world", 200,
     "name=jason\nmessage=hello world\n"),
    ("编码后的分隔符", "name=jason&message=a%26b%3Dc", 200,
     "name=jason\nmessage=a&b=c\n"),
    ("加号和空格", "name=jason&message=C%2B%2B+server", 200,
     "name=jason\nmessage=C++ server\n"),
    ("中文", urlencode({"name": "jason", "message": "你好 C++"}), 200,
     "name=jason\nmessage=你好 C++\n"),
    ("非法百分号", "name=jason&message=%GG", 400, None),
    ("缺少字段", "name=jason", 400, None),
    ("重复字段", "name=jason&name=other&message=test", 400, None),
]

for name, body, expected_status, expected_body in cases:
    conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=3)
    try:
        conn.request("POST", "/form", body=body.encode("ascii"), headers={
            "Content-Type": "application/x-www-form-urlencoded"
        })
        response = conn.getresponse()
        data = response.read()
        assert response.status == expected_status, (name, response.status, data)
        if expected_body is not None:
            assert data == expected_body.encode("utf-8"), (name, data)
        print("通过：" + name)
    finally:
        conn.close()

for method, content_type, expected in [
    ("GET", "application/x-www-form-urlencoded", 405),
    ("POST", "application/json", 415),
]:
    conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=3)
    try:
        conn.request(method, "/form", headers={"Content-Type": content_type})
        response = conn.getresponse()
        response.read()
        assert response.status == expected, (method, response.status)
        if expected == 405:
            assert response.getheader("Allow") == "POST"
        print(f"通过：{method} {content_type} → {expected}")
    finally:
        conn.close()
