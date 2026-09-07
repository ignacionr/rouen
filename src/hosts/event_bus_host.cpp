#include "event_bus_host.hpp"
#include <algorithm>

namespace rouen::hosts {

bool topic_matches(std::string_view pattern, std::string_view topic) {
    if (pattern == "*" || pattern == topic) {
        return true;
    }
    if (pattern.ends_with('*')) {
        std::string_view const prefix = pattern.substr(0, pattern.length() - 1);
        return topic.starts_with(prefix);
    }
    if (pattern.starts_with('*')) {
        std::string_view const suffix = pattern.substr(1);
        return topic.ends_with(suffix);
    }
    return false;
}

event_bus_host& event_bus_host::instance() {
    static event_bus_host host;
    return host;
}

void event_bus_host::publish(events::rouen_event evt) {
    std::lock_guard<std::mutex> const lock(queue_mutex_);
    pending_events_.push_back(std::move(evt));
}

subscription_id event_bus_host::subscribe(std::string_view topic_pattern, event_callback cb) {
    if (!cb) return 0;

    subscription_id const id = next_sub_id_++;
    {
        std::unique_lock<std::shared_mutex> const lock(sub_mutex_);
        subscribers_.push_back(subscriber_entry{
            .id = id,
            .pattern = std::string(topic_pattern),
            .callback = std::move(cb)
        });
    }
    return id;
}

void event_bus_host::unsubscribe(subscription_id id) {
    if (id == 0) return;

    std::unique_lock<std::shared_mutex> const lock(sub_mutex_);
    std::erase_if(subscribers_, [id](const subscriber_entry& sub) {
        return sub.id == id;
    });
}

void event_bus_host::dispatch_pending_events() {
    std::vector<events::rouen_event> local_events;
    {
        std::lock_guard<std::mutex> const lock(queue_mutex_);
        if (pending_events_.empty()) return;
        local_events.swap(pending_events_);
    }

    for (const auto& evt : local_events) {
        std::vector<event_callback> callbacks_to_invoke;
        {
            std::shared_lock<std::shared_mutex> const lock(sub_mutex_);
            callbacks_to_invoke.reserve(subscribers_.size());
            for (const auto& sub : subscribers_) {
                if (topic_matches(sub.pattern, evt.topic)) {
                    callbacks_to_invoke.push_back(sub.callback);
                }
            }
        }

        for (const auto& cb : callbacks_to_invoke) {
            try {
                cb(evt);
            } catch (...) {
                // Safeguard against subscriber callback exceptions on the UI thread
            }
        }
    }
}

size_t event_bus_host::subscriber_count() const {
    std::shared_lock<std::shared_mutex> const lock(sub_mutex_);
    return subscribers_.size();
}

size_t event_bus_host::pending_event_count() const {
    std::lock_guard<std::mutex> const lock(queue_mutex_);
    return pending_events_.size();
}

void event_bus_host::clear() {
    {
        std::lock_guard<std::mutex> const lock(queue_mutex_);
        pending_events_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> const lock(sub_mutex_);
        subscribers_.clear();
    }
}

} // namespace rouen::hosts
