#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t threadCount, size_t maxQueued);

    ~ThreadPool();

    /**
     *  true: Successful receipt, but does not mean that execution has been completed.
     *  false: The task is null, the queue is full, or the thread pool has been shut down.
     */
    bool Submit(Task task);

    void Close();

private:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator= (const ThreadPool&) = delete;

private:
    void WorkerLoop();
    void JoinWorks();

    const size_t maxQueued_;

    std::mutex mtx_;
    std::condition_variable ready_;
    std::queue<Task> tasks_;

    bool closing_ = false;

    std::vector<std::thread> workers_;
};
