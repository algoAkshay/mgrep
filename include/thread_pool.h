#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <exception>
#include <stdexcept>

// simple thread pool - i wrote this based on the pattern from cppreference
// probably not the most optimal thing ever but it works fine for file I/O
// bound tasks which is what we care about here

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) : stopping(false) {
        if (numThreads == 0) throw std::invalid_argument("thread pool needs a worker");
        try {
            for (size_t i = 0; i < numThreads; i++) {
                workers.emplace_back([this] {
                    workerLoop();
                });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                stopping = true;
            }
            cv.notify_all();
            for (auto& t : workers) t.join();
            throw;
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stopping = true;
        }
        cv.notify_all();
        for (auto& t : workers)
            t.join();
    }

    // no copy, no move - doesnt make sense for a pool
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stopping) return; // pool is shutting down, ignore
            tasks.push(std::move(task));
            pendingTasks++;
        }
        cv.notify_one();
    }

    // blocks until all queued work is done
    void waitAll() {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [this] {
            return pendingTasks.load() == 0;
        });
        if (firstException) std::rethrow_exception(firstException);
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                cv.wait(lock, [this] {
                    return stopping || !tasks.empty();
                });

                if (stopping && tasks.empty())
                    return;

                task = std::move(tasks.front());
                tasks.pop();
            }

            try {
                task(); // run it
            } catch (...) {
                std::lock_guard<std::mutex> lock(doneMutex);
                if (!firstException) firstException = std::current_exception();
            }

            // decrement pending and wake anyone waiting in waitAll()
            if (--pendingTasks == 0) {
                std::unique_lock<std::mutex> lock(doneMutex);
                doneCv.notify_all();
            }
        }
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable cv;

    // separate mutex/cv for waitAll - avoids deadlock with the queue lock
    std::mutex doneMutex;
    std::condition_variable doneCv;

    std::atomic<size_t> pendingTasks{0};
    std::exception_ptr firstException; // guarded by doneMutex
    bool stopping;
};
