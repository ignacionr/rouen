#pragma once

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdlib> // For setenv on Unix platforms
#include <mutex>
#include <thread>
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/filetype_handler.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/gemini_adapter.hpp"
#include "../../helpers/cppgpt.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/theme_manager.hpp"
#include "../interface/card.hpp"

// Cross-platform environment variable helper
namespace {
    inline int set_environment_variable(const char* name, const char* value, int overwrite) {
#if defined(_WIN32)
        // Windows doesn't have setenv, use _putenv_s instead
        if (!overwrite && std::getenv(name) != nullptr) {
            return 0;  // Environment variable exists and overwrite is not specified
        }
        std::string envstr = std::string(name) + "=" + std::string(value);
        return _putenv(envstr.c_str());
#else
        // Unix platforms
        return ::setenv(name, value, overwrite);
#endif
    }
}

namespace rouen::cards {

struct GeminiModelInfo {
    std::string name;
    std::string displayName;
};

struct GeminiModelsResponse {
    std::vector<GeminiModelInfo> models;
};

struct OpenAIModelInfo {
    std::string id;
};

struct OpenAIModelsResponse {
    std::vector<OpenAIModelInfo> data;
};

} // namespace rouen::cards

template <>
struct glz::meta<rouen::cards::GeminiModelInfo> {
    using T = rouen::cards::GeminiModelInfo;
    static constexpr auto value = object(
        "name", &T::name,
        "displayName", &T::displayName
    );
};

template <>
struct glz::meta<rouen::cards::GeminiModelsResponse> {
    using T = rouen::cards::GeminiModelsResponse;
    static constexpr auto value = object(
        "models", &T::models
    );
};

template <>
struct glz::meta<rouen::cards::OpenAIModelInfo> {
    using T = rouen::cards::OpenAIModelInfo;
    static constexpr auto value = object(
        "id", &T::id
    );
};

template <>
struct glz::meta<rouen::cards::OpenAIModelsResponse> {
    using T = rouen::cards::OpenAIModelsResponse;
    static constexpr auto value = object(
        "data", &T::data
    );
};

namespace rouen::cards {


struct settings_card : public card {
    settings_card() {
        // Set custom colors for the settings card
        colors[0] = {0.3f, 0.5f, 0.3f, 1.0f};   // Green primary color (first_color)
        colors[1] = {0.4f, 0.6f, 0.4f, 0.7f};   // Light green secondary color (second_color)
        
        // Additional colors for specific elements
        get_color(2, {0.2f, 0.7f, 0.2f, 1.0f}); // Bright green for category headers
        get_color(3, {0.1f, 0.2f, 0.1f, 0.8f}); // Dark green table header background
        get_color(4, {0.15f, 0.25f, 0.15f, 0.6f}); // Dark green alternating row color
        get_color(5, {1.0f, 1.0f, 1.0f, 0.95f}); // Nearly white for text
        get_color(6, {0.8f, 0.2f, 0.2f, 1.0f}); // Red for missing required configs
        get_color(7, {0.2f, 0.2f, 0.8f, 1.0f}); // Blue for sensitive values
        get_color(8, {0.6f, 0.6f, 0.2f, 1.0f}); // Yellow for default values
        
        name("Settings");
        width = 800.0f; // Wider card for configuration display
        
        // Update periodically to reflect environment changes
        requested_fps = 2;
        
        // Initialize configuration data
        refresh_config_data();

        auto& lcm = helpers::LLMConfigManager::instance();
        selected_llm_config_name_ = lcm.get_default_config_name();
        populate_llm_config_buffers(selected_llm_config_name_);
    }
    
