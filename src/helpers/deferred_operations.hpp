#pragma once

#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include <SDL.h>
#include "./imgui_include.hpp"

// A service that queues operations to be executed after the ImGui frame is completed
class deferred_operations {
public:
    using operation = std::function<void()>;
    
    // Add an operation to the queue
    void queue(operation op) {
        std::lock_guard<std::mutex> lock(mutex_);
        operations_.push(std::move(op));
    }
    
    // Process all queued operations
    void process_queue(SDL_Renderer* /*renderer*/) {
        std::queue<operation> ops_to_run;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::swap(ops_to_run, operations_);
        }
        
        while (!ops_to_run.empty()) {            
            // Execute the operation
            ops_to_run.front()();
            ops_to_run.pop();
        }
    }
    
    // Check if there are any operations in the queue
    bool has_operations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !operations_.empty();
    }
    
private:
    std::queue<operation> operations_;
    mutable std::mutex mutex_;
};
