#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../helpers/glaze_include.hpp"

namespace rouen::hosts {

/**
 * Ephemeral Presence Record stored in the Rouen Mesh Registry
 * under key: "telegram/presence/{client_id}"
 */
struct telegram_presence_record {
    std::string client_id;
    std::string hostname;
    std::string platform{"macos"};
    std::string bot_username;
    std::string bot_first_name;
    int64_t operator_chat_id{0};
    std::string status{"active"};
    uint16_t local_api_port{8081};
    std::vector<std::string> capabilities{
        "send_notification",
        "send_message",
        "read_sessions"
    };
    uint64_t last_seen_epoch_ms{0};
    std::string last_seen_iso;

    struct glaze {
        using T = telegram_presence_record;
        static constexpr auto value = glz::object(
            "client_id", &T::client_id,
            "hostname", &T::hostname,
            "platform", &T::platform,
            "bot_username", &T::bot_username,
            "bot_first_name", &T::bot_first_name,
            "operator_chat_id", &T::operator_chat_id,
            "status", &T::status,
            "local_api_port", &T::local_api_port,
            "capabilities", &T::capabilities,
            "last_seen_epoch_ms", &T::last_seen_epoch_ms,
            "last_seen_iso", &T::last_seen_iso
        );
    };
};

/**
 * Extended options for routing notifications across mesh and telegram channels.
 */
struct notification_options {
    std::string channel{"auto"}; // "auto", "desktop", "telegram", "both"
    bool urgent{false};
    bool spoken{true};
    int64_t target_chat_id{0}; // 0 = auto-resolve from gateway presence

    struct glaze {
        using T = notification_options;
        static constexpr auto value = glz::object(
            "channel", &T::channel,
            "urgent", &T::urgent,
            "spoken", &T::spoken,
            "target_chat_id", &T::target_chat_id
        );
    };
};

} // namespace rouen::hosts

namespace rouen::services {
    using telegram_presence_record = rouen::hosts::telegram_presence_record;
    using notification_options = rouen::hosts::notification_options;
}
