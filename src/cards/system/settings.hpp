#pragma once

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdlib> // For setenv on Unix platforms
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
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
    
    void refresh_config_data() {
        auto config_service = helpers::ConfigService::instance();
        
        // Clear existing data
        category_data_.clear();
        
        // Get category names
        std::map<helpers::ConfigService::Category, std::string> category_names = {
            {helpers::ConfigService::Category::API_CREDENTIALS, "API Credentials"},
            {helpers::ConfigService::Category::JIRA_PROFILES, "JIRA Profiles"},
            {helpers::ConfigService::Category::BYBIT_CONFIG, "Bybit Configuration"},
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
        float current_time = static_cast<float>(ImGui::GetTime());
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
        const char* category_items[] = {"All", "API Credentials", "JIRA Profiles", "Bybit Config", "System Paths", "Executable Paths", "Logging", "HTTP SSL", "General"};
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
    
    void export_to_env_file() {
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
    
    void render_ssl_mode_selector() {
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
};

} // namespace rouen::cards
