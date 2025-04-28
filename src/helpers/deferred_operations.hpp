#pragma once

#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

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
        std::lock_guard<std::mutex> lock(mutex_);
        
        while (!operations_.empty()) {            
            // Execute the operation
            operations_.front()();
            operations_.pop();
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