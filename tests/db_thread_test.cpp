// 仅测试包含实验源码以调用其内部 CheckConcurrent；不修改生产接口。
// 所有可能触达假 MYSQL 的接口均包装，查询固定失败，不联网。
#define main db_smoke_entry_for_test
#include "../code/experiments/db_smoke.cpp"
#undef main

#include <cassert>

namespace {
std::atomic<int> initialized{0};
std::atomic<int> ended{0};
std::atomic<int> queried{0};
std::atomic<int> closed{0};
bool failFirstInit = false;
}

extern "C" MYSQL* STDCALL __wrap_mysql_init(MYSQL*)
{
    return reinterpret_cast<MYSQL*>(new int(0));
}
extern "C" int STDCALL __wrap_mysql_options(MYSQL*, enum mysql_option, const void*) { return 0; }
extern "C" MYSQL* STDCALL __wrap_mysql_real_connect(
    MYSQL* connection, const char*, const char*, const char*, const char*,
    unsigned int, const char*, unsigned long)
{
    return connection;
}
extern "C" unsigned int STDCALL __wrap_mysql_errno(MYSQL*) { return 2013; }
extern "C" void STDCALL __wrap_mysql_close(MYSQL* connection)
{
    delete reinterpret_cast<int*>(connection);
    ++closed;
}
extern "C" auto STDCALL __wrap_mysql_thread_init() -> decltype(mysql_thread_init())
{
    const int attempt = ++initialized;
    return failFirstInit && attempt == 1;
}
extern "C" void STDCALL __wrap_mysql_thread_end() { ++ended; }
extern "C" int STDCALL __wrap_mysql_query(MYSQL*, const char*)
{
    ++queried;
    return 1; // 注入查询失败，绝不调用真实库读取假指针。
}

static void CheckFailure(bool failInit)
{
    initialized = ended = queried = closed = 0;
    failFirstInit = failInit;
    {
        SqlConnPool pool("test-only", "test-only", 2);
        assert(CheckConcurrent(pool) == 1);
        const int expected = failInit ? 1 : 2;
        assert(initialized == 2 && queried == expected);
        if (ended != expected) {
            std::fprintf(stderr, "thread cleanup mismatch: expected=%d actual=%d\n",
                         expected, ended.load());
        }
        assert(ended == expected);
        SqlConnLease first(pool), second(pool), third(pool);
        assert(first.Get() && second.Get() && !third.Get());
    }
    assert(closed == 2);
}

int main()
{
    CheckFailure(false);
    CheckFailure(true);
    std::puts("PASS 注入查询失败仍归还连接并清理线程；线程初始化失败不调用 thread_end");
}
