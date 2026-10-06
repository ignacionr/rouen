#include "task_runner.hpp"
#include <iostream>
#include <chrono>
#include <format>
#include <thread>

int main() {
    std::cout << "=== Starting C++23 Windows Task Runner Demo ===\n";

    {
        WinTaskRunner runner;

        // Enqueue tasks with name, priority, and lambda
        auto t1 = runner.enqueue("Normal Task", Priority::Normal, []() -> std::expected<std::string, ErrorCode> {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return "Normal priority background job done";
        });

        auto t2 = runner.enqueue("Critical Task", Priority::Critical, []() -> std::expected<std::string, ErrorCode> {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return "Critical system alert dispatched";
        });

        auto t3 = runner.enqueue("Low Task", Priority::Low, []() -> std::expected<std::string, ErrorCode> {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return "Low priority telemetry cleanup finished";
        });

        auto t4 = runner.enqueue("High Task", Priority::High, []() -> std::expected<std::string, ErrorCode> {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            return "High priority database sync completed";
        });

        if (t1 && t2 && t3 && t4) {
            std::cout << std::format("Successfully enqueued tasks IDs: {}, {}, {}, {}\n", t1, t2, t3, t4);
        } else {
            std::cout << "Failed to enqueue one or more tasks.\n";
        }

        // Allow worker thread to process queue
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    std::cout << "=== C++23 Task Runner Demo Completed Successfully ===\n";
    return 0;
}
