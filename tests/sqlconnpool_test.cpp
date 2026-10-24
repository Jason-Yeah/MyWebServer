// 真实 SqlConnPool/SqlConnLease，MySQL 建连函数由仅测试链接包装替代。
// 不读取凭据、不联网；不能代替真实数据库/客户端库的多线程查询验收。
#include "../code/pool/sqlconnpool.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace {
enum class Failure { None, Init, Options, Connect };
Failure failure = Failure::None;
int attempts = 0;
int created = 0;
int closed = 0;
std::set<MYSQL*> live;

void Reset(Failure next = Failure::None)
{
    assert(live.empty());
    failure = next;
    attempts = created = closed = 0;
}
}

extern "C" MYSQL* STDCALL __wrap_mysql_init(MYSQL*)
{
    ++attempts;
    if (failure == Failure::Init && attempts == 2) return nullptr;
    // 指针只用于身份识别，绝不作为真实 MYSQL 解引用或交给真实库。
    auto* connection = reinterpret_cast<MYSQL*>(new int(attempts));
    assert(live.insert(connection).second);
    ++created;
    return connection;
}

extern "C" int STDCALL __wrap_mysql_options(MYSQL* connection, enum mysql_option, const void*)
{
    assert(live.count(connection));
    return failure == Failure::Options && attempts == 2 ? 1 : 0;
}

extern "C" MYSQL* STDCALL __wrap_mysql_real_connect(
    MYSQL* connection, const char*, const char*, const char*, const char*,
    unsigned int, const char*, unsigned long)
{
    assert(live.count(connection));
    return failure == Failure::Connect && attempts == 2 ? nullptr : connection;
}

extern "C" unsigned int STDCALL __wrap_mysql_errno(MYSQL* connection)
{
    assert(live.count(connection));
    return 2003;
}

extern "C" void STDCALL __wrap_mysql_close(MYSQL* connection)
{
    assert(live.erase(connection) == 1); // 重复关闭会在这里失败。
    delete reinterpret_cast<int*>(connection);
    ++closed;
}

static void Basic()
{
    Reset();
    {
        SqlConnPool pool("test-only", "test-only", 2);
        assert(created == 2 && closed == 0);
        MYSQL* returned;
        {
            SqlConnLease first(pool);
            assert(first.Get());
            {
                SqlConnLease second(pool);
                SqlConnLease third(pool);
                assert(second.Get() && first.Get() != second.Get());
                assert(!third.Get());
                returned = second.Get();
            }
            assert(closed == 0); // 归还不是关闭。
            SqlConnLease again(pool);
            assert(again.Get() == returned);
        }
        try {
            SqlConnLease lease(pool);
            assert(lease.Get());
            throw std::runtime_error("test scope unwind");
        } catch (const std::runtime_error&) {}
        SqlConnLease one(pool), two(pool), three(pool);
        assert(one.Get() && two.Get() && one.Get() != two.Get());
        assert(!three.Get());
    }
    assert(closed == 2 && live.empty());
    std::puts("PASS 独占/耗尽/归还不关闭/异常退栈归还/最终关闭恰好一次");
}

static void Rollback()
{
    for (auto kind : {Failure::Init, Failure::Options, Failure::Connect}) {
        Reset(kind);
        bool threw = false;
        try { SqlConnPool pool("test-only", "test-only", 3); }
        catch (const std::runtime_error&) { threw = true; }
        assert(threw && attempts == 2 && live.empty() && created == closed);
    }
    Reset();
    bool threw = false;
    try { SqlConnPool pool("test-only", "test-only", 0); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw && attempts == 0);
    threw = false;
    try { SqlConnPool pool(nullptr, "test-only", 2); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw && attempts == 0);
    std::puts("PASS 第二次 init/options/connect 注入失败，已建连接全部回滚；无效配置拒绝");
}

static void Competition()
{
    Reset();
    {
        SqlConnPool pool("test-only", "test-only", 2);
        std::mutex mutex;
        std::set<MYSQL*> held;
        std::atomic<int> borrowed{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&] {
                for (int j = 0; j < 400; ++j) {
                    SqlConnLease lease(pool);
                    if (!lease.Get()) {
                        std::this_thread::yield();
                        continue;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        assert(held.insert(lease.Get()).second);
                        assert(held.size() <= 2);
                    }
                    ++borrowed;
                    std::this_thread::yield();
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        assert(held.erase(lease.Get()) == 1);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();
        assert(borrowed > 0 && held.empty());
        SqlConnLease one(pool), two(pool), three(pool);
        assert(one.Get() && two.Get() && !three.Get());
    }
    assert(created == 2 && closed == 2 && live.empty());
    std::puts("PASS 四线程少量借还竞争，无重复借用，线程结束后池容量恢复");
}

static void LifetimeViolation()
{
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        std::set_terminate([] { std::_Exit(77); });
        SqlConnPool* pool = new SqlConnPool("test-only", "test-only", 1);
        SqlConnLease lease(*pool);
        assert(lease.Get());
        delete pool; // 故意违反“池必须比借用对象活得久”。
        std::_Exit(1);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    std::puts("PASS 仍有借用时销毁池，按约定 terminate，不继续释放在用连接");
}

int main()
{
    static_assert(!std::is_copy_constructible<SqlConnPool>::value, "pool must not copy");
    static_assert(!std::is_copy_constructible<SqlConnLease>::value, "lease must not copy");
    static_assert(!std::is_move_constructible<SqlConnLease>::value, "lease must not move");
    Basic();
    Rollback();
    Competition();
    LifetimeViolation();
}
