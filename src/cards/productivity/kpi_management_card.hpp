#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <format>
#include <cstring>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"

// 3. All other includes
#include "../interface/card.hpp"
#include "../../models/kpi_model.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

class kpi_management_card : public card {
public:
    kpi_management_card() {
        // Set custom colors for KPI management theme
        colors[0] = ImVec4{0.4f, 0.6f, 0.2f, 1.0f};  // Primary green
        colors[1] = ImVec4{0.5f, 0.7f, 0.3f, 0.7f};  // Secondary green
        get_color(2, ImVec4(0.0f, 0.7f, 0.3f, 1.0f));  // Success green
        get_color(3, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));  // Error red
        get_color(4, ImVec4(0.9f, 0.7f, 0.0f, 1.0f));  // Warning amber
        get_color(5, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));  // Neutral gray
        get_color(6, ImVec4(0.2f, 0.6f, 0.8f, 1.0f));  // Info blue
        
        name("KPI Management");
        width = 800.0f;
        requested_fps = 3;
        
        // Initialize KPI model
        kpi_model_ = std::make_shared<models::kpi_model>();
        
        // Initialize form fields
        clear_kpi_form();
        clear_category_form();
        
        // Load initial data
        refresh_data();
    }
    
    bool render() override {
        return render_window([this]() {
            // Check for async operations completion
            check_async_operations();
            
            if (!error_message_.empty()) {
                render_error_message();
                ImGui::Separator();
            }
            
            if (ImGui::BeginTabBar("ManagementTabs")) {
                if (ImGui::BeginTabItem("KPIs")) {
                    render_kpis_tab();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Categories")) {
                    render_categories_tab();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Dashboards")) {
                    render_dashboards_tab();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Data Import")) {
                    render_import_tab();
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
        });
    }
    
    std::string get_uri() const override {
        return "kpi-management";
    }

private:
    // Data members
    std::shared_ptr<models::kpi_model> kpi_model_;
    std::vector<models::kpi_category> categories_;
    std::vector<models::kpi> kpis_;
    std::vector<models::kpi_dashboard> dashboards_;
    
    // UI state
    std::string error_message_;
    std::string success_message_;
    bool is_loading_ = false;
    
    // Form state for KPI creation/editing
    char kpi_name_[256] = {0};
    char kpi_description_[1024] = {0};
    char kpi_unit_[64] = {0};
    models::kpi_value_type kpi_value_type_ = models::kpi_value_type::number;
    models::kpi_aggregation kpi_aggregation_ = models::kpi_aggregation::average;
    std::string selected_category_id_;
    char kpi_current_value_[64] = {0};
    bool has_target_ = false;
    models::kpi_target_type target_type_ = models::kpi_target_type::MINIMUM;
    char target_value_[64] = {0};
    char target_description_[256] = {0};
    std::string editing_kpi_id_;
    
    // Form state for category creation/editing
    char category_name_[256] = {0};
    char category_description_[1024] = {0};
    char category_color_[32] = "#4A90E2";
    std::string editing_category_id_;
    
    // Search and filter state
    char kpi_search_filter_[256] = {0};
    char category_search_filter_[256] = {0};
    
    void refresh_data() {
        is_loading_ = true;
        error_message_.clear();
        success_message_.clear();
        
        // Load categories
        auto categories_future = kpi_model_->get_categories();
        models::kpi_model::execute_async<std::vector<models::kpi_category>>(
            std::move(categories_future),
            [this](const std::vector<models::kpi_category>& result) {
                categories_ = result;
                load_kpis();
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading categories: {}", error);
                is_loading_ = false;
            }
        );
        
        // Load dashboards
        auto dashboards_future = kpi_model_->get_dashboards();
        models::kpi_model::execute_async<std::vector<models::kpi_dashboard>>(
            std::move(dashboards_future),
            [this](const std::vector<models::kpi_dashboard>& result) {
                dashboards_ = result;
            },
            [](const std::string& /* error */) {
                // Non-critical error for dashboards
            }
        );
    }
    
    void load_kpis() {
        auto kpis_future = kpi_model_->get_kpis();
        models::kpi_model::execute_async<std::vector<models::kpi>>(
            std::move(kpis_future),
            [this](const std::vector<models::kpi>& result) {
                kpis_ = result;
                is_loading_ = false;
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading KPIs: {}", error);
                is_loading_ = false;
            }
        );
    }
    
    void check_async_operations() {
        // This method would check for completed async operations
        // The actual implementation depends on the async framework used
    }
    
    void render_error_message() {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[3]);
        ImGui::TextWrapped("Error: %s", error_message_.c_str());
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        if (ImGui::Button("Clear##error")) {
            error_message_.clear();
        }
    }
    
    void render_success_message() {
        if (!success_message_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
            ImGui::TextWrapped("Success: %s", success_message_.c_str());
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            if (ImGui::Button("Clear##success")) {
                success_message_.clear();
            }
            ImGui::Separator();
        }
    }
    
    void render_kpis_tab() {
        render_success_message();
        
        ImGui::TextColored(colors[0], "KPI Management");
        
        // Search and filter controls
        ImGui::PushItemWidth(200.0f);
        ImGui::InputTextWithHint("##kpi_search", "Search KPIs...", kpi_search_filter_, sizeof(kpi_search_filter_));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Add New KPI")) {
            clear_kpi_form();
            ImGui::OpenPopup("KPI Form");
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            refresh_data();
        }
        
        ImGui::Separator();
        
        if (is_loading_) {
            ImGui::TextColored(colors[1], "Loading KPIs...");
            return;
        }
        
        // KPIs table
        if (ImGui::BeginTable("KPIsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Current Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            
            auto filtered_kpis = get_filtered_kpis();
            for (const auto& kpi : filtered_kpis) {
                ImGui::TableNextRow();
                
                // Name
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[0], "%s", kpi.name.c_str());
                if (!kpi.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextWrapped("%s", kpi.description.c_str());
                    ImGui::EndTooltip();
                }
                
                // Category
                ImGui::TableNextColumn();
                std::string category_name = get_category_name(kpi.category_id);
                ImGui::Text("%s", category_name.c_str());
                
                // Current Value
                ImGui::TableNextColumn();
                std::string current_value = format_kpi_value(kpi.current_value, kpi.value_type);
                ImGui::Text("%s", current_value.c_str());
                
                // Target
                ImGui::TableNextColumn();
                if (kpi.target.has_value()) {
                    std::string target_value = format_kpi_value(kpi.target->target_value, kpi.value_type);
                    ImGui::Text("%s", target_value.c_str());
                } else {
                    ImGui::TextColored(colors[5], "None");
                }
                
                // Unit
                ImGui::TableNextColumn();
                ImGui::Text("%s", kpi.unit.c_str());
                
                // Actions
                ImGui::TableNextColumn();
                ImGui::PushID(kpi.id.c_str());
                
                if (ImGui::Button("Edit", ImVec2(50, 0))) {
                    populate_kpi_form(kpi);
                    ImGui::OpenPopup("KPI Form");
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Delete", ImVec2(50, 0))) {
                    ImGui::OpenPopup("Delete KPI Confirm");
                }
                
                // Delete confirmation popup
                if (ImGui::BeginPopupModal("Delete KPI Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Are you sure you want to delete KPI '%s'?", kpi.name.c_str());
                    ImGui::Text("This action cannot be undone.");
                    ImGui::Separator();
                    
                    if (ImGui::Button("Delete", ImVec2(120, 0))) {
                        delete_kpi(kpi.id);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        // KPI form popup
        render_kpi_form_popup();
    }
    
    void render_categories_tab() {
        render_success_message();
        
        ImGui::TextColored(colors[0], "Category Management");
        
        // Search and controls
        ImGui::PushItemWidth(200.0f);
        ImGui::InputTextWithHint("##category_search", "Search categories...", category_search_filter_, sizeof(category_search_filter_));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Add New Category")) {
            clear_category_form();
            ImGui::OpenPopup("Category Form");
        }
        
        ImGui::Separator();
        
        if (is_loading_) {
            ImGui::TextColored(colors[1], "Loading categories...");
            return;
        }
        
        // Categories table
        if (ImGui::BeginTable("CategoriesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("KPI Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            
            auto filtered_categories = get_filtered_categories();
            for (const auto& category : filtered_categories) {
                ImGui::TableNextRow();
                
                // Name
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[0], "%s", category.name.c_str());
                
                // Description
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", category.description.c_str());
                
                // KPI Count
                ImGui::TableNextColumn();
                int kpi_count = count_kpis_in_category(category.id);
                ImGui::Text("%d", kpi_count);
                
                // Actions
                ImGui::TableNextColumn();
                ImGui::PushID(category.id.c_str());
                
                if (ImGui::Button("Edit", ImVec2(50, 0))) {
                    populate_category_form(category);
                    ImGui::OpenPopup("Category Form");
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Delete", ImVec2(50, 0))) {
                    ImGui::OpenPopup("Delete Category Confirm");
                }
                
                // Delete confirmation popup
                if (ImGui::BeginPopupModal("Delete Category Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Are you sure you want to delete category '%s'?", category.name.c_str());
                    int category_kpi_count = count_kpis_in_category(category.id);
                    if (category_kpi_count > 0) {
                        ImGui::TextColored(colors[3], "Warning: This category contains %d KPI(s).", category_kpi_count);
                        ImGui::TextColored(colors[3], "These KPIs will be moved to 'Uncategorized'.");
                    }
                    ImGui::Separator();
                    
                    if (ImGui::Button("Delete", ImVec2(120, 0))) {
                        delete_category(category.id);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        // Category form popup
        render_category_form_popup();
    }
    
    void render_dashboards_tab() {
        ImGui::TextColored(colors[0], "Dashboard Management");
        ImGui::TextColored(colors[5], "Dashboard management functionality coming soon...");
        ImGui::Text("This tab will include:");
        ImGui::BulletText("Create and edit dashboards");
        ImGui::BulletText("Add/remove KPIs from dashboards");
        ImGui::BulletText("Configure dashboard layouts");
        ImGui::BulletText("Set dashboard permissions");
    }
    
    void render_import_tab() {
        ImGui::TextColored(colors[0], "Data Import/Export");
        ImGui::TextColored(colors[5], "Import/export functionality coming soon...");
        ImGui::Text("This tab will include:");
        ImGui::BulletText("Import KPIs from CSV/Excel files");
        ImGui::BulletText("Export KPI data to various formats");
        ImGui::BulletText("Bulk data operations");
        ImGui::BulletText("Data validation and error handling");
    }
    
    void render_kpi_form_popup() {
        if (ImGui::BeginPopupModal("KPI Form", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool is_editing = !editing_kpi_id_.empty();
            ImGui::TextColored(colors[0], "%s KPI", is_editing ? "Edit" : "Create");
            ImGui::Separator();
            
            // Name
            ImGui::Text("Name:");
            ImGui::PushItemWidth(300.0f);
            ImGui::InputText("##name", kpi_name_, sizeof(kpi_name_));
            ImGui::PopItemWidth();
            
            // Description
            ImGui::Text("Description:");
            ImGui::PushItemWidth(300.0f);
            ImGui::InputTextMultiline("##description", kpi_description_, sizeof(kpi_description_), ImVec2(300, 60));
            ImGui::PopItemWidth();
            
            // Category
            ImGui::Text("Category:");
            ImGui::PushItemWidth(200.0f);
            if (ImGui::BeginCombo("##category", get_category_name(selected_category_id_).c_str())) {
                for (const auto& category : categories_) {
                    bool is_selected = (selected_category_id_ == category.id);
                    if (ImGui::Selectable(category.name.c_str(), is_selected)) {
                        selected_category_id_ = category.id;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            
            // Value Type
            ImGui::Text("Value Type:");
            ImGui::PushItemWidth(150.0f);
            const char* value_types[] = {"Integer", "Decimal", "Percentage", "Currency", "Duration"};
            int current_type = static_cast<int>(kpi_value_type_);
            if (ImGui::Combo("##value_type", &current_type, value_types, IM_ARRAYSIZE(value_types))) {
                kpi_value_type_ = static_cast<models::kpi_value_type>(current_type);
            }
            ImGui::PopItemWidth();
            
            // Unit
            ImGui::Text("Unit:");
            ImGui::PushItemWidth(150.0f);
            ImGui::InputText("##unit", kpi_unit_, sizeof(kpi_unit_));
            ImGui::PopItemWidth();
            
            // Current Value
            ImGui::Text("Current Value:");
            ImGui::PushItemWidth(150.0f);
            ImGui::InputText("##current_value", kpi_current_value_, sizeof(kpi_current_value_));
            ImGui::PopItemWidth();
            
            // Aggregation Method
            ImGui::Text("Aggregation Method:");
            ImGui::PushItemWidth(150.0f);
            const char* aggregation_methods[] = {"Sum", "Average", "Count", "Min", "Max", "Latest"};
            int current_aggregation = static_cast<int>(kpi_aggregation_);
            if (ImGui::Combo("##aggregation", &current_aggregation, aggregation_methods, IM_ARRAYSIZE(aggregation_methods))) {
                kpi_aggregation_ = static_cast<models::kpi_aggregation>(current_aggregation);
            }
            ImGui::PopItemWidth();
            
            ImGui::Separator();
            
            // Target section
            if (ImGui::Checkbox("Has Target", &has_target_)) {
                // Clear target fields if unchecked
                if (!has_target_) {
                    target_value_[0] = '\0';
                    target_description_[0] = '\0';
                }
            }
            
            if (has_target_) {
                ImGui::Indent();
                
                // Target Type
                ImGui::Text("Target Type:");
                ImGui::PushItemWidth(150.0f);
                const char* target_types[] = {"Minimum", "Maximum", "Exact"};
                int current_target_type = static_cast<int>(target_type_);
                if (ImGui::Combo("##target_type", &current_target_type, target_types, IM_ARRAYSIZE(target_types))) {
                    target_type_ = static_cast<models::kpi_target_type>(current_target_type);
                }
                ImGui::PopItemWidth();
                
                // Target Value
                ImGui::Text("Target Value:");
                ImGui::PushItemWidth(150.0f);
                ImGui::InputText("##target_value", target_value_, sizeof(target_value_));
                ImGui::PopItemWidth();
                
                // Target Description
                ImGui::Text("Target Description:");
                ImGui::PushItemWidth(300.0f);
                ImGui::InputText("##target_description", target_description_, sizeof(target_description_));
                ImGui::PopItemWidth();
                
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Buttons
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (validate_kpi_form()) {
                    save_kpi();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
    }
    
    void render_category_form_popup() {
        if (ImGui::BeginPopupModal("Category Form", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool is_editing = !editing_category_id_.empty();
            ImGui::TextColored(colors[0], "%s Category", is_editing ? "Edit" : "Create");
            ImGui::Separator();
            
            // Name
            ImGui::Text("Name:");
            ImGui::PushItemWidth(250.0f);
            ImGui::InputText("##cat_name", category_name_, sizeof(category_name_));
            ImGui::PopItemWidth();
            
            // Description
            ImGui::Text("Description:");
            ImGui::PushItemWidth(300.0f);
            ImGui::InputTextMultiline("##cat_description", category_description_, sizeof(category_description_), ImVec2(300, 80));
            ImGui::PopItemWidth();
            
            // Color (placeholder for now)
            ImGui::Text("Color:");
            ImGui::PushItemWidth(150.0f);
            ImGui::InputText("##cat_color", category_color_, sizeof(category_color_));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "(Hex color code)");
            
            ImGui::Separator();
            
            // Buttons
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (validate_category_form()) {
                    save_category();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
    }
    
    // Helper methods
    std::vector<models::kpi> get_filtered_kpis() const {
        std::vector<models::kpi> filtered;
        
        std::string search_term = kpi_search_filter_;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        
        for (const auto& kpi : kpis_) {
            if (!search_term.empty()) {
                std::string kpi_name = kpi.name;
                std::transform(kpi_name.begin(), kpi_name.end(), kpi_name.begin(), ::tolower);
                if (kpi_name.find(search_term) == std::string::npos) {
                    continue;
                }
            }
            filtered.push_back(kpi);
        }
        
        return filtered;
    }
    
    std::vector<models::kpi_category> get_filtered_categories() const {
        std::vector<models::kpi_category> filtered;
        
        std::string search_term = category_search_filter_;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        
        for (const auto& category : categories_) {
            if (!search_term.empty()) {
                std::string category_name = category.name;
                std::transform(category_name.begin(), category_name.end(), category_name.begin(), ::tolower);
                if (category_name.find(search_term) == std::string::npos) {
                    continue;
                }
            }
            filtered.push_back(category);
        }
        
        return filtered;
    }
    
    std::string get_category_name(const std::string& category_id) const {
        if (category_id.empty()) return "Select Category";
        
        auto it = std::find_if(categories_.begin(), categories_.end(),
            [&category_id](const models::kpi_category& c) { return c.id == category_id; });
        return it != categories_.end() ? it->name : "Unknown Category";
    }
    
    int count_kpis_in_category(const std::string& category_id) const {
        return static_cast<int>(std::count_if(kpis_.begin(), kpis_.end(),
            [&category_id](const models::kpi& kpi) { return kpi.category_id == category_id; }));
    }
    
    std::string format_kpi_value(double value, models::kpi_value_type type) const {
        switch (type) {
            case models::kpi_value_type::number:
                return std::format("{:.2f}", value);
            case models::kpi_value_type::percentage:
                return std::format("{:.1f}%", value);
            case models::kpi_value_type::currency:
                return std::format("${:.2f}", value);
            case models::kpi_value_type::count:
                return std::format("{}", static_cast<long long>(value));
            case models::kpi_value_type::ratio:
                return std::format("{:.2f}:1", value);
            case models::kpi_value_type::boolean:
                return value > 0.5 ? "Yes" : "No";
            case models::kpi_value_type::text:
                return std::format("{:.2f}", value);
            default:
                return std::format("{:.2f}", value);
        }
    }
    
    void clear_kpi_form() {
        editing_kpi_id_.clear();
        kpi_name_[0] = '\0';
        kpi_description_[0] = '\0';
        kpi_unit_[0] = '\0';
        kpi_value_type_ = models::kpi_value_type::number;
        kpi_aggregation_ = models::kpi_aggregation::average;
        selected_category_id_.clear();
        kpi_current_value_[0] = '\0';
        has_target_ = false;
        target_type_ = models::kpi_target_type::MINIMUM;
        target_value_[0] = '\0';
        target_description_[0] = '\0';
    }
    
    void clear_category_form() {
        editing_category_id_.clear();
        category_name_[0] = '\0';
        category_description_[0] = '\0';
        std::strcpy(category_color_, "#4A90E2");
    }
    
    void populate_kpi_form(const models::kpi& kpi) {
        editing_kpi_id_ = kpi.id;
        std::strcpy(kpi_name_, kpi.name.c_str());
        std::strcpy(kpi_description_, kpi.description.c_str());
        std::strcpy(kpi_unit_, kpi.unit.c_str());
        kpi_value_type_ = kpi.value_type;
        kpi_aggregation_ = kpi.aggregation_type;
        selected_category_id_ = kpi.category_id;
        std::strcpy(kpi_current_value_, std::format("{}", kpi.current_value).c_str());
        
        if (kpi.target.has_value()) {
            has_target_ = true;
            target_type_ = kpi.target->target_type;
            std::strcpy(target_value_, std::format("{}", kpi.target->target_value).c_str());
            std::strcpy(target_description_, kpi.target->description.c_str());
        } else {
            has_target_ = false;
            target_value_[0] = '\0';
            target_description_[0] = '\0';
        }
    }
    
    void populate_category_form(const models::kpi_category& category) {
        editing_category_id_ = category.id;
        std::strcpy(category_name_, category.name.c_str());
        std::strcpy(category_description_, category.description.c_str());
        std::strcpy(category_color_, category.color.c_str());
    }
    
    bool validate_kpi_form() {
        if (std::strlen(kpi_name_) == 0) {
            error_message_ = "KPI name is required";
            return false;
        }
        
        if (selected_category_id_.empty()) {
            error_message_ = "Please select a category";
            return false;
        }
        
        if (std::strlen(kpi_current_value_) == 0) {
            error_message_ = "Current value is required";
            return false;
        }
        
        // Try to parse current value
        try {
            std::stod(kpi_current_value_);
        } catch (...) {
            error_message_ = "Invalid current value format";
            return false;
        }
        
        // Validate target value if has_target_ is true
        if (has_target_ && std::strlen(target_value_) > 0) {
            try {
                std::stod(target_value_);
            } catch (...) {
                error_message_ = "Invalid target value format";
                return false;
            }
        }
        
        return true;
    }
    
    bool validate_category_form() {
        if (std::strlen(category_name_) == 0) {
            error_message_ = "Category name is required";
            return false;
        }
        
        return true;
    }
    
    void save_kpi() {
        try {
            models::kpi kpi;
            
            if (!editing_kpi_id_.empty()) {
                kpi.id = editing_kpi_id_;
            }
            
            kpi.name = kpi_name_;
            kpi.description = kpi_description_;
            kpi.unit = kpi_unit_;
            kpi.value_type = kpi_value_type_;
            kpi.aggregation_type = kpi_aggregation_;
            kpi.category_id = selected_category_id_;
            kpi.current_value = std::stod(kpi_current_value_);
            
            if (has_target_ && std::strlen(target_value_) > 0) {
                models::kpi_target target;
                target.target_type = target_type_;
                target.target_value = std::stod(target_value_);
                target.description = target_description_;
                kpi.target = target;
            }
            
            // Save KPI (async operation)
            std::future<bool> save_future;
            if (editing_kpi_id_.empty()) {
                save_future = kpi_model_->create_kpi(kpi);
            } else {
                save_future = kpi_model_->update_kpi(kpi);
            }
            
            models::kpi_model::execute_async_bool(
                std::move(save_future),
                [this](bool success) {
                    if (success) {
                        success_message_ = editing_kpi_id_.empty() ? 
                            "KPI created successfully" : "KPI updated successfully";
                        clear_kpi_form();
                        refresh_data();
                    } else {
                        error_message_ = "Failed to save KPI";
                    }
                },
                [this](const std::string& error) {
                    error_message_ = std::format("Error saving KPI: {}", error);
                }
            );
            
        } catch (const std::exception& e) {
            error_message_ = std::format("Error saving KPI: {}", e.what());
        }
    }
    
    void save_category() {
        try {
            models::kpi_category category;
            
            if (!editing_category_id_.empty()) {
                category.id = editing_category_id_;
            }
            
            category.name = category_name_;
            category.description = category_description_;
            category.color = category_color_;
            
            // Save category (async operation)
            std::future<bool> save_future;
            if (editing_category_id_.empty()) {
                save_future = kpi_model_->create_category(category);
            } else {
                save_future = kpi_model_->update_category(category);
            }
            
            models::kpi_model::execute_async_bool(
                std::move(save_future),
                [this](bool success) {
                    if (success) {
                        success_message_ = editing_category_id_.empty() ? 
                            "Category created successfully" : "Category updated successfully";
                        clear_category_form();
                        refresh_data();
                    } else {
                        error_message_ = "Failed to save category";
                    }
                },
                [this](const std::string& error) {
                    error_message_ = std::format("Error saving category: {}", error);
                }
            );
            
        } catch (const std::exception& e) {
            error_message_ = std::format("Error saving category: {}", e.what());
        }
    }
    
    void delete_kpi(const std::string& kpi_id) {
        auto delete_future = kpi_model_->delete_kpi(kpi_id);
        
        models::kpi_model::execute_async_bool(
            std::move(delete_future),
            [this](bool success) {
                if (success) {
                    success_message_ = "KPI deleted successfully";
                    refresh_data();
                } else {
                    error_message_ = "Failed to delete KPI";
                }
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error deleting KPI: {}", error);
            }
        );
    }
    
    void delete_category(const std::string& category_id) {
        auto delete_future = kpi_model_->delete_category(category_id);
        
        models::kpi_model::execute_async_bool(
            std::move(delete_future),
            [this](bool success) {
                if (success) {
                    success_message_ = "Category deleted successfully";
                    refresh_data();
                } else {
                    error_message_ = "Failed to delete category";
                }
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error deleting category: {}", error);
            }
        );
    }
};

} // namespace rouen::cards
