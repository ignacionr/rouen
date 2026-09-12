#pragma once

#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include "../../hosts/telegram_host.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class telegram_card : public card {
public:
    explicit telegram_card(std::string_view uri = {});

    bool render() override;
    std::string get_uri() const override { return "telegram"; }

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
};

} // namespace rouen::cards
