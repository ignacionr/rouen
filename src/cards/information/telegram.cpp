#include "telegram.hpp"
#include "../../fonts.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/platform_utils.hpp"

#include <cctype>

namespace rouen::cards {

static std::string format_timestamp(int64_t ts) {
    if (ts <= 0) return "";
    std::time_t unix_ts = static_cast<std::time_t>(ts);
    const std::tm* tm_ptr = std::localtime(&unix_ts);
    if (!tm_ptr) return "";
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m %H:%M", tm_ptr);
    return buf;
}

static std::string strip_raw_markdown(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '*' || c == '#' || c == '`' || c == '~') continue;
        out.push_back(c);
    }
    return out;
}

telegram_card::telegram_card(std::string_view /*uri*/) {
    colors[0] = ImVec4(0.0f, 0.53f, 0.82f, 1.0f); // Telegram blue accent
    colors[1] = ImVec4(0.0f, 0.65f, 0.95f, 0.7f);
    get_color(2, ImVec4(0.18f, 0.24f, 0.32f, 1.0f)); // Left panel bg / dark bubble
    get_color(3, ImVec4(0.0f, 0.48f, 0.75f, 1.0f));  // Outgoing bubble
    get_color(4, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Subtitle text
    get_color(5, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));   // Main text

    name("Telegram Bot");
    width = 1100.0f;
    requested_fps = 10;

    host_ = rouen::hosts::telegram_host::get_host();
    if (host_) {
        std::string tok = host_->get_bot_token();
        snprintf(token_buf_, sizeof(token_buf_), "%s", tok.c_str());
    }
}

bool telegram_card::render() {
    return render_window([this]() {
        render_content();
    });
}

