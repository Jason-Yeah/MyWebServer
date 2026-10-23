#include "threadpool.h"

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(size_t threadCount, size_t maxQueued): maxQueued_(maxQueued)
{
    if (threadCount <= 0 || maxQueued <= 0)
        throw std::invalid_argument("threadCount and maxQueued must be positive");

    workers_.reserve(threadCount); // capacity
    try
    {
        // Create threads
        for (size_t i = 0; i < threadCount; ++i)
            workers_.emplace_back([this] {
                WorkerLoop();
            });
    }
    catch (...)
    {
        Close();
        JoinWorks();
        throw;
    }
}

ThreadPool::~ThreadPool()
{
    Close();
    JoinWorks();
}

bool ThreadPool::Submit(Task task)
{
    if (!task) return false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (closing_ || tasks_.size() >= maxQueued_) return false;

        tasks_.push(std::move(task));
    }
    ready_.notify_one();
    return true;
}

void ThreadPool::Close()
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        closing_ = true;
    }

    ready_.notify_all();
}

void ThreadPool::JoinWorks()
{
    for (auto& worker: workers_)
        if (worker.joinable()) worker.join();
}

void ThreadPool::WorkerLoop()
{
    while (true)
    {
        // std::function<void()>;
        Task task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            ready_.wait(lock, [this] {
                // wait condition: return false;
                // not closing and no task -> wait
                return closing_ || !tasks_.empty();
            });

            if (closing_ && tasks_.empty()) return;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try
        {
            task();
        }
        catch (...)
        {
            std::fputs("Threadpool task threw an exception\n", stderr);
        }
    }
}
