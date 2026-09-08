#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../helpers/glaze_include.hpp"


namespace rouen::events {

/**
 * Event Data Structure representing an asynchronous message on the Rouen Event Bus.
 */
struct rouen_event {
    std::string topic;                                    // Hierarchical topic string (e.g. "card:pomodoro:finished")
    std::string source_id;                                // URI or identifier of sender
    glz::json_t payload{};                                // Glaze JSON payload object
    std::chrono::system_clock::time_point timestamp{      // Event creation timestamp
        std::chrono::system_clock::now()
    };
};

} // namespace rouen::events

namespace rouen::hosts {

using subscription_id = uint64_t;
using event_callback = std::function<void(const events::rouen_event&)>;

/**
 * Evaluates whether a topic matches a given pattern (supporting standard prefix/suffix wildcards like "*", "host:*", "card:pomodoro:*").
 */
bool topic_matches(std::string_view pattern, std::string_view topic);

/**
 * Stateful Host managing application-wide thread-safe Event Bus pub/sub delivery.
 */
class event_bus_host {
public:
    static event_bus_host& instance();

    event_bus_host(const event_bus_host&) = delete;
    event_bus_host& operator=(const event_bus_host&) = delete;

    /**
     * Publish an event (thread-safe, non-blocking). Can be safely called from background worker threads.
     */
    void publish(events::rouen_event evt);

    /**
     * Subscribe to a topic pattern (supports exact topics or wildcards like "card:pomodoro:*", "host:*", "*").
     */
    subscription_id subscribe(std::string_view topic_pattern, event_callback cb);

    /**
     * Unsubscribe listener by subscription ID.
     */
    void unsubscribe(subscription_id id);

    /**
     * Called during deck::render() on the main UI thread to dispatch queued background events.
     */
    void dispatch_pending_events();

    /**
     * Returns number of current active subscribers.
     */
    [[nodiscard]] size_t subscriber_count() const;

    /**
     * Returns number of events waiting in the thread-safe queue.
     */
    [[nodiscard]] size_t pending_event_count() const;

    /**
     * Clears all pending events and active subscribers (primarily for cleanup or testing).
     */
    void clear();

private:
    event_bus_host() = default;
    ~event_bus_host() = default;

    mutable std::mutex queue_mutex_;
    std::vector<events::rouen_event> pending_events_;

    mutable std::shared_mutex sub_mutex_;
    struct subscriber_entry {
        subscription_id id;
        std::string pattern;
        event_callback callback;
    };
    std::vector<subscriber_entry> subscribers_;
    std::atomic<subscription_id> next_sub_id_{1};
};

/**
 * RAII helper class ensuring automatic unsubscription upon destruction.
 */
class scoped_event_subscription {
public:
    scoped_event_subscription() = default;
    explicit scoped_event_subscription(subscription_id id) noexcept : id_(id) {}
    
    ~scoped_event_subscription() {
        reset();
    }

    scoped_event_subscription(const scoped_event_subscription&) = delete;
    scoped_event_subscription& operator=(const scoped_event_subscription&) = delete;

    scoped_event_subscription(scoped_event_subscription&& other) noexcept 
        : id_(std::exchange(other.id_, 0)) {}

    scoped_event_subscription& operator=(scoped_event_subscription&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    void reset() noexcept {
        if (id_ != 0) {
            event_bus_host::instance().unsubscribe(id_);
            id_ = 0;
        }
    }

    [[nodiscard]] subscription_id get() const noexcept { return id_; }
    explicit operator bool() const noexcept { return id_ != 0; }

private:
    subscription_id id_{0};
};

} // namespace rouen::hosts