void telegram_card::render_content() {
    if (!host_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Telegram Host not available.");
        return;
    }

    // Top status banner
    auto status = host_->get_status();
    std::string status_msg = host_->get_status_message();
    std::string bot_user = host_->get_bot_username();

    ImVec4 status_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    if (status == rouen::hosts::telegram_host::Status::Active) {
        status_color = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
    } else if (status == rouen::hosts::telegram_host::Status::Connecting) {
        status_color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
    } else if (status == rouen::hosts::telegram_host::Status::Error) {
        status_color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    }

    ImGui::TextColored(colors[0], "Telegram Bot Host");
    ImGui::SameLine();
    ImGui::TextColored(status_color, "[%s]", status_msg.c_str());

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("TelegramTabBar", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Chat Sessions")) {
            render_chat_sessions_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Routes Configuration")) {
            render_routes_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bot Token & Settings")) {
            render_bot_settings_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void telegram_card::render_chat_sessions_tab() {
    auto sessions = host_->get_sessions();

    if (sessions.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors[4], "No chat sessions recorded yet.");
        ImGui::TextWrapped("Incoming Telegram messages will automatically appear here once your Bot Token is configured.");
        return;
    }

    // Push borderless child window styling for clean modern look
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

    // Split view: Ultra-minimal left sessions bar (~48px), Right active session detail
    float total_w = ImGui::GetContentRegionAvail().x;
    float left_w = 48.0f;
    float right_w = total_w - left_w - 12.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.08f, 0.12f, 0.6f));
    ImGui::BeginChild("SessionListChild", ImVec2(left_w, 0), false);
    ImGui::PopStyleColor();

    for (size_t i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];
        bool is_selected = (selected_chat_id_ == s.chat_id);

        std::string label = s.user_name.empty() ? ("Chat " + std::to_string(s.chat_id)) : s.user_name;

        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable("##session_sel", is_selected, 0, ImVec2(left_w, 48.0f))) {
            selected_session_idx_ = static_cast<int>(i);
            selected_chat_id_ = s.chat_id;
        }

        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        if (is_selected) {
            draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(0, 120, 215, 60), 6.0f);
            draw_list->AddRectFilled(rect_min, ImVec2(rect_min.x + 3.0f, rect_max.y), IM_COL32(0, 160, 245, 255), 2.0f);
        } else if (ImGui::IsItemHovered()) {
            draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(255, 255, 255, 15), 6.0f);
        }

        // Centered avatar circle
        ImVec2 avatar_center(rect_min.x + left_w * 0.5f, rect_min.y + 24.0f);
        ImU32 avatar_bg = is_selected ? IM_COL32(0, 136, 204, 255) : IM_COL32(40, 52, 68, 255);
        draw_list->AddCircleFilled(avatar_center, 14.0f, avatar_bg);

        // Initial letter
        char initial = label.empty() ? '?' : static_cast<char>(std::toupper(label[0]));
        char init_str[2] = { initial, '\0' };
        ImVec2 init_size = ImGui::CalcTextSize(init_str);
        draw_list->AddText(ImVec2(avatar_center.x - init_size.x * 0.5f, avatar_center.y - init_size.y * 0.5f), IM_COL32(255, 255, 255, 255), init_str);

        // Green active status dot on avatar edge
        if (is_selected) {
            draw_list->AddCircleFilled(ImVec2(avatar_center.x + 10.0f, avatar_center.y + 10.0f), 4.0f, IM_COL32(40, 200, 90, 255));
            draw_list->AddCircle(ImVec2(avatar_center.x + 10.0f, avatar_center.y + 10.0f), 4.0f, IM_COL32(10, 15, 25, 255), 0, 1.5f);
        }

        // Expanded information card on hover
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.12f, 0.18f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.00f, 0.52f, 0.85f, 0.60f));

            if (ImGui::BeginTooltip()) {
                ImGui::TextColored(colors[0], "%s", label.c_str());
                ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "Chat ID: %lld • User ID: %lld", s.chat_id, s.user_id);
                ImGui::Separator();
                ImGui::Spacing();

                std::string time_str = format_timestamp(s.last_message_time);
                if (!time_str.empty()) {
                    ImGui::TextColored(colors[4], "Last Active: %s", time_str.c_str());
                }

                std::string clean_snippet = strip_raw_markdown(s.last_message_text);
                if (!clean_snippet.empty()) {
                    if (clean_snippet.size() > 60) clean_snippet = clean_snippet.substr(0, 57) + "...";
                    ImGui::TextWrapped("\"%s\"", clean_snippet.c_str());
                }

                ImGui::Spacing();
                auto full_session_opt = host_->get_session(s.chat_id);
                size_t msg_count = full_session_opt.has_value() ? full_session_opt->messages.size() : 0;
                ImGui::TextColored(ImVec4(0.40f, 0.82f, 1.0f, 1.0f), "%zu messages recorded", msg_count);

                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right detail panel
    ImGui::BeginChild("SessionDetailChild", ImVec2(right_w, 0), false);

    auto active_session_opt = host_->get_session(selected_chat_id_);
    if (!active_session_opt.has_value() && !sessions.empty()) {
        selected_chat_id_ = sessions[0].chat_id;
        active_session_opt = host_->get_session(selected_chat_id_);
    }

    if (active_session_opt.has_value()) {
        const auto& active_session = active_session_opt.value();

        // Header info
        ImGui::TextColored(colors[0], "Chat with: %s", active_session.user_name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors[4], "(ID: %lld)", active_session.chat_id);

        ImGui::SameLine();
        float avail_w = ImGui::GetContentRegionAvail().x;
        if (avail_w > 110.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - 105.0f));
            if (ImGui::Button("Clear Context", ImVec2(100.0f, 0))) {
                host_->clear_session_messages(active_session.chat_id);
            }
        }
        ImGui::Separator();

        // Messages list child
        float msg_box_h = ImGui::GetContentRegionAvail().y - 45.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("MessagesBox", ImVec2(0, msg_box_h), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::PopStyleVar();

        std::string bot_name = host_->get_bot_username();
        const rouen::helpers::markdown_render_config md_cfg{
            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
        };

        for (size_t msg_idx = 0; msg_idx < active_session.messages.size(); ++msg_idx) {
            const auto& msg = active_session.messages[msg_idx];
            std::string time_str = format_timestamp(msg.timestamp);

            ImGui::PushID(static_cast<int>(msg_idx));

            ImVec4 card_bg = msg.is_outgoing 
                ? ImVec4(0.06f, 0.16f, 0.26f, 0.85f)   // Deep Telegram Blue for Outgoing
                : ImVec4(0.11f, 0.15f, 0.20f, 0.85f);  // Dark Slate Gray for Incoming

            ImVec4 border_col = msg.is_outgoing
                ? ImVec4(0.00f, 0.48f, 0.80f, 0.50f)
                : ImVec4(0.22f, 0.28f, 0.38f, 0.50f);

            ImVec4 header_txt_col = msg.is_outgoing
                ? ImVec4(0.40f, 0.82f, 1.0f, 1.0f)
                : ImVec4(0.95f, 0.82f, 0.45f, 1.0f);

            std::string header_txt = msg.is_outgoing 
                ? std::format("@{} • {}", bot_name.empty() ? "rouenapp_bot" : bot_name, time_str)
                : std::format("{} • {}", msg.from_name, time_str);

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, card_bg);
            ImGui::PushStyleColor(ImGuiCol_Border, border_col);

            std::string child_id = "msg_card_" + std::to_string(msg_idx);
            ImGui::BeginChild(child_id.c_str(), ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

            ImGui::TextColored(header_txt_col, "%s", header_txt.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            float content_w = ImGui::GetContentRegionAvail().x;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_w - 6.0f);
            rouen::helpers::render_markdown_block(
                msg.text,
                md_cfg,
                [](const std::string& url) {
                    rouen::platform::open_url(url);
                }
            );
            ImGui::PopTextWrapPos();

            ImGui::EndChild();

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            ImGui::PopID();
            ImGui::Spacing();
        }

        // Auto scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 50.0f) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        // Bottom manual message send box
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushItemWidth(right_w - 90.0f);
        bool send_pressed = ImGui::InputText("##manual_msg_input", manual_msg_buf_, sizeof(manual_msg_buf_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.53f, 0.82f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.63f, 0.95f, 1.0f));
        if (ImGui::Button("Send", ImVec2(75.0f, 0)) || send_pressed) {
            if (manual_msg_buf_[0] != '\0') {
                host_->send_manual_message(active_session.chat_id, manual_msg_buf_);
                manual_msg_buf_[0] = '\0';
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    } else {
        ImGui::TextColored(colors[4], "Select a chat session from the list on the left.");
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void telegram_card::render_routes_tab() {
    ImGui::TextColored(colors[0], "Message Routing Rules");
    ImGui::TextWrapped("Route incoming messages by User ID or use the Default fallback. Routes are evaluated by Priority (highest first). Match targets can send a fixed response or delegate to an AI Persona.");
    ImGui::Separator();
    ImGui::Spacing();

    auto routes = host_->get_routes();
    const auto& personas = rouen::helpers::PersonaManager::instance().get_personas();

    // Table of routes
    if (ImGui::BeginTable("RoutesTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Match Filter", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("User ID / Condition", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Action Target", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Response Config / Persona", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableHeadersRow();

        bool routes_changed = false;
        size_t route_to_delete = static_cast<size_t>(-1);

        for (size_t i = 0; i < routes.size(); ++i) {
            auto& r = routes[i];
            ImGui::TableNextRow();

            ImGui::PushID(static_cast<int>(i));

            // Col 0: Priority
            ImGui::TableSetColumnIndex(0);
            int prio = r.priority;
            ImGui::PushItemWidth(65.0f);
            if (ImGui::InputInt("##prio", &prio, 0)) {
                r.priority = prio;
                routes_changed = true;
            }
            ImGui::PopItemWidth();

            // Col 1: Match Filter
            ImGui::TableSetColumnIndex(1);
            if (r.is_default) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Default Fallback");
            } else {
                ImGui::Text("User ID Filter");
            }

            // Col 2: User ID
            ImGui::TableSetColumnIndex(2);
            if (r.is_default) {
                ImGui::TextColored(colors[4], "(All unmatched)");
            } else {
                int64_t uid = r.user_id;
                if (ImGui::InputScalar("##uid", ImGuiDataType_S64, &uid)) {
                    r.user_id = uid;
                    routes_changed = true;
                }
            }

            // Col 3: Action Target
            ImGui::TableSetColumnIndex(3);
            const char* target_items[] = { "Fixed Message", "AI Persona" };
            int current_target = r.target_type;
            if (ImGui::Combo("##target", &current_target, target_items, 2)) {
                r.target_type = current_target;
                routes_changed = true;
            }

            // Col 4: Response Config / Persona
            ImGui::TableSetColumnIndex(4);
            if (r.target_type == 0) { // Fixed Message
                char msg_buf[256];
                snprintf(msg_buf, sizeof(msg_buf), "%s", r.fixed_message.c_str());
                if (ImGui::InputText("##fixed_text", msg_buf, sizeof(msg_buf))) {
                    r.fixed_message = msg_buf;
                    routes_changed = true;
                }
            } else { // AI Persona
                std::string current_p = r.persona_name.empty() ? (personas.empty() ? "None" : personas[0].name) : r.persona_name;
                if (ImGui::BeginCombo("##persona_combo", current_p.c_str())) {
                    for (const auto& p : personas) {
                        bool selected = (p.name == current_p);
                        if (ImGui::Selectable(p.name.c_str(), selected)) {
                            r.persona_name = p.name;
                            routes_changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            // Col 5: Actions
            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button("Delete")) {
                route_to_delete = i;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (route_to_delete != static_cast<size_t>(-1)) {
            host_->delete_route(route_to_delete);
        } else if (routes_changed) {
            host_->set_routes(routes);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(colors[0], "Add New Route Rule");

    ImGui::PushItemWidth(100.0f);
    ImGui::InputInt("Priority", &new_priority_);
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::Checkbox("Default Fallback", &new_route_is_default_);
    if (!new_route_is_default_) {
        ImGui::SameLine();
        ImGui::PushItemWidth(180.0f);
        ImGui::InputText("User ID", new_user_id_buf_, sizeof(new_user_id_buf_));
        ImGui::PopItemWidth();
    }

    const char* target_items[] = { "Fixed Message", "AI Persona" };
    ImGui::Combo("Target Action", &new_route_type_, target_items, 2);

    if (new_route_type_ == 0) {
        ImGui::InputText("Fixed Message Text", new_fixed_msg_buf_, sizeof(new_fixed_msg_buf_));
    } else {
        std::string current_p = (new_persona_idx_ >= 0 && static_cast<size_t>(new_persona_idx_) < personas.size())
                                    ? personas[static_cast<size_t>(new_persona_idx_)].name
                                    : (personas.empty() ? "None" : personas[0].name);

        if (ImGui::BeginCombo("Select AI Persona", current_p.c_str())) {
            for (size_t idx = 0; idx < personas.size(); ++idx) {
                bool selected = (static_cast<size_t>(new_persona_idx_) == idx);
                if (ImGui::Selectable(personas[idx].name.c_str(), selected)) {
                    new_persona_idx_ = static_cast<int>(idx);
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::Button("Add Route Rule")) {
        rouen::hosts::telegram_route nr;
        nr.id = "route_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        nr.priority = new_priority_;
        nr.is_default = new_route_is_default_;
        nr.user_id = new_route_is_default_ ? 0 : std::atoll(new_user_id_buf_);
        nr.target_type = new_route_type_;
        nr.fixed_message = new_fixed_msg_buf_;
        if (!personas.empty() && new_persona_idx_ >= 0 && static_cast<size_t>(new_persona_idx_) < personas.size()) {
            nr.persona_name = personas[static_cast<size_t>(new_persona_idx_)].name;
        } else {
            nr.persona_name = "Rouen Assistant";
        }
        host_->add_route(nr);

        // Reset inputs
        new_user_id_buf_[0] = '\0';
        new_fixed_msg_buf_[0] = '\0';
    }
}

void telegram_card::render_bot_settings_tab() {
    ImGui::TextColored(colors[0], "Telegram Bot API Configuration");
    ImGui::TextWrapped("Enter your Telegram Bot Token obtained from BotFather on Telegram (@BotFather).");
    ImGui::Separator();
    ImGui::Spacing();

    // Bot status preview
    auto status = host_->get_status();
    std::string status_msg = host_->get_status_message();
    std::string bot_username = host_->get_bot_username();

    ImGui::Text("Connection Status: ");
    ImGui::SameLine();
    if (status == rouen::hosts::telegram_host::Status::Active) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Active - Long Polling (@%s)", bot_username.c_str());
    } else if (status == rouen::hosts::telegram_host::Status::Connecting) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Connecting...");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Disconnected / Error: %s", status_msg.c_str());
    }

    ImGui::Spacing();
    ImGui::PushItemWidth(500.0f);
    ImGuiInputTextFlags flags = show_token_ ? 0 : ImGuiInputTextFlags_Password;
    ImGui::InputText("Bot Token", token_buf_, sizeof(token_buf_), flags);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Checkbox("Show Token", &show_token_);

    ImGui::Spacing();
    if (ImGui::Button("Save Token & Connect", ImVec2(180.0f, 32.0f))) {
        host_->set_bot_token(token_buf_);
    }

    ImGui::SameLine();
    if (ImGui::Button("Test Connection", ImVec2(140.0f, 32.0f))) {
        host_->test_connection();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(colors[4], "Instructions:");
    ImGui::BulletText("1. Open Telegram and search for @BotFather.");
    ImGui::BulletText("2. Create a new bot using /newbot or use an existing bot.");
    ImGui::BulletText("3. Copy the HTTP API token provided by BotFather into the input above.");
    ImGui::BulletText("4. Click 'Save Token & Connect'. Rouen will start long-polling updates.");
}

} // namespace rouen::cards
