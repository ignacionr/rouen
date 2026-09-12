#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../helpers/fetch.hpp"
#include "../helpers/glaze_include.hpp"
#include "../helpers/llm_config.hpp"
#include "../helpers/persona_manager.hpp"
#include "../helpers/platform_utils.hpp"
#include "../registrar.hpp"
#include "mcp_host.hpp"

namespace rouen::hosts {

struct telegram_message {
    int64_t message_id{0};
    int64_t from_id{0};
    std::string from_name;
    int64_t chat_id{0};
    std::string text;
    int64_t timestamp{0};
    bool is_outgoing{false};

    struct glaze {
        using T = telegram_message;
        static constexpr auto value = glz::object(
            "message_id", &T::message_id,
            "from_id", &T::from_id,
            "from_name", &T::from_name,
            "chat_id", &T::chat_id,
            "text", &T::text,
            "timestamp", &T::timestamp,
            "is_outgoing", &T::is_outgoing
        );
    };
};

struct telegram_chat_session {
    int64_t chat_id{0};
    int64_t user_id{0};
    std::string user_name;
    std::string last_message_text;
    int64_t last_message_time{0};
    std::vector<telegram_message> messages;

    struct glaze {
        using T = telegram_chat_session;
        static constexpr auto value = glz::object(
            "chat_id", &T::chat_id,
            "user_id", &T::user_id,
            "user_name", &T::user_name,
            "last_message_text", &T::last_message_text,
            "last_message_time", &T::last_message_time,
            "messages", &T::messages
        );
    };
};

struct telegram_route {
    std::string id;
    bool is_default{false};
    int64_t user_id{0};
    int priority{0}; // Higher priority is evaluated first
    int target_type{0}; // 0 = fixed_message, 1 = ai_persona
    std::string fixed_message;
    std::string persona_name;

    struct glaze {
        using T = telegram_route;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "is_default", &T::is_default,
            "user_id", &T::user_id,
            "priority", &T::priority,
            "target_type", &T::target_type,
            "fixed_message", &T::fixed_message,
            "persona_name", &T::persona_name
        );
    };
};

struct telegram_host_state {
    std::string bot_token;
    int64_t last_update_id{0};
    std::vector<telegram_route> routes;
    std::vector<telegram_chat_session> sessions;

    struct glaze {
        using T = telegram_host_state;
        static constexpr auto value = glz::object(
            "bot_token", &T::bot_token,
            "last_update_id", &T::last_update_id,
            "routes", &T::routes,
            "sessions", &T::sessions
        );
    };
};

class telegram_host : public std::enable_shared_from_this<telegram_host> {
public:
    enum class Status {
        Disconnected,
        Connecting,
        Active,
        Error
    };

    static std::shared_ptr<telegram_host> get_host();

    telegram_host();
    ~telegram_host();

    // Configuration
    void set_bot_token(const std::string& token);
    std::string get_bot_token() const;

    Status get_status() const { return status_.load(); }
    std::string get_status_message() const;
    std::string get_bot_username() const;

    // Sessions & Messages
    std::vector<telegram_chat_session> get_sessions() const;
    std::optional<telegram_chat_session> get_session(int64_t chat_id) const;
    bool send_manual_message(int64_t chat_id, const std::string& text);
    bool inject_incoming_message(int64_t chat_id, int64_t from_id, const std::string& from_name, const std::string& text);
    bool clear_session_messages(int64_t chat_id);

    // Routes Management
    std::vector<telegram_route> get_routes() const;
    void set_routes(const std::vector<telegram_route>& routes);
    void add_route(const telegram_route& route);
    void update_route(size_t index, const telegram_route& route);
    void delete_route(size_t index);

    // Force refresh / reconnect
    void test_connection();

private:
    void load_state();
    void save_state();

    void start_polling();
    void stop_polling();
    void poll_loop();

    void process_update(const glz::json_t& update);
    void route_incoming_message(const telegram_message& msg);
    bool send_telegram_message(int64_t chat_id, const std::string& text, telegram_message* out_msg = nullptr);
    bool validate_token_and_fetch_bot_info();

    mutable std::mutex mutex_;
    std::string bot_token_;
    std::string bot_username_;
    std::string bot_first_name_;
    int64_t last_update_id_{0};
    std::atomic<Status> status_{Status::Disconnected};
    std::string status_message_;

    std::vector<telegram_route> routes_;
    std::unordered_map<int64_t, telegram_chat_session> sessions_map_;
    std::vector<int64_t> session_order_;

    std::atomic<bool> stop_polling_{false};
    std::thread poll_thread_;
};

} // namespace rouen::hosts
