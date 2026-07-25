#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../registrar.hpp"
#include "debug.hpp"
#include "config_service.hpp"

struct notify_service {
    struct notification_entry {
        std::uint64_t id;
        std::string timestamp;
        std::string preview;
        std::string message;
        std::chrono::system_clock::time_point created_at;
    };

    static constexpr std::size_t max_history = 200;

    notify_service() {
        registrar::add<std::function<void(std::string const&)>>("notify", 
            std::make_shared<std::function<void(std::string const&)>>(
                [](std::string const &message) {
                    record_notification(message);
                    if (spoken_notifications_enabled()) {
                        speak_notification(message);
                    }
                    NOTIFY_INFO(message);
                }
            )
        );
    }

    static std::vector<notification_entry> history_snapshot() {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return {history_.rbegin(), history_.rend()};
    }

    static bool spoken_notifications_enabled() {
        return CONFIG_SERVICE()->get_typed<bool>("ROUEN_SPOKEN_NOTIFICATIONS").value_or(true);
    }

    static bool set_spoken_notifications_enabled(bool enabled) {
        return CONFIG_SERVICE()->set_env_value("ROUEN_SPOKEN_NOTIFICATIONS", enabled ? "1" : "0", true);
    }

    static void speak_notification(const std::string& message) {
        std::thread([message]() {
            std::string safe_message;
            safe_message.reserve(message.size());

            for (char c : message) {
                if (c == '"') {
                    safe_message += "\\\"";
                } else if (c == '\\') {
                    safe_message += "\\\\";
                } else if (c == '`' || c == '$' || c == '(' || c == ')' || c == ';' || c == '&' || c == '|' || c == '\n' || c == '\r') {
                    safe_message += ' ';
                } else {
                    safe_message += c;
                }
            }

            std::string say_path = CONFIG_SERVICE()->get_say_path();
            [[maybe_unused]] int system_result = system(std::format("\"{}\" \"{}\"", say_path, safe_message).c_str());
        }).detach();
    }

private:
    inline static std::atomic<std::uint64_t> next_id_ {1};
    inline static std::mutex history_mutex_;
    inline static std::deque<notification_entry> history_;

    static void record_notification(const std::string& message) {
        notification_entry entry{
            .id = next_id_.fetch_add(1, std::memory_order_relaxed),
            .timestamp = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now()),
            .preview = make_preview(message),
            .message = message,
            .created_at = std::chrono::system_clock::now()
        };

        std::lock_guard<std::mutex> lock(history_mutex_);
        history_.push_back(std::move(entry));
        while (history_.size() > max_history) {
            history_.pop_front();
        }
    }

    static std::string make_preview(const std::string& message) {
        std::string first_line = message;
        if (auto newline_pos = first_line.find_first_of("\r\n"); newline_pos != std::string::npos) {
            first_line.resize(newline_pos);
        }

        auto first_non_space = std::find_if_not(first_line.begin(), first_line.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        first_line.erase(first_line.begin(), first_non_space);

        constexpr std::size_t max_preview_length = 80;
        if (first_line.size() > max_preview_length) {
            first_line.resize(max_preview_length - 3);
            first_line += "...";
        }

        return first_line.empty() ? std::string("(empty notification)") : first_line;
    }
};
