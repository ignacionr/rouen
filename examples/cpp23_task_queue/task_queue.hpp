#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <expected>
#include <functional>
#include <optional>
#include <chrono>

enum class TaskError {
    Empty,
    Timeout,
    Cancelled
};

template <typename T>
class TaskQueue {
public:
    TaskQueue() = default;
    ~TaskQueue() = default;

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    void push(T task) {
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

    [[nodiscard]] std::expected<T, TaskError> pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });

        if (queue_.empty()) {
            return std::unexpected(TaskError::Empty);
        }

        T task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] std::expected<T, TaskError> pop_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return std::unexpected(TaskError::Timeout);
        }

        if (queue_.empty()) {
            return std::unexpected(TaskError::Empty);
        }

        T task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};
