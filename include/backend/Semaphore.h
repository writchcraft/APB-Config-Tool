#pragma once
// Simple counting semaphore for C++17 (std::counting_semaphore requires C++20)
#include <mutex>
#include <condition_variable>

struct CountingSemaphore {
    explicit CountingSemaphore(int count) : _count(count) {}
    void acquire() {
        std::unique_lock<std::mutex> lk(_mu);
        _cv.wait(lk, [this]{ return _count > 0; });
        --_count;
    }
    void release() {
        std::lock_guard<std::mutex> lk(_mu);
        ++_count;
        _cv.notify_one();
    }
private:
    std::mutex _mu;
    std::condition_variable _cv;
    int _count;
};
