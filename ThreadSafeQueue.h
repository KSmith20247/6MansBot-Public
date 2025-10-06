#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template<typename T>
class ThreadSafeQueue {
private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable condition_;
    size_t max_size_;

public:
    explicit ThreadSafeQueue(size_t max_size = 100) : max_size_(max_size) {}

    // Non-copyable
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // Push an item to the queue (non-blocking)
    // Returns false if queue is full
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) {
            return false; // Queue is full
        }
        queue_.push(item);
        condition_.notify_one();
        return true;
    }

    // Push an item to the queue (non-blocking, move semantics)
    bool push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) {
            return false; // Queue is full
        }
        queue_.push(std::move(item));
        condition_.notify_one();
        return true;
    }

    // Pop an item from the queue (non-blocking)
    // Returns std::nullopt if queue is empty
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T result = std::move(queue_.front());
        queue_.pop();
        return result;
    }

    // Pop an item from the queue (blocking with timeout)
    // Returns std::nullopt if timeout expires
    template<typename Rep, typename Period>
    std::optional<T> waitForPop(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return std::nullopt; // Timeout
        }
        T result = std::move(queue_.front());
        queue_.pop();
        return result;
    }

    // Check if queue is empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Get current queue size
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Clear all items from queue
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        queue_.swap(empty);
    }
};