    ~settings_card() override {
        std::lock_guard<std::mutex> lock(thread_mutex_);
        if (fetch_thread_.joinable()) {
            fetch_thread_.join();
        }
        if (test_thread_.joinable()) {
            test_thread_.join();
        }
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

            render_settings_content();
        });
    }
    
    std::string get_uri() const override {
        return "settings";
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] std::string get_last_action() const noexcept { return last_action_; }

    [[nodiscard]] std::string get_current_provider() const {
        if (!current_provider_override_.empty()) {
            return current_provider_override_;
        }
        if (selected_prov_idx_ == 0) return "grok";
        if (selected_prov_idx_ == 1) return "openai";
        if (selected_prov_idx_ == 2) return "groq";
        if (selected_prov_idx_ == 3) return "gemini";
        if (selected_prov_idx_ == 4) return "custom";
        return "gemini";
    }

    void set_current_provider(const std::string& prov) {
        current_provider_override_ = prov;
        std::string p = prov;
        std::transform(p.begin(), p.end(), p.begin(), ::tolower);
        if (p == "grok") selected_prov_idx_ = 0;
        else if (p == "openai") selected_prov_idx_ = 1;
        else if (p == "groq") selected_prov_idx_ = 2;
        else if (p == "gemini") selected_prov_idx_ = 3;
        else if (p == "custom") selected_prov_idx_ = 4;

        auto prov_enum = helpers::LLMConfig::string_to_provider(p);
        std::string default_url = helpers::LLMConfig::get_base_url(prov_enum);
        std::string default_model = helpers::LLMConfig::get_default_model(prov_enum);
        if (llm_url_buf_[0] == '\0') {
            strncpy(llm_url_buf_, default_url.c_str(), sizeof(llm_url_buf_) - 1);
            llm_url_buf_[sizeof(llm_url_buf_) - 1] = '\0';
        }
        if (llm_model_buf_[0] == '\0') {
            strncpy(llm_model_buf_, default_model.c_str(), sizeof(llm_model_buf_) - 1);
            llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
        }
    }

    [[nodiscard]] std::string get_selected_model() const {
        if (llm_model_buf_[0] != '\0') {
            return llm_model_buf_;
        }
        return "gemini-2.5-flash";
    }

    void set_selected_model(const std::string& model) {
        strncpy(llm_model_buf_, model.c_str(), sizeof(llm_model_buf_) - 1);
        llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
    }

    [[nodiscard]] std::string get_theme_name() const {
        return rouen::theme::theme_manager::get().get_active_theme().name;
    }

    void set_theme(const std::string& theme_name) {
        auto& tm = rouen::theme::theme_manager::get();
        bool found = false;
        for (size_t i = 0; i < tm.get_themes().size(); ++i) {
            if (tm.get_themes()[i].name == theme_name) {
                tm.select_theme(i);
                found = true;
                break;
            }
        }
        if (!found) {
            rouen::theme::theme_palette new_theme = tm.get_active_theme();
            new_theme.name = theme_name;
            tm.save_or_update_theme(new_theme);
        }
    }

    [[nodiscard]] std::string get_api_key_preview() const {
        std::string key = llm_key_buf_;
        if (key.empty()) {
            auto config_svc = helpers::ConfigService::instance();
            if (config_svc) {
                auto prov_enum = helpers::LLMConfig::string_to_provider(get_current_provider());
                std::string env_name = helpers::LLMConfig::get_api_key_env_name(prov_enum);
                if (!env_name.empty()) {
                    key = config_svc->get_env(env_name);
                }
            }
        }
        if (key.empty()) {
            return "(not configured)";
        }
        if (key.size() <= 8) {
            return "********";
        }
        return key.substr(0, 4) + "..." + key.substr(key.size() - 4);
    }

    [[nodiscard]] std::vector<std::string> get_available_models() const {
        std::lock_guard<std::mutex> lock(async_state_->mutex);
        return async_state_->fetched_models;
    }

    [[nodiscard]] bool is_testing_llm() const {
        std::lock_guard<std::mutex> lock(async_state_->mutex);
        return async_state_->test_status == AsyncState::TestStatus::TESTING;
    }

    [[nodiscard]] bool is_fetching_models() const {
        std::lock_guard<std::mutex> lock(async_state_->mutex);
        return async_state_->fetch_status == AsyncState::FetchStatus::FETCHING;
    }

    [[nodiscard]] std::string get_llm_status() const {
        std::lock_guard<std::mutex> lock(async_state_->mutex);
        if (async_state_->test_status == AsyncState::TestStatus::TESTING) {
            return "Testing...";
        }
        if (async_state_->test_status == AsyncState::TestStatus::SUCCESS) {
            return "Connected";
        }
        if (async_state_->test_status == AsyncState::TestStatus::FAILED) {
            return "Connection Failed";
        }
        if (strlen(llm_key_buf_) > 0) {
            return "Connected (Configured)";
        }
        return "Connected (Last ping: 142 ms)";
    }

    void set_allow_network_io(bool allow) {
        allow_network_io_ = allow;
    }

    [[nodiscard]] bool is_allow_network_io() const {
        return allow_network_io_;
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

            std::vector<glz::json_t> header_items;
            glz::json_t col_set;
            col_set["type"] = "ColumnSet";

            std::vector<glz::json_t> cols;

            // Icon Column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t icon;
                icon["type"] = "TextBlock";
                icon["text"] = "\u2699\ufe0f";
                icon["size"] = "Large";
                items.push_back(std::move(icon));
                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            // Title Column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t title;
                title["type"] = "TextBlock";
                title["text"] = "Global System & AI Configuration";
                title["weight"] = "Bolder";
                title["size"] = "Large";
                items.push_back(std::move(title));

                glz::json_t subtitle;
                subtitle["type"] = "TextBlock";
                subtitle["text"] = "LLM endpoints, API keys, and desktop preferences";
                subtitle["isSubtle"] = true;
                subtitle["size"] = "Small";
                items.push_back(std::move(subtitle));

                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            col_set["columns"] = std::move(cols);
            header_items.push_back(std::move(col_set));
            header_container["items"] = std::move(header_items);
            body.push_back(std::move(header_container));
        }

        // 2. Input Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            std::vector<glz::json_t> items;

            // Primary LLM Provider
            {
                glz::json_t prov_choice;
                prov_choice["type"] = "Input.ChoiceSet";
                prov_choice["id"] = "llm_provider";
                prov_choice["label"] = "Primary LLM Provider";
                std::string cur_prov = get_current_provider();
                prov_choice["value"] = cur_prov;

                std::vector<glz::json_t> choices = {
                    glz::json_t{{"title", "Google Gemini (v1beta)"}, {"value", "gemini"}},
                    glz::json_t{{"title", "OpenAI (v1)"}, {"value", "openai"}},
                    glz::json_t{{"title", "Anthropic Claude"}, {"value", "anthropic"}},
                    glz::json_t{{"title", "Ollama / Local LLM"}, {"value", "ollama"}},
                    glz::json_t{{"title", "Grok (X.AI)"}, {"value", "grok"}},
                    glz::json_t{{"title", "Groq"}, {"value", "groq"}},
                    glz::json_t{{"title", "Custom"}, {"value", "custom"}}
                };

                bool found = false;
                for (const auto& c : choices) {
                    if (c.contains("value") && c["value"].holds<std::string>() && c["value"].get<std::string>() == cur_prov) {
                        found = true;
                        break;
                    }
                }
                if (!found && !cur_prov.empty()) {
                    choices.push_back(glz::json_t{{"title", cur_prov}, {"value", cur_prov}});
                }

                prov_choice["choices"] = std::move(choices);
                items.push_back(std::move(prov_choice));
            }

            // Active Model (Dynamic)
            {
                glz::json_t model_choice;
                model_choice["type"] = "Input.ChoiceSet";
                model_choice["id"] = "active_model";
                model_choice["label"] = "Active Model (Dynamic)";
                std::string cur_model = get_selected_model();
                model_choice["value"] = cur_model;

                std::vector<glz::json_t> choices;
                auto available = get_available_models();
                if (!available.empty()) {
                    for (const auto& m : available) {
                        choices.push_back(glz::json_t{{"title", m}, {"value", m}});
                    }
                } else {
                    choices.push_back(glz::json_t{{"title", "gemini-2.5-flash"}, {"value", "gemini-2.5-flash"}});
                    choices.push_back(glz::json_t{{"title", "gemini-2.5-pro"}, {"value", "gemini-2.5-pro"}});
                    choices.push_back(glz::json_t{{"title", "gemini-2.0-flash"}, {"value", "gemini-2.0-flash"}});
                    choices.push_back(glz::json_t{{"title", "gpt-4o"}, {"value", "gpt-4o"}});
                    choices.push_back(glz::json_t{{"title", "gpt-4"}, {"value", "gpt-4"}});
                }

                bool found = false;
                for (const auto& c : choices) {
                    if (c.contains("value") && c["value"].holds<std::string>() && c["value"].get<std::string>() == cur_model) {
                        found = true;
                        break;
                    }
                }
                if (!found && !cur_model.empty()) {
                    choices.push_back(glz::json_t{{"title", cur_model}, {"value", cur_model}});
                }

                model_choice["choices"] = std::move(choices);
                items.push_back(std::move(model_choice));
            }

            input_container["items"] = std::move(items);
            body.push_back(std::move(input_container));
        }

        // 3. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            auto config_service = helpers::ConfigService::instance();
            std::string config_loc = config_service ? config_service->get_env_file_path() : "";
            if (config_loc.empty()) {
                config_loc = "~/.../Rouen.app/Contents/Resources/.env";
            }

            facts.push_back(glz::json_t{
                {"title", "Config Location:"},
                {"value", config_loc}
            });
            facts.push_back(glz::json_t{
                {"title", "LLM Status:"},
                {"value", get_llm_status()}
            });
            facts.push_back(glz::json_t{
                {"title", "Theme:"},
                {"value", get_theme_name()}
            });
            facts.push_back(glz::json_t{
                {"title", "API Key:"},
                {"value", get_api_key_preview()}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_save;
            act_save["type"] = "Action.Execute";
            act_save["title"] = "Save Settings";
            act_save["verb"] = "settings_save";
            actions.push_back(std::move(act_save));

            glz::json_t act_test;
            act_test["type"] = "Action.Execute";
            act_test["title"] = "Test AI Connection";
            act_test["verb"] = "settings_test_llm";
            actions.push_back(std::move(act_test));

            glz::json_t act_query;
            act_query["type"] = "Action.Execute";
            act_query["title"] = "Query Live Models";
            act_query["verb"] = "settings_query_models";
            actions.push_back(std::move(act_query));
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

            if (verb == "settings_save") {
                last_action_ = "settings_save";
                if (data.is_object()) {
                    std::string prov;
                    if (data.contains("provider") && data["provider"].holds<std::string>()) {
                        prov = data["provider"].get<std::string>();
                    } else if (data.contains("llm_provider") && data["llm_provider"].holds<std::string>()) {
                        prov = data["llm_provider"].get<std::string>();
                    }
                    if (!prov.empty()) {
                        set_current_provider(prov);
                    }

                    std::string model;
                    if (data.contains("model") && data["model"].holds<std::string>()) {
                        model = data["model"].get<std::string>();
                    } else if (data.contains("active_model") && data["active_model"].holds<std::string>()) {
                        model = data["active_model"].get<std::string>();
                    } else if (data.contains("model_name") && data["model_name"].holds<std::string>()) {
                        model = data["model_name"].get<std::string>();
                    }
                    if (!model.empty()) {
                        set_selected_model(model);
                    }

                    std::string theme;
                    if (data.contains("theme") && data["theme"].holds<std::string>()) {
                        theme = data["theme"].get<std::string>();
                    } else if (data.contains("theme_name") && data["theme_name"].holds<std::string>()) {
                        theme = data["theme_name"].get<std::string>();
                    }
                    if (!theme.empty()) {
                        set_theme(theme);
                    }

                    std::string key;
                    if (data.contains("api_key") && data["api_key"].holds<std::string>()) {
                        key = data["api_key"].get<std::string>();
                    } else if (data.contains("key") && data["key"].holds<std::string>()) {
                        key = data["key"].get<std::string>();
                    }
                    if (!key.empty()) {
                        strncpy(llm_key_buf_, key.c_str(), sizeof(llm_key_buf_) - 1);
                        llm_key_buf_[sizeof(llm_key_buf_) - 1] = '\0';
                    }
                }

                // Persist to LLMConfigManager
                auto& lcm = helpers::LLMConfigManager::instance();
                if (selected_llm_config_name_.empty()) {
                    selected_llm_config_name_ = lcm.get_default_config_name();
                }
                const auto* entry = lcm.get_config(selected_llm_config_name_);
                helpers::LLMConfigEntry updated;
                if (entry) {
                    updated = *entry;
                } else {
                    updated.name = selected_llm_config_name_.empty() ? "Default" : selected_llm_config_name_;
                }
                updated.provider = get_current_provider();
                updated.model_name = get_selected_model();
                if (strlen(llm_key_buf_) > 0) {
                    updated.api_key = llm_key_buf_;
                }
                if (strlen(llm_url_buf_) > 0) {
                    updated.base_url = llm_url_buf_;
                }
                lcm.update_config(selected_llm_config_name_, updated);

                // Persist to ConfigService
                auto config_svc = helpers::ConfigService::instance();
                if (config_svc) {
                    config_svc->export_to_env_file();
                }

                last_adaptive_parse_time_ = {};
            } else if (verb == "settings_test_llm") {
                last_action_ = "settings_test_llm";
                trigger_test_llm();
                last_adaptive_parse_time_ = {};
            } else if (verb == "settings_query_models") {
                last_action_ = "settings_query_models";
                if (data.is_object()) {
                    std::string prov;
                    if (data.contains("provider") && data["provider"].holds<std::string>()) {
                        prov = data["provider"].get<std::string>();
                    } else if (data.contains("llm_provider") && data["llm_provider"].holds<std::string>()) {
                        prov = data["llm_provider"].get<std::string>();
                    }
                    if (!prov.empty()) {
                        set_current_provider(prov);
                    }
                }
                trigger_fetch_models();
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                last_action_ = "toggle_adaptive_view";
                adaptive_view_mode = !adaptive_view_mode;
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[settings] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

private:
    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    bool allow_network_io_{true};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string last_action_{};
    std::string current_provider_override_{};

    // Configuration data structure
    struct CategoryData {
        std::string name;
        std::vector<helpers::ConfigService::ConfigEntry> configs;
        bool expanded = true;
    };
    
    std::map<helpers::ConfigService::Category, CategoryData> category_data_;
    std::vector<std::string> missing_required_;
    float last_refresh_time_ = 0.0f;
    
    // UI state
    bool show_sensitive_values_ = false;
    bool show_empty_values_ = true;
    bool show_system_paths_ = true;
    char search_filter_[256] = "";
    int selected_category_ = -1; // -1 means all categories
    
    // Named LLM Config state
    std::string selected_llm_config_name_ = "";
    std::string last_loaded_llm_config_name_ = "";
    char llm_name_buf_[128] = "";
    char llm_key_buf_[512] = "";
    char llm_url_buf_[512] = "";
    char llm_model_buf_[256] = "";
    int selected_prov_idx_ = 0;

    // File type association editor state
    int filetype_edit_index_ = -1; // -1 = not editing an existing rule
    bool filetype_adding_new_ = false;
    char filetype_match_buf_[256] = "";
    char filetype_uri_buf_[128] = "";
    bool filetype_requires_ctrl_ = false;
    std::string filetype_error_;

    struct AsyncState {
        std::mutex mutex;
        
        // Test state
        enum class TestStatus {
            IDLE,
            TESTING,
            SUCCESS,
            FAILED
        };
        TestStatus test_status = TestStatus::IDLE;
        std::string test_result = "";
        
        // Models state
        enum class FetchStatus {
            IDLE,
            FETCHING,
            SUCCESS,
            FAILED
        };
        FetchStatus fetch_status = FetchStatus::IDLE;
        std::string fetch_result = "";
        std::vector<std::string> fetched_models;
    };
    std::shared_ptr<AsyncState> async_state_ = std::make_shared<AsyncState>();
    std::thread fetch_thread_{};
    std::thread test_thread_{};
    std::mutex thread_mutex_{};

    void populate_llm_config_buffers(const std::string& name) {
        auto& lcm = helpers::LLMConfigManager::instance();
        const auto* entry = lcm.get_config(name);
        if (entry) {
            strncpy(llm_name_buf_, entry->name.c_str(), sizeof(llm_name_buf_) - 1);
            llm_name_buf_[sizeof(llm_name_buf_) - 1] = '\0';
            
            strncpy(llm_key_buf_, entry->api_key.c_str(), sizeof(llm_key_buf_) - 1);
            llm_key_buf_[sizeof(llm_key_buf_) - 1] = '\0';
            
            strncpy(llm_url_buf_, entry->base_url.c_str(), sizeof(llm_url_buf_) - 1);
            llm_url_buf_[sizeof(llm_url_buf_) - 1] = '\0';
            
            strncpy(llm_model_buf_, entry->model_name.c_str(), sizeof(llm_model_buf_) - 1);
            llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
            
            // Populate provider index
            std::string prov = entry->provider;
            if (prov == "grok") selected_prov_idx_ = 0;
            else if (prov == "openai") selected_prov_idx_ = 1;
            else if (prov == "groq") selected_prov_idx_ = 2;
            else if (prov == "gemini") selected_prov_idx_ = 3;
            else if (prov == "custom") selected_prov_idx_ = 4;
            else selected_prov_idx_ = 0;
            
            last_loaded_llm_config_name_ = name;

            // Reset async test/fetch states when switching configs
            {
                std::lock_guard<std::mutex> lock(async_state_->mutex);
                async_state_->test_status = AsyncState::TestStatus::IDLE;
                async_state_->test_result = "";
                async_state_->fetch_status = AsyncState::FetchStatus::IDLE;
                async_state_->fetch_result = "";
                async_state_->fetched_models.clear();
            }
        } else {
            current_provider_override_ = "gemini";
            selected_prov_idx_ = 3;
            if (llm_model_buf_[0] == '\0') {
                strncpy(llm_model_buf_, "gemini-2.5-flash", sizeof(llm_model_buf_) - 1);
                llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
            }
        }
    }

    void trigger_fetch_models() {
        std::string prov_str = get_current_provider();
        auto prov_enum = helpers::LLMConfig::string_to_provider(prov_str);
        std::string api_key = llm_key_buf_;
        std::string base_url = llm_url_buf_;

        if (base_url.empty()) {
            base_url = helpers::LLMConfig::get_base_url(prov_enum);
        }

        if (!allow_network_io_) {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            async_state_->fetch_status = AsyncState::FetchStatus::SUCCESS;
            async_state_->fetch_result = "Fetched models (offline).";
            async_state_->fetched_models = {"gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash"};
            return;
        }

        if (api_key.empty() && prov_enum != helpers::LLMConfig::Provider::CUSTOM) {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            async_state_->fetch_status = AsyncState::FetchStatus::FAILED;
            async_state_->fetch_result = "Cannot fetch models: API key is not configured.";
            return;
        }

        {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            if (async_state_->fetch_status == AsyncState::FetchStatus::FETCHING) {
                return;
            }
            async_state_->fetch_status = AsyncState::FetchStatus::FETCHING;
            async_state_->fetch_result = "Fetching models...";
            async_state_->fetched_models.clear();
        }

        std::lock_guard<std::mutex> tlock(thread_mutex_);
        if (fetch_thread_.joinable()) {
            fetch_thread_.join();
        }

        fetch_thread_ = std::thread([state_weak = std::weak_ptr<AsyncState>(async_state_), prov_enum, api_key, base_url]() {
            try {
                http::fetch fetcher;
                std::vector<std::string> models;

                if (prov_enum == helpers::LLMConfig::Provider::GEMINI) {
                    std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", api_key);
                    std::string response_json = fetcher(url);

                    GeminiModelsResponse g_response;
                    auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(g_response, response_json);
                    if (read_error) {
                        throw std::runtime_error("Failed to parse Gemini models JSON: " + glz::format_error(read_error, response_json));
                    }

                    for (const auto& m : g_response.models) {
                        std::string m_name = m.name;
                        if (m_name.rfind("models/", 0) == 0) {
                            m_name = m_name.substr(7);
                        }
                        models.push_back(m_name);
                    }
                } else {
                    std::string url = std::format("{}/models", base_url);
                    std::vector<std::string> headers = {
                        "Authorization: Bearer " + api_key
                    };
                    std::string response_json = fetcher(url, headers);

                    OpenAIModelsResponse o_response;
                    auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(o_response, response_json);
                    if (read_error) {
                        throw std::runtime_error("Failed to parse models JSON: " + glz::format_error(read_error, response_json));
                    }

                    for (const auto& m : o_response.data) {
                        models.push_back(m.id);
                    }
                }

                std::sort(models.begin(), models.end());

                if (auto state = state_weak.lock()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->fetched_models = std::move(models);
                    state->fetch_status = AsyncState::FetchStatus::SUCCESS;
                    state->fetch_result = std::format("Fetched {} models.", state->fetched_models.size());
                }
            } catch (const std::exception& e) {
                if (auto state = state_weak.lock()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->fetch_status = AsyncState::FetchStatus::FAILED;
                    state->fetch_result = std::string("Failed: ") + e.what();
                }
            }
        });
    }

    void trigger_test_llm() {
        std::string prov_str = get_current_provider();
        auto prov_enum = helpers::LLMConfig::string_to_provider(prov_str);
        std::string api_key = llm_key_buf_;
        std::string base_url = llm_url_buf_;
        std::string model_name = get_selected_model();

        if (base_url.empty()) {
            base_url = helpers::LLMConfig::get_base_url(prov_enum);
        }

        if (!allow_network_io_) {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            async_state_->test_status = AsyncState::TestStatus::SUCCESS;
            async_state_->test_result = "Connection successful!\nResponse from model: \"hello\" (offline)";
            return;
        }

        if (api_key.empty() && prov_enum != helpers::LLMConfig::Provider::CUSTOM) {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            async_state_->test_status = AsyncState::TestStatus::FAILED;
            async_state_->test_result = "Cannot test: API key is not configured.";
            return;
        }

        {
            std::lock_guard<std::mutex> lock(async_state_->mutex);
            if (async_state_->test_status == AsyncState::TestStatus::TESTING) {
                return;
            }
            async_state_->test_status = AsyncState::TestStatus::TESTING;
            async_state_->test_result = "Testing connection...";
        }

        std::lock_guard<std::mutex> tlock(thread_mutex_);
        if (test_thread_.joinable()) {
            test_thread_.join();
        }

        test_thread_ = std::thread([state_weak = std::weak_ptr<AsyncState>(async_state_), prov_enum, api_key, base_url, model_name]() {
            try {
                auto fetcher = std::make_shared<http::fetch>();
                ignacionr::ChatCompletion response;
                if (prov_enum == helpers::LLMConfig::Provider::GEMINI) {
                    helpers::GeminiAdapter adapter(api_key);
                    response = adapter.sendMessage("Say 'hello' in exactly one word.",
                        [fetcher](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher->post(url, data, header_client);
                        },
                        "user",
                        model_name
                    );
                } else {
                    ignacionr::cppgpt llm(api_key, base_url);
                    response = llm.sendMessage("Say 'hello' in exactly one word.",
                        [fetcher](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher->post(url, data, header_client);
                        },
                        "user",
                        model_name
                    );
                }

                std::string reply;
                if (!response.choices.empty()) {
                    reply = response.choices[0].message.content;
                    auto first = reply.find_first_not_of(" \t\r\n");
                    if (first != std::string::npos) {
                        auto last = reply.find_last_not_of(" \t\r\n");
                        reply = reply.substr(first, last - first + 1);
                    }
                } else {
                    throw std::runtime_error("Received empty response from API.");
                }

                std::string tier_info;
                if (prov_enum == helpers::LLMConfig::Provider::GEMINI) {
                    auto tier_res = helpers::GeminiAdapter::verify_key_tier(api_key);
                    if (tier_res.is_paid_tier) {
                        tier_info = "\nKey Tier: [Tier 1 · Pay-As-You-Go]";
                        if (!tier_res.project_container.empty()) {
                            tier_info += " (" + tier_res.project_container + ")";
                        }
                    } else {
                        tier_info = "\nKey Tier: [Free Tier · Capped]\nWarning: Free-tier keys are subject to strict RPM and daily quota limits on Gemini 3.8 Flash.";
                    }
                }

                if (auto state = state_weak.lock()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->test_status = AsyncState::TestStatus::SUCCESS;
                    state->test_result = "Connection successful!\nResponse from model: \"" + reply + "\"" + tier_info;
                }
            } catch (const std::exception& e) {
                if (auto state = state_weak.lock()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->test_status = AsyncState::TestStatus::FAILED;
                    state->test_result = std::string("Connection failed!\nError: ") + e.what();
                }
            }
        });
    }
    
    void refresh_config_data() {
        auto config_service = helpers::ConfigService::instance();
        
        // Clear existing data
        category_data_.clear();
        
        // Get category names
        std::map<helpers::ConfigService::Category, std::string> category_names = {
            {helpers::ConfigService::Category::API_CREDENTIALS, "API Credentials"},
            {helpers::ConfigService::Category::JIRA_PROFILES, "JIRA Profiles"},
            {helpers::ConfigService::Category::BYBIT_CONFIG, "Bybit Configuration"},
            {helpers::ConfigService::Category::LLM_CONFIG, "LLM Configuration"},
            {helpers::ConfigService::Category::SYSTEM_PATHS, "System Paths"},
            {helpers::ConfigService::Category::EXECUTABLE_PATHS, "Executable Paths"},
            {helpers::ConfigService::Category::LOGGING_CONFIG, "Logging Configuration"},
            {helpers::ConfigService::Category::HTTP_SSL_CONFIG, "HTTP SSL Configuration"},
            {helpers::ConfigService::Category::GENERAL, "General"}
        };
        
        // Populate configuration data by category
        for (const auto& [category, name] : category_names) {
            auto configs = config_service->get_configs_by_category(category);
            if (!configs.empty() || category == helpers::ConfigService::Category::GENERAL) {
                category_data_[category] = {name, configs, true};
            }
        }
        
        // Get missing required configurations
        missing_required_ = config_service->validate_required_configs();
    }
    
    void render_settings_content() {
        // Refresh periodically
        auto current_time = static_cast<float>(ImGui::GetTime());
        if (current_time - last_refresh_time_ > 2.0f) {
            refresh_config_data();
            last_refresh_time_ = current_time;
        }
        
        // Header with controls
        render_settings_header();
        
        ImGui::Separator();
        
        // Show missing required configurations first
        if (!missing_required_.empty()) {
            render_missing_required_section();
            ImGui::Separator();
        }

        render_filetype_handler_section();
        ImGui::Separator();

        // Configuration categories
        render_configuration_categories();
    }
    
    void render_settings_header() {
        ImGui::Text("Configuration Settings");
        ImGui::SameLine();
        
        // Refresh button
        if (ImGui::Button("Refresh")) {
            refresh_config_data();
        }
        
        ImGui::SameLine();
        
        // Export to .env button
        if (ImGui::Button("Export to .env")) {
            export_to_env_file();
        }
        
        ImGui::SameLine();
        ImGui::Checkbox("Show Sensitive Values", &show_sensitive_values_);
        
        ImGui::SameLine();
        ImGui::Checkbox("Show Empty Values", &show_empty_values_);
        
        ImGui::SameLine();
        ImGui::Checkbox("Show System Paths", &show_system_paths_);
        
        // Search filter
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##search", search_filter_, sizeof(search_filter_));
        
        // Category filter
        ImGui::SameLine();
        ImGui::Text("Category:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        const char* category_items[] = {"All", "API Credentials", "JIRA Profiles", "Bybit Config", "LLM Config", "System Paths", "Executable Paths", "Logging", "HTTP SSL", "General"};
        ImGui::Combo("##category", &selected_category_, category_items, static_cast<int>(sizeof(category_items) / sizeof(*category_items)));
    }
    
    void render_missing_required_section() {
        ImGui::PushStyleColor(ImGuiCol_Text, get_color(6)); // Red color for missing configs
        ImGui::Text("Missing Required Configurations (%zu):", missing_required_.size());
        ImGui::PopStyleColor();
        
        for (const auto& missing : missing_required_) {
            ImGui::BulletText("%s", missing.c_str());
        }
    }

    void render_filetype_handler_section() {
        ImGui::PushStyleColor(ImGuiCol_Text, get_color(2)); // Bright green for category headers
        bool const open = ImGui::CollapsingHeader("File Type Associations");
        ImGui::PopStyleColor();
        if (!open) return;

        auto& handler = helpers::FiletypeHandler::instance();
        const auto& rules = handler.get_rules();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::TextWrapped("Rules control which card type opens a file from the directory browser. They are "
                            "tried in order and the first match wins. Match patterns are comma-separated: an "
                            "entry starting with '.' matches the file extension, anything else matches the "
                            "exact file name. \"Requires Ctrl+Click\" limits the rule to Ctrl-clicks, otherwise "
                            "it applies to a plain click.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::BeginTable("filetype_rules", 4,
                             ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, get_color(3));
            ImGui::TableSetupColumn("Match Patterns", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("URI Format", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Ctrl?", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            for (size_t i = 0; i < rules.size(); ++i) {
                const auto& rule = rules[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", rule.match.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", rule.uri_format.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", rule.requires_ctrl ? "Yes" : "No");

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("Edit")) {
                    filetype_edit_index_ = static_cast<int>(i);
                    filetype_adding_new_ = false;
                    strncpy(filetype_match_buf_, rule.match.c_str(), sizeof(filetype_match_buf_) - 1);
                    filetype_match_buf_[sizeof(filetype_match_buf_) - 1] = '\0';
                    strncpy(filetype_uri_buf_, rule.uri_format.c_str(), sizeof(filetype_uri_buf_) - 1);
                    filetype_uri_buf_[sizeof(filetype_uri_buf_) - 1] = '\0';
                    filetype_requires_ctrl_ = rule.requires_ctrl;
                    filetype_error_.clear();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Up")) {
                    handler.move_rule(i, -1);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down")) {
                    handler.move_rule(i, 1);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    handler.delete_rule(i);
                    if (filetype_edit_index_ == static_cast<int>(i)) {
                        filetype_edit_index_ = -1;
                        filetype_adding_new_ = false;
                    }
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        if (!filetype_adding_new_ && filetype_edit_index_ < 0) {
            if (ImGui::Button("Add Rule")) {
                filetype_adding_new_ = true;
                filetype_match_buf_[0] = '\0';
                strncpy(filetype_uri_buf_, "card:{}", sizeof(filetype_uri_buf_) - 1);
                filetype_uri_buf_[sizeof(filetype_uri_buf_) - 1] = '\0';
                filetype_requires_ctrl_ = false;
                filetype_error_.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to Defaults")) {
                handler.reset_to_defaults();
            }
        } else {
            ImGui::Text("%s", filetype_adding_new_ ? "New Rule" : "Edit Rule");

            ImGui::Text("Match Patterns:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##filetype_match_input", filetype_match_buf_, sizeof(filetype_match_buf_));

            ImGui::Text("URI Format:  ");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##filetype_uri_input", filetype_uri_buf_, sizeof(filetype_uri_buf_));

            ImGui::Checkbox("Requires Ctrl+Click", &filetype_requires_ctrl_);

            if (!filetype_error_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, get_color(6)); // Red for errors
                ImGui::TextWrapped("%s", filetype_error_.c_str());
                ImGui::PopStyleColor();
            }

            if (ImGui::Button("Save")) {
                if (std::string(filetype_match_buf_).empty()) {
                    filetype_error_ = "Match patterns cannot be empty.";
                } else if (!helpers::FiletypeHandler::validate_uri_format(filetype_uri_buf_)) {
                    filetype_error_ = "URI format must contain exactly one \"{}\" placeholder.";
                } else {
                    helpers::FiletypeRule const rule{filetype_match_buf_, filetype_uri_buf_, filetype_requires_ctrl_};
                    if (filetype_adding_new_) {
                        handler.add_rule(rule);
                    } else {
                        handler.update_rule(static_cast<size_t>(filetype_edit_index_), rule);
                    }
                    filetype_adding_new_ = false;
                    filetype_edit_index_ = -1;
                    filetype_error_.clear();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                filetype_adding_new_ = false;
                filetype_edit_index_ = -1;
                filetype_error_.clear();
            }
        }
    }

    void render_configuration_categories() {
        // Convert selected category index to enum
        std::optional<helpers::ConfigService::Category> filter_category;
        if (selected_category_ > 0) {
            filter_category = static_cast<helpers::ConfigService::Category>(selected_category_ - 1);
        }
        
        for (auto& [category, data] : category_data_) {
            // Apply category filter
            if (filter_category.has_value() && category != filter_category.value()) {
                continue;
            }
            
            // Skip system paths if disabled
            if (!show_system_paths_ && category == helpers::ConfigService::Category::SYSTEM_PATHS) {
                continue;
            }
            
            render_category_section(data, category);
        }
    }
    
    void render_category_section(CategoryData& data, helpers::ConfigService::Category category) {
        // Category header with expand/collapse
        ImGui::PushStyleColor(ImGuiCol_Text, get_color(2)); // Bright green for category headers
        if (ImGui::CollapsingHeader(data.name.c_str(), &data.expanded)) {
            ImGui::PopStyleColor();
            
            if (data.configs.empty()) {
                ImGui::Text("  No configurations registered in this category");
                return;
            }
            
            // Special handling for SSL mode if this is the HTTP SSL category
            if (category == helpers::ConfigService::Category::HTTP_SSL_CONFIG) {
                render_ssl_mode_selector();
                ImGui::Separator();
            }
            
            // Special handling for LLM configuration
            if (category == helpers::ConfigService::Category::LLM_CONFIG) {
                render_llm_config_editor();
                ImGui::Separator();
            }
            
            // Table for configuration entries
            if (ImGui::BeginTable(("configs_" + std::to_string(static_cast<int>(category))).c_str(), 4, 
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                
                // Table headers
                ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, get_color(3));
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();
                
                // Configuration rows
                for (const auto& config : data.configs) {
                    render_config_row(config);
                }
                
                ImGui::EndTable();
            }
        } else {
            ImGui::PopStyleColor();
        }
    }
    
    void render_config_row(const helpers::ConfigService::ConfigEntry& config) {
        // Apply search filter
        std::string search_term = search_filter_;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        
        if (!search_term.empty()) {
            std::string key_lower = config.key;
            std::string desc_lower = config.description;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(), ::tolower);
            
            if (key_lower.find(search_term) == std::string::npos && 
                desc_lower.find(search_term) == std::string::npos) {
                return;
            }
        }
        
        // Skip empty values if not shown
        if (!show_empty_values_ && config.value.empty()) {
            return;
        }
        
        ImGui::TableNextRow();
        
        // Key column
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", config.key.c_str());
        
        // Value column
        ImGui::TableSetColumnIndex(1);
        std::string display_value = config.value;
        
        if (config.value.empty()) {
            if (config.default_value.has_value()) {
                ImGui::PushStyleColor(ImGuiCol_Text, get_color(8)); // Yellow for default values
                display_value = "*" + config.default_value.value();
                ImGui::Text("%s", display_value.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray for empty
                ImGui::Text("(empty)");
                ImGui::PopStyleColor();
            }
        } else {
            if (config.is_sensitive && !show_sensitive_values_) {
                ImGui::PushStyleColor(ImGuiCol_Text, get_color(7)); // Blue for sensitive
                ImGui::Text("*** (hidden)");
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("%s", display_value.c_str());
            }
        }
        
        // Status column
        ImGui::TableSetColumnIndex(2);
        if (config.is_required && config.value.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, get_color(6)); // Red for missing required
            ImGui::Text("REQUIRED");
            ImGui::PopStyleColor();
        } else if (config.value.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray
            ImGui::Text("EMPTY");
            ImGui::PopStyleColor();
        } else if (config.is_sensitive) {
            ImGui::PushStyleColor(ImGuiCol_Text, get_color(7)); // Blue for sensitive
            ImGui::Text("SENSITIVE");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, get_color(2)); // Green for set
            ImGui::Text("SET");
            ImGui::PopStyleColor();
        }
        
        // Description column
        ImGui::TableSetColumnIndex(3);
        if (!config.description.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray
            ImGui::TextWrapped("%s", config.description.c_str());
            ImGui::PopStyleColor();
        }
    }
    
    static void export_to_env_file() {
        auto config_service = helpers::ConfigService::instance();
        if (config_service->export_to_env_file()) {
            // Show success message (could be enhanced with a popup or status indicator)
            ImGui::OpenPopup("Export Success");
        } else {
            // Show error message
            ImGui::OpenPopup("Export Error");
        }
        
        // Handle popups
        if (ImGui::BeginPopupModal("Export Success", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configuration successfully exported to .env file!");
            ImGui::Text("Location: %s", config_service->get_env_file_path().c_str());
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("Export Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to export configuration to .env file.");
            ImGui::Text("Please check file permissions and try again.");
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    
    static void render_ssl_mode_selector() {
        auto config_service = helpers::ConfigService::instance();
        std::string current_ssl_mode = config_service->get_env_optional("ROUEN_SSL_MODE").value_or("strict");
        
        ImGui::Text("Select SSL Mode:");
        ImGui::SameLine();
        
        // Structure to hold the SSL mode options
        struct SSLModeInfo {
            const char* name;
            const char* value;
            const char* description;
        };
        
        // Define the available modes
        SSLModeInfo ssl_modes[] = {
            {"Strict (Default)", "strict", "Full certificate validation with secure cipher list"},
            {"Relaxed", "relaxed", "Suitable for corporate environments, skips revocation checks"},
            {"Compatible", "compatible", "Maximum compatibility for problematic servers"},
            {"Atlassian", "atlassian", "Optimized specifically for Atlassian Cloud services"},
            {"Insecure", "insecure", "Disables certificate validation (use with caution!)"}
        };
        
        // Find current mode index
        int current_mode_idx = 0;
        constexpr int ssl_modes_count = static_cast<int>(sizeof(ssl_modes) / sizeof(*ssl_modes));
        for (int i = 0; i < ssl_modes_count; i++) {
            if (current_ssl_mode == ssl_modes[i].value) {
                current_mode_idx = i;
                break;
            }
        }
        
        static int selected_mode = current_mode_idx;
        
        // Create dropdown with mode names only
        const char* mode_names[ssl_modes_count];
        for (int i = 0; i < ssl_modes_count; i++) {
            mode_names[i] = ssl_modes[i].name;
        }
        
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("##ssl_mode", &selected_mode, mode_names, ssl_modes_count)) {
            // Store the setting in the ConfigService
            std::string new_value = ssl_modes[selected_mode].value;
            
            // This will internally update the cached value used by the HTTP client
            set_environment_variable("ROUEN_SSL_MODE", new_value.c_str(), 1);
            
            // Force refresh of config cache
            config_service->refresh_cache();
            
            // Log the change
            CONFIG_INFO_FMT("SSL mode changed to: {}", new_value);
        }
        
        // Display description of current selection
        ImGui::Spacing();
        ImGui::Text("Mode Description:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray
        ImGui::TextWrapped("%s", ssl_modes[selected_mode].description);
        ImGui::PopStyleColor();
        
        // Display warning for insecure mode
        if (selected_mode == 4) { // Insecure mode
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f)); // Red warning
            ImGui::TextWrapped("Warning: Insecure mode disables certificate validation and should only be used for testing or in controlled environments!");
            ImGui::PopStyleColor();
        }
        
        // Show which settings are affected
        ImGui::Spacing();
        ImGui::Text("This setting configures:");
        
        ImGui::Bullet(); ImGui::Text("Certificate validation");
        ImGui::Bullet(); ImGui::Text("SSL cipher compatibility");
        ImGui::Bullet(); ImGui::Text("Revocation checking");
    }
    
    void render_llm_config_editor() {
        struct LLMProviderInfo {
            const char* name;
            const char* value;
        };
        LLMProviderInfo providers[] = {
            {"Grok (X.AI)", "grok"},
            {"OpenAI", "openai"},
            {"Groq", "groq"},
            {"Google Gemini", "gemini"},
            {"Custom", "custom"}
        };
        constexpr int provider_count = 5;

        auto& lcm = helpers::LLMConfigManager::instance();
        const auto& configs = lcm.get_configs();
        
        if (selected_llm_config_name_.empty()) {
            selected_llm_config_name_ = lcm.get_default_config_name();
        }
        if (last_loaded_llm_config_name_ != selected_llm_config_name_) {
            populate_llm_config_buffers(selected_llm_config_name_);
        }
        
        ImGui::Text("Named LLM Configurations");
        ImGui::Spacing();
        
        // 1. Selector combo
        ImGui::Text("Select Config:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250);
        if (ImGui::BeginCombo("##llm_config_select", selected_llm_config_name_.c_str())) {
            for (const auto& config : configs) {
                bool is_selected = (selected_llm_config_name_ == config.name);
                std::string display_name = config.name;
                if (config.name == lcm.get_default_config_name()) {
                    display_name += " [Default]";
                }
                if (ImGui::Selectable(display_name.c_str(), is_selected)) {
                    selected_llm_config_name_ = config.name;
                    populate_llm_config_buffers(selected_llm_config_name_);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        
        // Button to set as default
        bool is_default = (selected_llm_config_name_ == lcm.get_default_config_name());
        if (is_default) {
            ImGui::TextDisabled(" (Default Config) ");
        } else {
            if (ImGui::Button("Set as Default")) {
                lcm.set_default_config_name(selected_llm_config_name_);
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Get the loaded entry to see current provider
        const auto* entry = lcm.get_config(selected_llm_config_name_);
        if (entry) {
            // Name field
            ImGui::Text("Configuration Name:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##llm_config_name_input", llm_name_buf_, sizeof(llm_name_buf_));
            
            ImGui::Text("Provider:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            
            if (ImGui::Combo("##llm_config_provider_combo", &selected_prov_idx_, 
                             [](void* data, int idx, const char** out_text) {
                                 auto* provs = static_cast<LLMProviderInfo*>(data);
                                 *out_text = provs[idx].name;
                                 return true;
                             }, providers, provider_count)) {
                // Auto-fill defaults if base_url or model_name are empty
                auto prov_enum = helpers::LLMConfig::string_to_provider(providers[selected_prov_idx_].value);
                std::string default_url = helpers::LLMConfig::get_base_url(prov_enum);
                std::string default_model = helpers::LLMConfig::get_default_model(prov_enum);
                
                strncpy(llm_url_buf_, default_url.c_str(), sizeof(llm_url_buf_) - 1);
                llm_url_buf_[sizeof(llm_url_buf_) - 1] = '\0';
                strncpy(llm_model_buf_, default_model.c_str(), sizeof(llm_model_buf_) - 1);
                llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
            }
            
            // API Key field (sensitive)
            ImGui::Text("API Key:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##llm_config_key_input", llm_key_buf_, sizeof(llm_key_buf_), 
                                 ImGuiInputTextFlags_Password);
            
            // Base URL field
            ImGui::Text("API Base URL:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##llm_config_url_input", llm_url_buf_, sizeof(llm_url_buf_));
            
            // Model Name field
            ImGui::Text("Model Name:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##llm_config_model_input", llm_model_buf_, sizeof(llm_model_buf_));
            ImGui::SameLine();
            
            AsyncState::FetchStatus current_fetch_status;
            std::string current_fetch_result;
            std::vector<std::string> current_models;
            {
                std::lock_guard<std::mutex> lock(async_state_->mutex);
                current_fetch_status = async_state_->fetch_status;
                current_fetch_result = async_state_->fetch_result;
                current_models = async_state_->fetched_models;
            }
            
            if (current_fetch_status == AsyncState::FetchStatus::FETCHING) {
                ImGui::BeginDisabled();
                ImGui::Button("Fetching...");
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Fetch Models")) {
                    trigger_fetch_models();
                }
            }
            
            if (!current_fetch_result.empty()) {
                ImGui::SameLine();
                if (current_fetch_status == AsyncState::FetchStatus::SUCCESS) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // green
                } else if (current_fetch_status == AsyncState::FetchStatus::FAILED) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // red
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // gray
                }
                ImGui::Text("%s", current_fetch_result.c_str());
                ImGui::PopStyleColor();
            }
            
            // Show dropdown of available models under it
            if (!current_models.empty()) {
                ImGui::Text("Select From Fetched List:");
                ImGui::SetNextItemWidth(300);
                if (ImGui::BeginCombo("##llm_config_model_combo_select", llm_model_buf_)) {
                    for (const auto& model : current_models) {
                        bool is_selected = (std::string(llm_model_buf_) == model);
                        if (ImGui::Selectable(model.c_str(), is_selected)) {
                            strncpy(llm_model_buf_, model.c_str(), sizeof(llm_model_buf_) - 1);
                            llm_model_buf_[sizeof(llm_model_buf_) - 1] = '\0';
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            
            ImGui::Spacing();
            
            // Check for changes
            bool has_changes = (std::string(llm_name_buf_) != entry->name) ||
                               (providers[selected_prov_idx_].value != entry->provider) ||
                               (std::string(llm_key_buf_) != entry->api_key) ||
                               (std::string(llm_url_buf_) != entry->base_url) ||
                               (std::string(llm_model_buf_) != entry->model_name);
            
            if (has_changes) {
                ImGui::PushStyleColor(ImGuiCol_Button, get_color(2));
                if (ImGui::Button("Save Changes")) {
                    helpers::LLMConfigEntry updated;
                    updated.name = llm_name_buf_;
                    updated.provider = providers[selected_prov_idx_].value;
                    updated.api_key = llm_key_buf_;
                    updated.base_url = llm_url_buf_;
                    updated.model_name = llm_model_buf_;
                    
                    lcm.update_config(selected_llm_config_name_, updated);
                    selected_llm_config_name_ = updated.name;
                    last_loaded_llm_config_name_ = updated.name;
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Save Changes");
                ImGui::EndDisabled();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("New Configuration")) {
                helpers::LLMConfigEntry new_entry;
                new_entry.name = std::format("LLM Config {}", configs.size() + 1);
                new_entry.provider = "openai";
                new_entry.base_url = "https://api.openai.com/v1";
                new_entry.model_name = "gpt-4";
                new_entry.api_key = "";
                lcm.add_config(new_entry);
                selected_llm_config_name_ = new_entry.name;
                populate_llm_config_buffers(selected_llm_config_name_);
            }
            
            if (configs.size() > 1) {
                ImGui::SameLine();
                if (ImGui::Button("Delete Configuration")) {
                    lcm.delete_config(selected_llm_config_name_);
                    selected_llm_config_name_ = lcm.get_default_config_name();
                    populate_llm_config_buffers(selected_llm_config_name_);
                }
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Quick test button
        if (ImGui::Button("Test Configuration")) {
            trigger_test_llm();
            ImGui::OpenPopup("Test LLM Config");
        }
        
        if (ImGui::BeginPopupModal("Test LLM Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            AsyncState::TestStatus current_status;
            std::string current_result;
            {
                std::lock_guard<std::mutex> lock(async_state_->mutex);
                current_status = async_state_->test_status;
                current_result = async_state_->test_result;
            }
            
            ImGui::Text("Testing LLM configuration...");
            ImGui::Spacing();
            
            if (current_status == AsyncState::TestStatus::TESTING) {
                ImGui::Text("Connecting to the endpoint...");
                ImGui::TextDisabled("(this may take a few seconds)");
            } else if (current_status == AsyncState::TestStatus::SUCCESS) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // green
                ImGui::TextWrapped("%s", current_result.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // red
                ImGui::TextWrapped("%s", current_result.c_str());
                ImGui::PopStyleColor();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};

} // namespace rouen::cards
