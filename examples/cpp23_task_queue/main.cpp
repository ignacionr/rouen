#include <iostream>
#include <coroutine>
#include <queue>
#include <vector>
#include <optional>
#include <expected>
#include <functional>
#include <variant>
#include <concepts>
#include <thread>
#include <chrono>

namespace task_queue {

// Monadic error types
enum class TaskError {
    Cancelled,
    Timeout,
    ExecutionFailed,
    QueueClosed
};

template<typename T>
using Result = std::expected<T, TaskError>;

// Priority levels
enum class Priority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

struct TaskItem {
    int id;
    Priority priority;
    std::function<void()> callback;

    // Higher priority comes first
    bool operator<(const TaskItem& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

class PriorityTaskQueue {
private:
    std::priority_queue<TaskItem> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
    int next_id_ = 1;

public:
    PriorityTaskQueue() = default;
    ~PriorityTaskQueue() {
        close();
    }

    Result<int> push(Priority p, std::function<void()> cb) {
        std::unique_lock lock(mutex_);
        if (closed_) {
            return std::unexpected(TaskError::QueueClosed);
        }
        int id = next_id_++;
        queue_.push(TaskItem{id, p, std::move(cb)});
        cv_.notify_one();
        return id;
    }

    Result<TaskItem> pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
        
        if (queue_.empty() && closed_) {
            return std::unexpected(TaskError::QueueClosed);
        }

        auto item = std::move(const_cast<TaskItem&>(queue_.top()));
        queue_.pop();
        return item;
    }

    void close() {
        std::unique_lock lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }
};

} // namespace task_queue

int main() {
    using namespace task_queue;
    std::cout << "[C++23 Task Queue] Initializing priority queue with monadic error handling...\n";

    PriorityTaskQueue queue;

    // Push tasks with varying priorities
    auto r1 = queue.push(Priority::Normal, [] { std::cout << "Executing Normal task #1\n"; });
    auto r2 = queue.push(Priority::Critical, [] { std::cout << "Executing CRITICAL task #2\n"; });
    auto r3 = queue.push(Priority::Low, [] { std::cout << "Executing Low priority task #3\n"; });

    if (r1 && r2 && r3) {
        std::cout << "Successfully queued tasks with IDs: " << *r1 << ", " << *r2 << ", " << *r3 << "\n";
    }

    queue.close();

    // Consume tasks
    while (auto task_res = queue.pop()) {
        auto& task = *task_res;
        std::cout << "Popped task ID " << task.id << " with priority " << static_cast<int>(task.priority) << "\n";
        task.callback();
    }

    std::cout << "[C++23 Task Queue] Completed successfully.\n";
    return 0;
}
