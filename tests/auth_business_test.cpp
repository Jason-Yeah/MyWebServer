#include "../code/auth/authservice.h"
#include "../code/auth/passwordhash.h"
#include "../code/http/httphandler.h"
#include "../code/buffer/buffer.h"
#include "auth_mysql_stub.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <thread>

static std::string Response(AuthService* auth, const std::string& path,
                            const std::string& body, const std::string& method = "POST",
                            const std::string& type = "application/x-www-form-urlencoded")
{
    const auto raw = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n"
        "Connection: keep-alive\r\nContent-Type: " + type + "\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
    Buffer input;
    input.Append(raw.data(), raw.size());
    HttpParser parser;
    assert(parser.Parse(input) == HttpParseResult::Complete);
    return BuildResponse(parser.Request(), HttpParseResult::Complete, true, auth);
}
static void Status(const std::string& response, int status)
{
    assert(response.find("HTTP/1.1 " + std::to_string(status) + " ") == 0);
    assert(response.find("Cache-Control: no-store\r\n") != std::string::npos);
    assert(response.find("Connection: keep-alive\r\n") != std::string::npos);
    assert(response.find("Set-Cookie:") == std::string::npos);
    const auto split = response.find("\r\n\r\n");
    const auto length = response.find("Content-Length: ");
    assert(split != std::string::npos && length != std::string::npos);
    assert(std::stoul(response.substr(length + 16)) == response.size() - split - 4);
}

int main()
{
    const std::string password = "synthetic-test-password";
    const auto a = HashPwd(password), b = HashPwd(password);
    assert(a != b && a.find(password) == std::string::npos);
    assert(VerifyPwd(password, a));
    assert(!VerifyPwd("synthetic-wrong-password", a));
    // 此向量由 Python hashlib 独立计算，避免 Hash/Verify 同时出错却互相通过。
    assert(VerifyPwd("synthetic-known-password",
        "pbkdf2-sha256$600000$000102030405060708090a0b0c0d0e0f$"
        "b82dd03d7e3f9cea0001cea878f4e5fc62b3a0f45b657a6c792362d9083b562a"));
    for (const auto& invalid : {std::string("invalid"), a.substr(0, a.size() - 1),
                               std::string("pbkdf2-sha256$999999999$bad")}) {
        bool threw = false;
        try { VerifyPwd(password, invalid); }
        catch (const std::runtime_error&) { threw = true; }
        assert(threw);
    }
    std::puts("PASS 真实 OpenSSL：独立已知向量、随机盐、正确/错误密码、损坏格式拒绝");

    AuthDbReset();
    AuthDbFailNext(AuthDbFailure::Prepare);
    bool failed = false;
    try { AuthService auth("test-only", "test-only", "mywebserver"); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed && AuthDbLiveConnections() == 0 && AuthDbLiveStatements() == 0);

    {
        AuthService auth("test-only", "test-only", "mywebserver");
        assert(!ValidCredentials("", password));
        assert(!ValidCredentials(std::string(65, 'u'), password));
        assert(!ValidCredentials("name", "short"));
        assert(!ValidCredentials("name", std::string(129, 'p')));
        assert(!ValidCredentials(std::string("a\0b", 3), password));
        assert(!ValidCredentials("name", std::string("1234\0abc", 8)));
        assert(ValidCredentials(std::string(64, 'u'), std::string(128, 'p')));
        assert(auth.Register("alice", password) == AuthResult::Registered);
        assert(AuthDbStoredHash("alice") != password);
        assert(VerifyPwd(password, AuthDbStoredHash("alice")));
        assert(auth.Register("alice", password) == AuthResult::Conflict);
        assert(auth.Login("alice", password) == AuthResult::Verified);
        assert(auth.Login("alice", "synthetic-wrong-password") == AuthResult::Unauthorized);
        assert(auth.Login("missing", password) == AuthResult::Unauthorized);
        assert(auth.Login("missing", "non-user-dummy-password") == AuthResult::Unauthorized);
        const std::string quoted = "x' OR 1=1 --";
        assert(auth.Login(quoted, password) == AuthResult::Unauthorized);
        assert(auth.Register(quoted, password) == AuthResult::Registered);
        assert(auth.Login(quoted, password) == AuthResult::Verified);
        std::puts("PASS 业务层：注册/重复/密码/不存在用户；绑定字节和固定 SQL 检查");

        std::atomic<int> registered{0}, conflicts{0};
        auto duplicate = [&] {
            const auto result = auth.Register("same-name", password);
            if (result == AuthResult::Registered) ++registered;
            if (result == AuthResult::Conflict) ++conflicts;
        };
        std::thread first(duplicate), second(duplicate);
        first.join(); second.join();
        assert(registered == 1 && conflicts == 1);

        AuthDbFailNext(AuthDbFailure::Execute);
        assert(auth.Register("failed-insert", password) == AuthResult::Unavailable);
        assert(auth.Login("failed-insert", password) == AuthResult::Unauthorized);
        for (auto kind : {AuthDbFailure::Prepare, AuthDbFailure::Bind, AuthDbFailure::Execute,
                          AuthDbFailure::Store, AuthDbFailure::Fetch,
                          AuthDbFailure::NullResult, AuthDbFailure::Truncate}) {
            AuthDbFailNext(kind);
            assert(auth.Login("alice", password) == AuthResult::Unavailable);
            assert(AuthDbLiveStatements() == 0 && AuthDbThreadBalance() == 0);
        }
        assert(auth.Login("alice", password) == AuthResult::Verified);
        std::puts("PASS 替身故障注入：同名注册、插入失败、查询故障、NULL/截断及资源回收");

        Status(Response(&auth, "/register", "", "GET"), 405);
        Status(Response(&auth, "/login", "{}", "POST", "application/json"), 415);
        for (const auto& body : {"username=a", "username=%GG&password=abcdefgh",
                                "username=a&username=b&password=abcdefgh",
                                "username=a&password=abcdefgh&extra=x",
                                "username=a&password=short"})
            Status(Response(&auth, "/register", body), 400);
        const std::string form = "username=http_name&password=synthetic-http-password";
        Status(Response(nullptr, "/register", form), 503);
        Status(Response(&auth, "/register", form), 201);
        Status(Response(&auth, "/register", form), 409);
        const auto verified = Response(&auth, "/login", form);
        Status(verified, 200);
        assert(verified.find("http_name") == std::string::npos);
        assert(verified.find("synthetic-http-password") == std::string::npos);
        const auto wrong = Response(&auth, "/login", "username=http_name&password=wrong-password");
        const auto missing = Response(&auth, "/login", "username=no-user&password=wrong-password");
        Status(wrong, 401); Status(missing, 401);
        assert(wrong == missing);
        AuthDbFailNext(AuthDbFailure::Execute);
        Status(Response(&auth, "/register", "username=failed-http&password=abcdefgh"), 503);
        AuthDbCorruptHash("http_name");
        Status(Response(&auth, "/login", form), 503);
        std::puts("PASS 正式 handler：201/200/400/401/405/409/415/503、长度/no-store/无会话");
    }
    assert(AuthDbLiveConnections() == 0 && AuthDbLiveStatements() == 0 && AuthDbThreadBalance() == 0);
    std::puts("PASS 语句/连接/线程资源全部回收；没有访问真实数据库");
}
