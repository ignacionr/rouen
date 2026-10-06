#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <expected>
#include <string>
#include <iostream>

enum class Priority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

enum class ErrorCode {
    Success = 0,
    Timeout,
    ExecutionFailed,
    Cancelled
};

struct Task {
    int id;
    std::string name;
    Priority priority;
    std::function<std::expected<std::string, ErrorCode>()> action;

    // Higher priority comes first in std::priority_queue
    bool operator<(const Task& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

class WinTaskRunner {
private:
    std::priority_queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::jthread worker_thread_;
    bool stop_requested_{false};
    int next_id_{1};

    void worker_loop(std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            Task task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, stoken, [this] { 
                    return stop_requested_ || !tasks_.empty(); 
                });

                if (stop_requested_ && tasks_.empty()) {
                    return;
                }

                if (tasks_.empty()) {
                    continue;
                }

                task = std::move(tasks_.top());
                tasks_.pop();
            }

            // Execute task and handle monadically
            auto result = task.action();
            result
                .and_then([&task](const std::string& val) -> std::expected<void, ErrorCode> {
                    std::cout << "[Task " << task.id << " (" << task.name << ")] Succeeded: " << val << "\n";
                    return {};
                })
                .or_else([&task](ErrorCode err) -> std::expected<void, ErrorCode> {
                    std::cerr << "[Task " << task.id << " (" << task.name << ")] Failed with error code: " << static_cast<int>(err) << "\n";
                    return std::unexpected(err);
                });
        }
    }

public:
    WinTaskRunner() {
        worker_thread_ = std::jthread([this](std::stop_token stoken) {
            worker_loop(stoken);
        });
    }

    ~WinTaskRunner() {
        {
            std::lock_guard lock(mutex_);
            stop_requested_ = true;
        }
        cv_.notify_all();
    }

    template<typename F>
    int enqueue(std::string name, Priority priority, F&& action) {
        {
            std::lock_guard lock(mutex_);
            int id = next_id_++;
            tasks_.push(Task{id, std::move(name), priority, std::forward<F>(action)});
        }
        cv_.notify_one();
        return next_id_ - 1;
    }

    void wait_all() {
        // Simple helper or rely on destructor
    }
};
