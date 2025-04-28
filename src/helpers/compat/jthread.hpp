// filepath: /Users/ignaciorodriguez/src/rouen/src/helpers/compat/jthread.hpp
#pragma once

// This file provides compatibility polyfills for C++20 features 
// that may not be available on all platforms (especially macOS)

#include <thread>
#include <atomic>
#include <functional>
#include <memory>

namespace std {
    // If we don't have std::jthread, provide a compatible implementation
    #if !defined(__cpp_lib_jthread) || (__cpp_lib_jthread < 201911L)
    
    // Simple implementation of stop_token
    class stop_token {
    private:
        std::shared_ptr<std::atomic<bool>> stop_requested_;
        
    public:
        stop_token() : stop_requested_(std::make_shared<std::atomic<bool>>(false)) {}
        
        explicit stop_token(std::shared_ptr<std::atomic<bool>> state) 
            : stop_requested_(std::move(state)) {}
        
        bool stop_requested() const noexcept {
            return stop_requested_ && stop_requested_->load();
        }
        
        bool stop_possible() const noexcept {
            return static_cast<bool>(stop_requested_);
        }
    };
    
    // Simple implementation of stop_source
    class stop_source {
    private:
        std::shared_ptr<std::atomic<bool>> stop_requested_;
        
    public:
        stop_source() : stop_requested_(std::make_shared<std::atomic<bool>>(false)) {}
        
        explicit stop_source(std::nullptr_t) noexcept : stop_requested_() {}
        
        stop_source(const stop_source&) = default;
        stop_source& operator=(const stop_source&) = default;
        stop_source(stop_source&&) = default;
        stop_source& operator=(stop_source&&) = default;
        
        ~stop_source() = default;
        
        bool request_stop() noexcept {
            if (!stop_requested_) {
                return false;
            }
            
            bool expected = false;
            return stop_requested_->compare_exchange_strong(expected, true);
        }
        
        stop_token get_token() const noexcept {
            return stop_token(stop_requested_);
        }
        
        bool stop_requested() const noexcept {
            return stop_requested_ && stop_requested_->load();
        }
        
        bool stop_possible() const noexcept {
            return static_cast<bool>(stop_requested_);
        }
    };
    
    // Implementation of jthread which automatically joins on destruction
    class jthread {
    private:
        std::thread thread_;
        stop_source source_;
        
    public:
        jthread() noexcept = default;
        
        template<typename Callable, typename... Args>
        explicit jthread(Callable&& func, Args&&... args)
            : source_() {
            auto token = source_.get_token();
            
            // Check if the first parameter is a stop_token
            if constexpr (std::is_invocable_v<Callable, stop_token, Args...>) {
                thread_ = std::thread(std::forward<Callable>(func), token, std::forward<Args>(args)...);
            } else {
                // If not, just call the function with the args
                thread_ = std::thread(std::forward<Callable>(func), std::forward<Args>(args)...);
            }
        }
        
        ~jthread() {
            if (joinable()) {
                request_stop();
                join();
            }
        }
        
        jthread(const jthread&) = delete;
        jthread& operator=(const jthread&) = delete;
        
        jthread(jthread&& other) noexcept
            : thread_(std::move(other.thread_)), source_(std::move(other.source_)) {}
        
        jthread& operator=(jthread&& other) noexcept {
            if (joinable()) {
                request_stop();
                join();
            }
            thread_ = std::move(other.thread_);
            source_ = std::move(other.source_);
            return *this;
        }
        
        void swap(jthread& other) noexcept {
            thread_.swap(other.thread_);
            std::swap(source_, other.source_);
        }
        
        [[nodiscard]] bool joinable() const noexcept {
            return thread_.joinable();
        }
        
        void join() {
            thread_.join();
        }
        
        void detach() {
            thread_.detach();
        }
        
        [[nodiscard]] std::thread::id get_id() const noexcept {
            return thread_.get_id();
        }
        
        [[nodiscard]] stop_source get_stop_source() noexcept {
            return source_;
        }
        
        [[nodiscard]] stop_token get_stop_token() const noexcept {
            return source_.get_token();
        }
        
        bool request_stop() noexcept {
            return source_.request_stop();
        }
        
        // Forward native_handle for platform-specific operations
        [[nodiscard]] std::thread::native_handle_type native_handle() {
            return thread_.native_handle();
        }
    };
    
    #endif // !defined(__cpp_lib_jthread)
} // namespace std