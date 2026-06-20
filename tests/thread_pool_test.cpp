#include "thread_pool.h"

#include <atomic>
#include <iostream>
#include <stdexcept>

int main() {
    bool rejectedZero = false;
    try {
        ThreadPool pool(0);
    } catch (const std::invalid_argument&) {
        rejectedZero = true;
    }
    if (!rejectedZero) {
        std::cerr << "zero-worker pool was accepted\n";
        return 1;
    }

    std::atomic<int> completed{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i)
            pool.enqueue([&completed] { ++completed; });
        // Destructor must drain work even without an explicit waitAll().
    }
    if (completed != 100) {
        std::cerr << "destructor did not drain queued work\n";
        return 1;
    }

    bool caughtTaskError = false;
    {
        ThreadPool pool(2);
        pool.enqueue([] { throw std::runtime_error("task failure"); });
        pool.enqueue([&completed] { ++completed; });
        try {
            pool.waitAll();
        } catch (const std::runtime_error&) {
            caughtTaskError = true;
        }
    }
    if (!caughtTaskError || completed != 101) {
        std::cerr << "task failure was not propagated after draining\n";
        return 1;
    }

    std::cout << "thread pool tests passed\n";
}
