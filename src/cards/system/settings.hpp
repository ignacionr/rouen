#pragma once

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdlib> // For setenv on Unix platforms
#include <mutex>
#include <thread>
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/gemini_adapter.hpp"
#include "../../helpers/cppgpt.hpp"
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
    }
    
    ~settings_card() override = default;
    
    bool render() override {
        return render_window([this]() {
            render_settings_content();
        });
    }
    
    std::string get_uri() const override {
        return "settings";
    }

private:
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
        }
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
                    auto prov_enum = helpers::LLMConfig::string_to_provider(providers[selected_prov_idx_].value);
                    std::string api_key = llm_key_buf_;
                    std::string base_url = llm_url_buf_;
                    
                    {
                        std::lock_guard<std::mutex> lock(async_state_->mutex);
                        async_state_->fetch_status = AsyncState::FetchStatus::FETCHING;
                        async_state_->fetch_result = "Fetching models...";
                        async_state_->fetched_models.clear();
                    }
                    
                    std::thread([state_weak = std::weak_ptr<AsyncState>(async_state_), prov_enum, api_key, base_url]() {
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
                    }).detach();
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
            auto prov_enum = helpers::LLMConfig::string_to_provider(providers[selected_prov_idx_].value);
            std::string api_key = llm_key_buf_;
            std::string base_url = llm_url_buf_;
            std::string model_name = llm_model_buf_;
            
            {
                std::lock_guard<std::mutex> lock(async_state_->mutex);
                async_state_->test_status = AsyncState::TestStatus::TESTING;
                async_state_->test_result = "Testing connection...";
            }
            
            std::thread([state_weak = std::weak_ptr<AsyncState>(async_state_), prov_enum, api_key, base_url, model_name]() {
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
                        // Trim reply
                        auto first = reply.find_first_not_of(" \t\r\n");
                        if (first != std::string::npos) {
                            auto last = reply.find_last_not_of(" \t\r\n");
                            reply = reply.substr(first, last - first + 1);
                        }
                    } else {
                        throw std::runtime_error("Received empty response from API.");
                    }
                    
                    if (auto state = state_weak.lock()) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->test_status = AsyncState::TestStatus::SUCCESS;
                        state->test_result = "Connection successful!\nResponse from model: \"" + reply + "\"";
                    }
                } catch (const std::exception& e) {
                    if (auto state = state_weak.lock()) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->test_status = AsyncState::TestStatus::FAILED;
                        state->test_result = std::string("Connection failed!\nError: ") + e.what();
                    }
                }
            }).detach();
            
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
