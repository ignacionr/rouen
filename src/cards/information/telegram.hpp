#pragma once

#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../hosts/telegram_host.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct telegram_message_entry {
    std::string sender;
    std::string text;
};

class telegram_card : public card {
public:
    explicit telegram_card(std::string_view uri = {});

    bool render() override;
    bool render(rouen::ui::ui_context& ui) override;
    std::string get_uri() const override { return "telegram"; }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }

    [[nodiscard]] bool is_allow_network_io() const noexcept { return allow_network_io_; }
    void set_allow_network_io(bool allow) noexcept { allow_network_io_ = allow; }

    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::string& get_last_sent_text() const noexcept { return last_sent_text_; }
    [[nodiscard]] const std::string& get_last_sent_chat_id() const noexcept { return last_sent_chat_id_; }
    [[nodiscard]] const std::string& get_last_command() const noexcept { return last_command_; }
    [[nodiscard]] bool get_refresh_triggered() const noexcept { return refresh_triggered_; }

    [[nodiscard]] const std::string& get_bot_username() const noexcept { return bot_username_; }
    void set_bot_username(std::string_view name) { bot_username_ = name; }

    [[nodiscard]] bool is_polling() const noexcept { return is_polling_; }
    void set_polling(bool polling) noexcept { is_polling_ = polling; }

    [[nodiscard]] const std::vector<telegram_message_entry>& get_message_feed() const noexcept { return message_feed_; }
    void set_message_feed(const std::vector<telegram_message_entry>& feed) { message_feed_ = feed; }

    [[nodiscard]] const std::string& get_last_chat_id() const noexcept { return last_chat_id_; }
    void set_last_chat_id(std::string_view chat_id) { last_chat_id_ = chat_id; }

private:
    void render_content();
    void render_chat_sessions_tab();
    void render_routes_tab();
    void render_bot_settings_tab();

    std::shared_ptr<rouen::hosts::telegram_host> host_;

    int selected_session_idx_{-1};
    int64_t selected_chat_id_{0};
    char manual_msg_buf_[1024]{""};

    char token_buf_[512]{""};
    bool show_token_{false};

    // Route edit temporary state
    char new_user_id_buf_[64]{""};
    char new_fixed_msg_buf_[512]{""};
    int new_route_type_{0}; // 0 = fixed_message, 1 = ai_persona
    int new_persona_idx_{0};
    int new_priority_{10};
    bool new_route_is_default_{false};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State tracking & sync for adaptive card
    bool allow_network_io_{true};
    std::string bot_username_{"RouenControlBot"};
    bool is_polling_{true};
    std::string last_chat_id_{"123456789"};
    std::vector<telegram_message_entry> message_feed_{};

    std::string last_action_{};
    std::string last_sent_text_{};
    std::string last_sent_chat_id_{};
    std::string last_command_{};
    bool refresh_triggered_{false};
};

} // namespace rouen::cards
