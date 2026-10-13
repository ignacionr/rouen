#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct envvars_card : public card {
    envvars_card() {
        // Set custom colors for the card
        colors[0] = {0.2f, 0.4f, 0.6f, 1.0f};   // Blue primary color (first_color)
        colors[1] = {0.3f, 0.5f, 0.7f, 0.7f};   // Light blue secondary color (second_color)
        
        // Additional colors for specific elements with higher contrast
        get_color(2, {0.0f, 0.8f, 1.0f, 1.0f}); // Bright cyan for variable name
        get_color(3, {0.2f, 0.2f, 0.3f, 0.7f}); // Darker table header background for contrast
        get_color(4, {0.2f, 0.3f, 0.4f, 0.5f}); // Darker alternating row color
        get_color(5, {1.0f, 1.0f, 1.0f, 0.95f}); // Nearly white for value text
        
        name("Environment Variables");
        width = 600.0f; // Set initial width
        
        // Request refresh rate for updating variables
        requested_fps = 2;  // Update twice per second
        
        // Load environment variables
        refresh_env_vars();
    }
    
    // Explicit destructor
    ~envvars_card() override {
        env_vars.clear();
    }
    
    // Get all environment variables
    void refresh_env_vars() {
        // Use centralized configuration service for consistent environment variable access
        auto config_service = helpers::ConfigService::instance();
        if (config_service) {
            env_vars = config_service->get_all_env_vars();
        }
        
        // Update the sorted keys list for display
        sorted_keys.clear();
        for (const auto& [key, _] : env_vars) {
            sorted_keys.push_back(key);
        }
        
        // Sort keys alphabetically
        std::sort(sorted_keys.begin(), sorted_keys.end());
    }
    
    std::string get_uri() const override {
        return "envvars";
    }
    
    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

            ImGui::Text("Environment Variables (%zu):", env_vars.size());
            
            // Filter input
            char filter_buffer[256] = "";
            std::strncpy(filter_buffer, search_filter_.c_str(), sizeof(filter_buffer) - 1);
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##filter", filter_buffer, static_cast<int>(sizeof(filter_buffer)))) {
                search_filter_ = filter_buffer;
            }
            
            // Show placeholder text when filter is empty
            if (filter_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Filter variables..."
                );
            }
            ImGui::PopItemWidth();
            
            ImGui::Separator();
            
            // Display variables in a scrollable area with a table
            if (ImGui::BeginChild("EnvVarsScroll", ImVec2(0, 0), true)) {
                // Filter text conversion for case insensitivity
                std::string filter_text = search_filter_;
                std::transform(filter_text.begin(), filter_text.end(), filter_text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                
                // Setup for table
                static ImGuiTableFlags table_flags = 
                    ImGuiTableFlags_Borders | 
                    ImGuiTableFlags_RowBg | 
                    ImGuiTableFlags_ScrollY | 
                    ImGuiTableFlags_SizingFixedFit;

                if (ImGui::BeginTable("EnvVarsTable", 2, table_flags)) {
                    // Set up table headers
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    
                    // Table headers with background color
                    ImGui::TableHeadersRow();
                    
                    // Filter and display variables in table rows
                    int row_idx = 0;
                    for (const auto& key : sorted_keys) {
                        bool sensitive = is_sensitive_key(key);
                        std::string display_value = sensitive ? "*** (hidden)" : env_vars[key];

                        // Apply filter if set
                        if (!filter_text.empty()) {
                            std::string lower_key = key;
                            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                                          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            
                            std::string lower_value = display_value;
                            std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(),
                                          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            
                            // Skip if neither key nor value contains filter text
                            if (lower_key.find(filter_text) == std::string::npos && 
                                lower_value.find(filter_text) == std::string::npos) {
                                continue;
                            }
                        }
                        
                        // Add a row to the table
                        ImGui::TableNextRow();
                        
                        // Variable name (first column)
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(
                            ImVec4(colors[2].x, colors[2].y, colors[2].z, colors[2].w),
                            "%s", key.c_str()
                        );
                        
                        // Variable value (second column)
                        ImGui::TableSetColumnIndex(1);
                        if (sensitive) {
                            ImGui::TextColored(
                                ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                "%s", display_value.c_str()
                            );
                        } else {
                            ImGui::TextColored(
                                ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                                "%s", display_value.c_str()
                            );
                        }
                        
                        row_idx++;
                    }
                    
                    ImGui::EndTable();
                    
                    // If no variables match the filter, show a message
                    if (row_idx == 0 && filter_text.length() > 0) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No variables match your filter");
                    }
                }
            }
            ImGui::EndChild();
        });
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Emoji gear icon
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "⚙️";
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 2: Title and Subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = "Environment Variables Inspector";
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "Active process runtime environment";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 3: Variables count badge
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = std::format("{} Variables", env_vars.size());
                tb["weight"] = "Bolder";
                tb["size"] = "Small";
                items.push_back(std::move(tb));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header_container));
        }

        // 2. FactSet for environment variables
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            std::string filter_lower = search_filter_;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            for (const auto& key : sorted_keys) {
                auto it = env_vars.find(key);
                if (it == env_vars.end()) continue;
                const auto& val = it->second;
                bool const sensitive = is_sensitive_key(key);
                std::string const display_value = sensitive ? "*** (hidden)" : val;

                if (!filter_lower.empty()) {
                    std::string k_lower = key;
                    std::transform(k_lower.begin(), k_lower.end(), k_lower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    std::string v_lower = display_value;
                    std::transform(v_lower.begin(), v_lower.end(), v_lower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (k_lower.find(filter_lower) == std::string::npos &&
                        v_lower.find(filter_lower) == std::string::npos) {
                        continue;
                    }
                }

                facts.push_back(glz::json_t::object_t{
                    {"title", key + ":"},
                    {"value", display_value}
                });
            }

            if (facts.empty()) {
                facts.push_back(glz::json_t::object_t{
                    {"title", "Variables:"},
                    {"value", "No variables match your filter"}
                });
            }

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Input Controls Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            input_container["separator"] = true;

            std::vector<glz::json_t> items;

            glz::json_t tb_hdr;
            tb_hdr["type"] = "TextBlock";
            tb_hdr["text"] = "Set or Override Variable";
            tb_hdr["weight"] = "Bolder";
            items.push_back(std::move(tb_hdr));

            glz::json_t input_key;
            input_key["type"] = "Input.Text";
            input_key["id"] = "env_key";
            input_key["placeholder"] = "Variable Name (e.g. GEMINI_API_KEY)";
            items.push_back(std::move(input_key));

            glz::json_t input_val;
            input_val["type"] = "Input.Text";
            input_val["id"] = "env_val";
            input_val["placeholder"] = "Value...";
            items.push_back(std::move(input_val));

            input_container["items"] = std::move(items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_set;
            act_set["type"] = "Action.Execute";
            act_set["title"] = "Set Variable";
            act_set["verb"] = "env_set";
            actions.push_back(std::move(act_set));

            glz::json_t act_export;
            act_export["type"] = "Action.Execute";
            act_export["title"] = "Export .env";
            act_export["verb"] = "env_export";
            actions.push_back(std::move(act_export));
        }
        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;
        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "env_set") {
                std::string key;
                std::string val;

                if (data.is_object()) {
                    if (data.contains("env_key") && data["env_key"].holds<std::string>()) {
                        key = data["env_key"].get<std::string>();
                    } else if (data.contains("key") && data["key"].holds<std::string>()) {
                        key = data["key"].get<std::string>();
                    }

                    if (data.contains("env_val")) {
                        if (data["env_val"].holds<std::string>()) {
                            val = data["env_val"].get<std::string>();
                        } else if (data["env_val"].holds<double>()) {
                            val = std::to_string(static_cast<int64_t>(data["env_val"].get<double>()));
                        } else if (data["env_val"].holds<bool>()) {
                            val = data["env_val"].get<bool>() ? "true" : "false";
                        }
                    } else if (data.contains("value")) {
                        if (data["value"].holds<std::string>()) {
                            val = data["value"].get<std::string>();
                        } else if (data["value"].holds<double>()) {
                            val = std::to_string(static_cast<int64_t>(data["value"].get<double>()));
                        } else if (data["value"].holds<bool>()) {
                            val = data["value"].get<bool>() ? "true" : "false";
                        }
                    }
                }

                if (!key.empty()) {
                    last_set_key_ = key;
                    last_set_value_ = val;
                    last_action_ = "env_set";

                    auto config_svc = helpers::ConfigService::instance();
                    if (config_svc) {
                        config_svc->set_env_value(key, val, false);
                    }
#ifdef _WIN32
                    _putenv_s(key.c_str(), val.c_str());
#else
                    ::setenv(key.c_str(), val.c_str(), 1);
#endif
                    refresh_env_vars();
                    last_adaptive_parse_time_ = {};
                }
            } else if (verb == "env_remove") {
                std::string key;
                if (data.is_object()) {
                    if (data.contains("env_key") && data["env_key"].holds<std::string>()) {
                        key = data["env_key"].get<std::string>();
                    } else if (data.contains("key") && data["key"].holds<std::string>()) {
                        key = data["key"].get<std::string>();
                    }
                }

                if (!key.empty()) {
                    last_removed_key_ = key;
                    last_action_ = "env_remove";

#ifdef _WIN32
                    _putenv_s(key.c_str(), "");
#else
                    ::unsetenv(key.c_str());
#endif
                    env_vars.erase(key);
                    refresh_env_vars();
                    last_adaptive_parse_time_ = {};
                }
            } else if (verb == "env_export") {
                last_action_ = "env_export";
                auto config_svc = helpers::ConfigService::instance();
                if (config_svc) {
                    last_export_success_ = config_svc->export_to_env_file();
                }
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                last_action_ = "toggle_adaptive_view";
                adaptive_view_mode = !adaptive_view_mode;
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[envvars] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }

    [[nodiscard]] std::string get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] std::string get_last_set_key() const noexcept { return last_set_key_; }
    [[nodiscard]] std::string get_last_set_value() const noexcept { return last_set_value_; }
    [[nodiscard]] std::string get_last_removed_key() const noexcept { return last_removed_key_; }
    [[nodiscard]] bool get_last_export_success() const noexcept { return last_export_success_; }
    [[nodiscard]] size_t get_total_count() const noexcept { return env_vars.size(); }
    [[nodiscard]] const std::map<std::string, std::string>& get_env_vars() const noexcept { return env_vars; }
    [[nodiscard]] std::string get_env_var(const std::string& key) const {
        auto it = env_vars.find(key);
        return it != env_vars.end() ? it->second : std::string{};
    }
    [[nodiscard]] bool has_env_var(const std::string& key) const {
        return env_vars.find(key) != env_vars.end();
    }
    [[nodiscard]] std::string get_search_filter() const { return search_filter_; }
    void set_search_filter(std::string filter) { search_filter_ = std::move(filter); }

    static bool is_sensitive_key(const std::string& key) {
        std::string upper_key = key;
        std::transform(upper_key.begin(), upper_key.end(), upper_key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return upper_key.find("PASS") != std::string::npos ||
               upper_key.find("PWD") != std::string::npos ||
               upper_key.find("TOKEN") != std::string::npos ||
               upper_key.find("KEY") != std::string::npos;
    }

private:
    std::map<std::string, std::string> env_vars;
    std::vector<std::string> sorted_keys;
    std::string search_filter_{};

    std::string last_action_{};
    std::string last_set_key_{};
    std::string last_set_value_{};
    std::string last_removed_key_{};
    bool last_export_success_{false};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
};

} // namespace rouen::cards
