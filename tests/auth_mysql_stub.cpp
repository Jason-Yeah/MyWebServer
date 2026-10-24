// 仅测试链接包装：保留真实 AuthService/密码哈希/参数绑定，替代 MySQL 网络访问。
#include "auth_mysql_stub.h"
#include <mysql/mysql.h>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct FakeConnection {};
struct FakeStatement {
    bool insert = false;
    unsigned int error = 0;
    bool found = false;
    std::string hash;
    std::vector<MYSQL_BIND> params;
    MYSQL_BIND output{};
};
std::mutex mutex;
std::map<std::string, std::string> users;
std::atomic<AuthDbFailure> failure{AuthDbFailure::None};
std::atomic<int> connections{0}, statements{0}, threads{0};
bool Fails(AuthDbFailure which)
{
    auto expected = which;
    return failure.compare_exchange_strong(expected, AuthDbFailure::None);
}
FakeStatement* State(MYSQL_STMT* stmt) { return reinterpret_cast<FakeStatement*>(stmt); }
std::string Value(const MYSQL_BIND& bind)
{
    assert(bind.buffer_type == MYSQL_TYPE_STRING && bind.length);
    return std::string(static_cast<const char*>(bind.buffer), *bind.length);
}
}
void AuthDbFailNext(AuthDbFailure next) { failure = next; }
void AuthDbReset()
{
    assert(connections == 0 && statements == 0 && threads == 0);
    std::lock_guard<std::mutex> lock(mutex);
    users.clear();
    failure = AuthDbFailure::None;
}
std::string AuthDbStoredHash(const std::string& username)
{
    std::lock_guard<std::mutex> lock(mutex);
    return users.at(username);
}
void AuthDbCorruptHash(const std::string& username)
{
    std::lock_guard<std::mutex> lock(mutex);
    users.at(username) = "invalid-stored-hash";
}
int AuthDbLiveConnections() { return connections; }
int AuthDbLiveStatements() { return statements; }
int AuthDbThreadBalance() { return threads; }

extern "C" int STDCALL __wrap_mysql_server_init(int, char**, char**) { return 0; }
extern "C" void STDCALL __wrap_mysql_server_end()
{
    assert(connections == 0 && statements == 0 && threads == 0);
}
extern "C" MYSQL* STDCALL __wrap_mysql_init(MYSQL*)
{
    ++connections;
    return reinterpret_cast<MYSQL*>(new FakeConnection);
}
extern "C" int STDCALL __wrap_mysql_options(MYSQL*, enum mysql_option, const void*) { return 0; }
extern "C" MYSQL* STDCALL __wrap_mysql_real_connect(
    MYSQL* conn, const char*, const char*, const char*, const char* database,
    unsigned int, const char*, unsigned long)
{
    assert(database && std::string(database) == "mywebserver");
    return conn;
}
extern "C" unsigned int STDCALL __wrap_mysql_errno(MYSQL*) { return 2003; }
extern "C" void STDCALL __wrap_mysql_close(MYSQL* conn)
{
    delete reinterpret_cast<FakeConnection*>(conn);
    --connections;
}
extern "C" auto STDCALL __wrap_mysql_thread_init() -> decltype(mysql_thread_init())
{
    ++threads;
    return false;
}
extern "C" void STDCALL __wrap_mysql_thread_end() { --threads; }
extern "C" MYSQL_STMT* STDCALL __wrap_mysql_stmt_init(MYSQL*)
{
    ++statements;
    return reinterpret_cast<MYSQL_STMT*>(new FakeStatement);
}
extern "C" int STDCALL __wrap_mysql_stmt_prepare(MYSQL_STMT* stmt, const char* sql, unsigned long length)
{
    auto* state = State(stmt);
    if (Fails(AuthDbFailure::Prepare) || std::getenv("AUTH_TEST_NO_TABLE")) {
        state->error = 1146;
        return 1;
    }
    const std::string query(sql, length);
    // 每一次都验证 SQL 结构固定，不能混入任何用户名或密码。
    assert(query == "SELECT password_hash FROM users WHERE username = ? LIMIT 1"
           || query == "INSERT INTO users (username, password_hash) VALUES (?, ?)");
    state->insert = query[0] == 'I';
    return 0;
}
extern "C" bool STDCALL __wrap_mysql_stmt_bind_param(MYSQL_STMT* stmt, MYSQL_BIND* params)
{
    auto* state = State(stmt);
    if (Fails(AuthDbFailure::Bind)) { state->error = 2031; return true; }
    state->params.assign(params, params + (state->insert ? 2 : 1));
    return false;
}
extern "C" int STDCALL __wrap_mysql_stmt_execute(MYSQL_STMT* stmt)
{
    auto* state = State(stmt);
    if (Fails(AuthDbFailure::Execute)
        || (state->insert && std::getenv("AUTH_TEST_FAIL_WRITE"))) {
        state->error = 2013;
        return 1;
    }
    std::lock_guard<std::mutex> lock(mutex);
    const auto username = Value(state->params.at(0));
    if (state->insert) {
        const auto inserted = users.emplace(username, Value(state->params.at(1)));
        if (!inserted.second) { state->error = 1062; return 1; }
    } else {
        const auto it = users.find(username);
        state->found = it != users.end();
        if (state->found) state->hash = it->second;
    }
    return 0;
}
extern "C" auto STDCALL __wrap_mysql_stmt_affected_rows(MYSQL_STMT*) -> decltype(mysql_stmt_affected_rows(nullptr))
{
    return 1;
}
extern "C" int STDCALL __wrap_mysql_stmt_store_result(MYSQL_STMT* stmt)
{
    if (Fails(AuthDbFailure::Store)) { State(stmt)->error = 2013; return 1; }
    return 0;
}
extern "C" bool STDCALL __wrap_mysql_stmt_bind_result(MYSQL_STMT* stmt, MYSQL_BIND* output)
{
    State(stmt)->output = *output;
    return false;
}
extern "C" int STDCALL __wrap_mysql_stmt_fetch(MYSQL_STMT* stmt)
{
    auto* state = State(stmt);
    if (Fails(AuthDbFailure::Fetch)) { state->error = 2013; return 1; }
    if (!state->found) return MYSQL_NO_DATA;
    auto& output = state->output;
    if (Fails(AuthDbFailure::NullResult)) { *output.is_null = true; return 0; }
    if (Fails(AuthDbFailure::Truncate)) { *output.error = true; return MYSQL_DATA_TRUNCATED; }
    assert(state->hash.size() <= output.buffer_length);
    std::memcpy(output.buffer, state->hash.data(), state->hash.size());
    *output.length = static_cast<unsigned long>(state->hash.size());
    *output.is_null = false;
    *output.error = false;
    state->found = false;
    return 0;
}
extern "C" unsigned int STDCALL __wrap_mysql_stmt_errno(MYSQL_STMT* stmt) { return State(stmt)->error; }
extern "C" bool STDCALL __wrap_mysql_stmt_close(MYSQL_STMT* stmt)
{
    delete State(stmt);
    --statements;
    return false;
}
