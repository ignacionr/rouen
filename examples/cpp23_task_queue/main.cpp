#include "task_queue.hpp"
#include <print>
#include <thread>
#include <chrono>
#include <string>

struct Task {
    int id;
    std::string description;

    void execute() const {
        std::println("[Task {}] Executing: {}", id, description);
    }
};

int main() {
    std::println("=== Modern C++23 Task Queue Demo ===");

    TaskQueue<Task> queue;

    // Producer thread
    std::jthread producer([&queue] {
        for (int i = 1; i <= 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Task t{i, std::format("Data processing job #{}", i)};
            std::println("[Producer] Pushing task {}: {}", t.id, t.description);
            queue.push(t);
        }
    });

    // Consumer thread using std::expected monadic operations
    std::jthread consumer([&queue] {
        for (int i = 0; i < 3; ++i) {
            auto result = queue.pop_for(std::chrono::seconds(2));
            
            // Demonstrate C++23 std::expected monadic .and_then() / .transform() or simple check
            result
                .and_then([](Task task) -> std::expected<Task, TaskError> {
                    std::println("[Consumer] Successfully popped task ID {}", task.id);
                    return task;
                })
                .transform([](Task task) {
                    task.execute();
                    return task;
                })
                .or_else([](TaskError err) {
                    std::println("[Consumer] Failed to pop task with error code: {}", static_cast<int>(err));
                    return std::expected<Task, TaskError>{std::unexpected(err)};
                });
        }
    });

    producer.join();
    consumer.join();

    std::println("=== Task Queue Demo Completed Successfully ===");
    return 0;
}
