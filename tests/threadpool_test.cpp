#include "../code/pool/threadpool.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono;

template<class T>
static void Await(std::future<T>& future)
{
    assert(future.wait_for(seconds(3)) == std::future_status::ready);
    future.get();
}

static void InvalidAndEmpty()
{
    for (const auto& values : {std::make_pair(0u, 1u), std::make_pair(1u, 0u)}) {
        bool rejected = false;
        try {
            ThreadPool pool(values.first, values.second);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
    }
    for (int i = 0; i < 10; ++i) {
        ThreadPool pool(2, 8);
        assert(!pool.Submit(ThreadPool::Task{}));
    }
    std::puts("PASS 参数检查、空任务拒绝、空池十轮析构");
}

static void BoundedAndClose()
{
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<int> completed{0};
    {
        ThreadPool pool(1, 1);
        assert(pool.Submit([&] {
            started.set_value();
            gate.wait();
            ++completed;
        }));
        Await(startedFuture); // 第一个任务已出队并运行，队列现在为空。
        assert(pool.Submit([&] { ++completed; }));
        assert(!pool.Submit([] {})); // 唯一的等待位置已经占满。
        pool.Close();
        pool.Close();
        assert(!pool.Submit([] {}));
        release.set_value();
    }
    assert(completed == 2);
    std::puts("PASS 队列上限不含运行任务、满队列拒绝、Close 幂等/拒绝新任务/排空");
}

static void ParallelWorkers()
{
    std::promise<void> allStarted;
    auto startedFuture = allStarted.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<int> running{0};
    {
        ThreadPool pool(3, 8);
        for (int i = 0; i < 3; ++i) {
            assert(pool.Submit([&] {
                if (++running == 3) allStarted.set_value();
                gate.wait();
            }));
        }
        Await(startedFuture); // 若持队列锁执行任务，三个任务无法同时到达此处。
        release.set_value();
    }
    std::puts("PASS 三个工作线程同时进入任务，任务在队列锁外执行");
}

static void MultipleProducers()
{
    constexpr int taskCount = 400;
    std::array<std::atomic<int>, taskCount> hits;
    for (auto& hit : hits) hit.store(0);
    {
        ThreadPool pool(4, 512);
        std::vector<std::thread> producers;
        for (int p = 0; p < 4; ++p) {
            producers.emplace_back([&, p] {
                for (int j = 0; j < 100; ++j) {
                    const int index = p * 100 + j;
                    assert(pool.Submit([&, index] { ++hits[index]; }));
                }
            });
        }
        for (auto& producer : producers) producer.join();
        pool.Close();
    }
    for (const auto& hit : hits) assert(hit.load() == 1);
    std::puts("PASS 四个提交线程/四个工作线程，400 个任务无丢失无重复");
}

static void ConcurrentClose()
{
    std::atomic<int> accepted{0};
    std::atomic<int> executed{0};
    {
        ThreadPool pool(3, 512);
        assert(pool.Submit([&] { ++executed; }));
        ++accepted;
        std::promise<void> start;
        auto gate = start.get_future().share();
        std::vector<std::thread> producers;
        for (int i = 0; i < 3; ++i) {
            producers.emplace_back([&] {
                gate.wait();
                for (int j = 0; j < 200; ++j)
                    if (pool.Submit([&] { ++executed; })) ++accepted;
            });
        }
        std::thread closer([&] {
            gate.wait();
            pool.Close();
        });
        start.set_value();
        for (auto& producer : producers) producer.join();
        closer.join();
        assert(!pool.Submit([] {}));
    }
    assert(executed.load() == accepted.load());
    std::puts("PASS Submit 与 Close 并发，所有已接收任务执行完毕");
}

static void ExceptionIsolation()
{
    std::promise<void> survived;
    auto future = survived.get_future();
    {
        ThreadPool pool(1, 8);
        assert(pool.Submit([] { throw std::runtime_error("test exception"); }));
        assert(pool.Submit([&] { survived.set_value(); }));
        Await(future);
    }
    std::puts("PASS 一个任务抛异常后，同一工作线程仍执行后续任务");
}

static void DestructorWaits()
{
    std::promise<void> taskStarted;
    auto started = taskStarted.get_future();
    std::promise<void> leavingScope;
    auto leaving = leavingScope.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<bool> done{false};
    auto owner = std::async(std::launch::async, [&] {
        ThreadPool pool(1, 4);
        assert(pool.Submit([&] {
            taskStarted.set_value();
            gate.wait();
            done = true;
        }));
        leavingScope.set_value();
        // 现在离开作用域，必须等待仍被 gate 阻塞的任务。
    });
    Await(started);
    Await(leaving);
    assert(owner.wait_for(milliseconds(50)) == std::future_status::timeout);
    assert(!done.load());
    release.set_value();
    Await(owner);
    assert(done.load());
    std::puts("PASS 析构等待运行中的任务完成，而不是 detach 后立即返回");
}

int main()
{
    InvalidAndEmpty();
    BoundedAndClose();
    ParallelWorkers();
    MultipleProducers();
    ConcurrentClose();
    ExceptionIsolation();
    DestructorWaits();
}
