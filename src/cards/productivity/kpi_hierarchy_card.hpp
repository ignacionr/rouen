#pragma once

#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../models/kpi_model.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class kpi_hierarchy_card : public card {
public:
    kpi_hierarchy_card() {
        // Set custom colors for KPI theme
        colors[0] = {0.2f, 0.5f, 0.8f, 1.0f}; // Blue primary color
        colors[1] = {0.3f, 0.6f, 0.9f, 0.7f}; // Light blue secondary color
        
        // Additional colors for KPI status
        get_color(2, ImVec4(0.2f, 0.7f, 0.2f, 1.0f)); // Green for good performance
        get_color(3, ImVec4(0.8f, 0.6f, 0.2f, 1.0f)); // Amber for warning
        get_color(4, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for poor performance
        get_color(5, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Gray for disabled/secondary text
        
        name("KPI Hierarchy");
        width = 600.0f;
        requested_fps = 2; // Lower refresh rate for tree view
        
        // Initialize KPI model
        kpi_model_ = std::make_shared<models::kpi_model>();
        
        // Load initial data
        refresh_data();
        
        // Clear form state
        clear_form();
    }

    bool render() override {
        render_header();
        ImGui::Separator();
        
        // Check for async operations completion
        check_async_operations();
        
        if (ImGui::BeginTabBar("KPIHierarchyTabs")) {
            if (ImGui::BeginTabItem("Hierarchy Tree")) {
                render_hierarchy_tree();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Category Management")) {
                render_category_management();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Move & Reorganize")) {
                render_reorganize_tab();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        // Render modals
        render_move_kpi_modal();
        render_delete_confirmation_modal();
        
        return true;
    }

    std::string get_uri() const override {
        return "kpi-hierarchy";
    }

private:
    std::shared_ptr<models::kpi_model> kpi_model_;
    
    // Data
    std::vector<models::kpi_category> categories_;
    std::vector<models::kpi> kpis_;
    
    // UI state
    bool show_move_modal_ = false;
    bool show_delete_modal_ = false;
    int selected_kpi_id_ = -1;
    int selected_category_id_ = -1;
    int move_target_parent_id_ = -1;
    int delete_target_id_ = -1;
    bool is_deleting_category_ = false;
    
    // Form data for new categories
    char category_name_buffer_[256] = {0};
    char category_description_buffer_[512] = {0};
    ImVec4 category_color_ = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    
    // Search and filter
    char search_buffer_[256] = {0};
    std::string filter_category_id_; // empty string for all categories
    
    // Async operation tracking
    std::future<void> refresh_future_;
    std::future<void> create_category_future_;
    std::future<void> update_category_future_;
    std::future<void> delete_future_;
    std::future<void> move_future_;
    
    // Status messages
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_time_;
    bool status_is_error_ = false;

    void render_header() {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[0]);
        ImGui::Text("KPI Hierarchy Management");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        if (ImGui::Button("Refresh Data", ImVec2(140, 0))) {
            refresh_data();
        }
        
        // Search and filter
        ImGui::Spacing();
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("##search", search_buffer_, sizeof(search_buffer_))) {
            // Search is applied in real-time during rendering
        }
        
        ImGui::SameLine();
        ImGui::Text("Filter by Category:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##filter_category", get_category_name(filter_category_id_).c_str())) {
            if (ImGui::Selectable("All Categories", filter_category_id_.empty())) {
                filter_category_id_.clear();
            }
            for (const auto& category : categories_) {
                bool is_selected = filter_category_id_ == category.id;
                if (ImGui::Selectable(category.name.c_str(), is_selected)) {
                    filter_category_id_ = category.id;
                }
            }
            ImGui::EndCombo();
        }
        
        // Status message
        render_status_message();
    }

    void render_hierarchy_tree() {
        if (categories_.empty()) {
            ImGui::TextColored(colors[5], "No categories found. Create a category in the Category Management tab.");
            return;
        }
        
        ImGui::Text("Expand categories to view KPI hierarchy:");
        ImGui::Spacing();
        
        for (const auto& category : categories_) {
            if (!filter_category_id_.empty() && filter_category_id_ != category.id) {
                continue;
            }
            
            render_category_node(category);
        }
    }

    void render_category_node(const models::kpi_category& category) {
        // Apply category color (parse hex color)
        ImVec4 category_color = parse_hex_color(category.color);
        
        ImGui::PushStyleColor(ImGuiCol_Text, category_color);
        
        // Get KPIs in this category
        auto category_kpis = get_kpis_in_category(category.id);
        auto root_kpis = get_root_kpis_in_category(category.id);
        
        std::string category_label = std::format("{} ({} KPIs)", category.name, category_kpis.size());
        
        bool category_open = ImGui::TreeNode(("cat_" + std::to_string(category.id)).c_str(), "%s", category_label.c_str());
        ImGui::PopStyleColor();
        
        // Category context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Edit Category")) {
                populate_category_form(category);
            }
            if (ImGui::MenuItem("Delete Category")) {
                show_delete_modal_ = true;
                delete_target_id_ = category.id;
                is_deleting_category_ = true;
            }
            ImGui::EndPopup();
        }
        
        if (category_open) {
            if (!category.description.empty()) {
                ImGui::TextWrapped("Description: %s", category.description.c_str());
                ImGui::Spacing();
            }
            
            if (root_kpis.empty()) {
                ImGui::TextColored(colors[5], "No KPIs in this category");
            } else {
                for (const auto& kpi : root_kpis) {
                    if (matches_search(kpi)) {
                        render_kpi_node(kpi, 0);
                    }
                }
            }
            
            ImGui::TreePop();
        }
    }

    void render_kpi_node(const models::kpi& kpi, int depth) {
        // Indentation for hierarchy levels
        if (depth > 0) {
            ImGui::Indent(20.0f * depth);
        }
        
        // KPI status color
        ImVec4 status_color = get_kpi_status_color(kpi);
        ImGui::PushStyleColor(ImGuiCol_Text, status_color);
        
        // KPI icon based on status
        const char* status_icon = get_kpi_status_icon(kpi);
        
        // Get child KPIs
        auto child_kpis = get_child_kpis(kpi.id);
        
        std::string kpi_label = std::format("{} {} ({})", 
                                           status_icon, kpi.name, kpi.unit);
        
        bool has_children = !child_kpis.empty();
        bool kpi_open = false;
        
        if (has_children) {
            kpi_open = ImGui::TreeNode(("kpi_" + std::to_string(kpi.id)).c_str(), "%s", kpi_label.c_str());
        } else {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::Text("%s", kpi_label.c_str());
        }
        
        ImGui::PopStyleColor();
        
        // KPI context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("View Details")) {
                "create_card"_sfn(std::format("kpi-dashboard:{}", kpi.id));
            }
            if (ImGui::MenuItem("Edit KPI")) {
                "create_card"_sfn(std::format("kpi-management:edit:{}", kpi.id));
            }
            if (ImGui::MenuItem("Move KPI")) {
                show_move_modal_ = true;
                selected_kpi_id_ = kpi.id;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete KPI")) {
                show_delete_modal_ = true;
                delete_target_id_ = kpi.id;
                is_deleting_category_ = false;
            }
            ImGui::EndPopup();
        }
        
        // Tooltip with KPI details
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Name: %s", kpi.name.c_str());
            ImGui::Text("Current Value: %.2f %s", kpi.current_value, kpi.unit.c_str());
            if (kpi.target_value.has_value()) {
                ImGui::Text("Target: %.2f", kpi.target_value.value());
            }
            ImGui::Text("Trend: %s", models::trend_to_string(kpi.current_trend).c_str());
            if (!kpi.description.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("Description: %s", kpi.description.c_str());
            }
            ImGui::EndTooltip();
        }
        
        // Render children if node is open
        if (has_children && kpi_open) {
            for (const auto& child : child_kpis) {
                if (matches_search(child)) {
                    render_kpi_node(child, depth + 1);
                }
            }
            ImGui::TreePop();
        }
        
        if (depth > 0) {
            ImGui::Unindent(20.0f * depth);
        }
    }

    void render_category_management() {
        ImGui::Text("Create New Category:");
        ImGui::Separator();
        
        // Category form
        ImGui::Text("Name:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##cat_name", category_name_buffer_, sizeof(category_name_buffer_));
        
        ImGui::Text("Description:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextMultiline("##cat_desc", category_description_buffer_, 
                                  sizeof(category_description_buffer_), ImVec2(400, 60));
        
        ImGui::Text("Color:");
        ImGui::SameLine();
        ImGui::ColorEdit3("##cat_color", reinterpret_cast<float*>(&category_color_));
        
        if (ImGui::Button("Create Category", ImVec2(150, 0))) {
            create_category();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Clear Form", ImVec2(100, 0))) {
            clear_form();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Existing Categories:");
        
        // Categories list
        if (categories_.empty()) {
            ImGui::TextColored(colors[5], "No categories defined");
        } else {
            if (ImGui::BeginTable("CategoriesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("KPIs", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableHeadersRow();
                
                for (const auto& category : categories_) {
                    ImGui::TableNextRow();
                    
                    // Color indicator
                    ImGui::TableSetColumnIndex(0);
                    ImVec4 cat_color = parse_hex_color(category.color);
                    ImGui::PushStyleColor(ImGuiCol_Text, cat_color);
                    ImGui::Text("%s", category.name.c_str());
                    ImGui::PopStyleColor();
                    
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", category.description.c_str());
                    
                    ImGui::TableSetColumnIndex(2);
                    auto cat_kpis = get_kpis_in_category(category.id);
                    ImGui::Text("%zu", cat_kpis.size());
                    
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton(("Edit##" + std::to_string(category.id)).c_str())) {
                        populate_category_form(category);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Del##" + std::to_string(category.id)).c_str())) {
                        show_delete_modal_ = true;
                        delete_target_id_ = category.id;
                        is_deleting_category_ = true;
                    }
                }
                
                ImGui::EndTable();
            }
        }
    }

    void render_reorganize_tab() {
        ImGui::Text("Drag and Drop Reorganization:");
        ImGui::TextColored(colors[5], "Use context menus in the Hierarchy Tree to move KPIs between parents.");
        ImGui::Spacing();
        
        ImGui::Text("Quick Actions:");
        ImGui::Separator();
        
        if (ImGui::Button("Flatten All Hierarchies", ImVec2(200, 0))) {
            flatten_all_hierarchies();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move all KPIs to root level (no parent)");
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Auto-Organize by Category", ImVec2(200, 0))) {
            auto_organize_by_category();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically organize KPIs within their categories");
        }
        
        ImGui::Spacing();
        ImGui::Text("Hierarchy Statistics:");
        render_hierarchy_stats();
    }

    void render_move_kpi_modal() {
        if (!show_move_modal_) return;
        
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Move KPI", &show_move_modal_)) {
            auto kpi = get_kpi_by_id(selected_kpi_id_);
            if (!kpi) {
                ImGui::Text("KPI not found");
                if (ImGui::Button("Close")) {
                    show_move_modal_ = false;
                }
                ImGui::EndPopup();
                return;
            }
            
            ImGui::Text("Moving KPI: %s", kpi->name.c_str());
            ImGui::Separator();
            
            ImGui::Text("Select new parent:");
            
            // Parent selection
            if (ImGui::BeginCombo("##new_parent", get_parent_name(move_target_parent_id_).c_str())) {
                // Root option
                if (ImGui::Selectable("(Root - No Parent)", move_target_parent_id_ == -1)) {
                    move_target_parent_id_ = -1;
                }
                
                // KPI options in same category
                auto category_kpis = get_kpis_in_category(kpi->category_id);
                for (const auto& potential_parent : category_kpis) {
                    if (potential_parent.id == selected_kpi_id_) continue; // Can't be parent of itself
                    if (is_descendant(potential_parent.id, selected_kpi_id_)) continue; // Prevent cycles
                    
                    bool is_selected = move_target_parent_id_ == potential_parent.id;
                    if (ImGui::Selectable(potential_parent.name.c_str(), is_selected)) {
                        move_target_parent_id_ = potential_parent.id;
                    }
                }
                ImGui::EndCombo();
            }
            
            ImGui::Spacing();
            if (ImGui::Button("Move", ImVec2(100, 0))) {
                move_kpi();
                show_move_modal_ = false;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                show_move_modal_ = false;
                move_target_parent_id_ = -1;
            }
            
            ImGui::EndPopup();
        }
    }

    void render_delete_confirmation_modal() {
        if (!show_delete_modal_) return;
        
        ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Confirm Delete", &show_delete_modal_)) {
            if (is_deleting_category_) {
                auto category = get_category_by_id(delete_target_id_);
                if (category) {
                    ImGui::Text("Delete category '%s'?", category->name.c_str());
                    auto cat_kpis = get_kpis_in_category(category->id);
                    if (!cat_kpis.empty()) {
                        ImGui::TextColored(colors[4], "Warning: This will also delete %zu KPIs!", cat_kpis.size());
                    }
                }
            } else {
                auto kpi = get_kpi_by_id(delete_target_id_);
                if (kpi) {
                    ImGui::Text("Delete KPI '%s'?", kpi->name.c_str());
                    auto children = get_child_kpis(kpi->id);
                    if (!children.empty()) {
                        ImGui::TextColored(colors[4], "Warning: This will also delete %zu child KPIs!", children.size());
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2(100, 0))) {
                if (is_deleting_category_) {
                    delete_category();
                } else {
                    delete_kpi();
                }
                show_delete_modal_ = false;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                show_delete_modal_ = false;
            }
            
            ImGui::EndPopup();
        }
    }

    void render_status_message() {
        if (!status_message_.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - status_message_time_).count();
            
            if (elapsed < 5) { // Show for 5 seconds
                ImVec4 msg_color = status_is_error_ ? colors[4] : colors[2];
                ImGui::PushStyleColor(ImGuiCol_Text, msg_color);
                ImGui::Text("%s", status_message_.c_str());
                ImGui::PopStyleColor();
            } else {
                status_message_.clear();
            }
        }
    }

    void render_hierarchy_stats() {
        int total_kpis = kpis_.size();
        int root_kpis = 0;
        int max_depth = 0;
        
        for (const auto& kpi : kpis_) {
            if (!kpi.parent_id.has_value()) {
                root_kpis++;
            }
            int depth = get_kpi_depth(kpi.id);
            max_depth = std::max(max_depth, depth);
        }
        
        ImGui::Text("Total KPIs: %d", total_kpis);
        ImGui::Text("Root KPIs: %d", root_kpis);
        ImGui::Text("Maximum Depth: %d", max_depth);
        ImGui::Text("Categories: %zu", categories_.size());
    }

    // Helper methods
    std::vector<models::kpi> get_kpis_in_category(int category_id) {
        std::vector<models::kpi> result;
        for (const auto& kpi : kpis_) {
            if (kpi.category_id == category_id) {
                result.push_back(kpi);
            }
        }
        return result;
    }

    std::vector<models::kpi> get_root_kpis_in_category(int category_id) {
        std::vector<models::kpi> result;
        for (const auto& kpi : kpis_) {
            if (kpi.category_id == category_id && !kpi.parent_id.has_value()) {
                result.push_back(kpi);
            }
        }
        return result;
    }

    std::vector<models::kpi> get_child_kpis(int parent_id) {
        std::vector<models::kpi> result;
        for (const auto& kpi : kpis_) {
            if (kpi.parent_id.has_value() && kpi.parent_id.value() == parent_id) {
                result.push_back(kpi);
            }
        }
        return result;
    }

    const models::kpi* get_kpi_by_id(int id) {
        for (const auto& kpi : kpis_) {
            if (kpi.id == id) {
                return &kpi;
            }
        }
        return nullptr;
    }

    const models::kpi_category* get_category_by_id(int id) {
        for (const auto& category : categories_) {
            if (category.id == id) {
                return &category;
            }
        }
        return nullptr;
    }

    std::string get_category_name(const std::string& id) {
        if (id.empty()) return "All Categories";
        auto it = std::find_if(categories_.begin(), categories_.end(),
            [&id](const models::kpi_category& cat) { return cat.id == id; });
        return it != categories_.end() ? it->name : "Unknown";
    }

    std::string get_parent_name(int id) {
        if (id == -1) return "(Root - No Parent)";
        auto kpi = get_kpi_by_id(id);
        return kpi ? kpi->name : "Unknown";
    }

    bool matches_search(const models::kpi& kpi) {
        if (strlen(search_buffer_) == 0) return true;
        
        std::string search_term = search_buffer_;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        
        auto kpi_name = kpi.name;
        std::transform(kpi_name.begin(), kpi_name.end(), kpi_name.begin(), ::tolower);
        
        auto kpi_desc = kpi.description;
        std::transform(kpi_desc.begin(), kpi_desc.end(), kpi_desc.begin(), ::tolower);
        
        return kpi_name.find(search_term) != std::string::npos ||
               kpi_desc.find(search_term) != std::string::npos;
    }

    ImVec4 get_kpi_status_color(const models::kpi& kpi) {
        if (!kpi.target_value.has_value()) {
            return colors[5]; // Gray for no target
        }
        
        double performance = kpi.current_value / kpi.target_value.value();
        
        if (performance >= 0.9) return colors[2]; // Green
        if (performance >= 0.7) return colors[3]; // Amber
        return colors[4]; // Red
    }

    const char* get_kpi_status_icon(const models::kpi& kpi) {
        if (!kpi.target_value.has_value()) {
            return "○"; // Circle for no target
        }
        
        double performance = kpi.current_value / kpi.target_value.value();
        
        if (performance >= 0.9) return "●"; // Green dot
        if (performance >= 0.7) return "◐"; // Amber half
        return "○"; // Red circle
    }

    bool is_descendant(int potential_parent_id, int kpi_id) {
        // Check if potential_parent_id is a descendant of kpi_id (prevent cycles)
        auto kpi = get_kpi_by_id(potential_parent_id);
        while (kpi && kpi->parent_id.has_value()) {
            if (kpi->parent_id.value() == kpi_id) {
                return true; // Found cycle
            }
            kpi = get_kpi_by_id(kpi->parent_id.value());
        }
        return false;
    }

    int get_kpi_depth(int kpi_id) {
        int depth = 0;
        auto kpi = get_kpi_by_id(kpi_id);
        while (kpi && kpi->parent_id.has_value()) {
            depth++;
            kpi = get_kpi_by_id(kpi->parent_id.value());
        }
        return depth;
    }

    void refresh_data() {
        refresh_future_ = std::async(std::launch::async, [this]() {
            try {
                categories_ = kpi_model_->get_categories().get();
                kpis_ = kpi_model_->get_all_kpis().get();
                
                set_status_message("Data refreshed successfully", false);
            } catch (const std::exception& e) {
                set_status_message(std::format("Failed to refresh data: {}", e.what()), true);
            }
        });
    }

    void create_category() {
        if (strlen(category_name_buffer_) == 0) {
            set_status_message("Category name is required", true);
            return;
        }
        
        models::kpi_category category;
        category.name = category_name_buffer_;
        category.description = category_description_buffer_;
        category.color_r = category_color_.x;
        category.color_g = category_color_.y;
        category.color_b = category_color_.z;
        
        create_category_future_ = std::async(std::launch::async, [this, category]() {
            try {
                auto result = kpi_model_->create_category(category).get();
                if (result > 0) {
                    clear_form();
                    refresh_data();
                    set_status_message("Category created successfully", false);
                } else {
                    set_status_message("Failed to create category", true);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Error creating category: {}", e.what()), true);
            }
        });
    }

    void populate_category_form(const models::kpi_category& category) {
        selected_category_id_ = category.id;
        std::strncpy(category_name_buffer_, category.name.c_str(), sizeof(category_name_buffer_) - 1);
        std::strncpy(category_description_buffer_, category.description.c_str(), sizeof(category_description_buffer_) - 1);
        category_color_ = ImVec4(category.color_r, category.color_g, category.color_b, 1.0f);
    }

    void clear_form() {
        selected_category_id_ = -1;
        category_name_buffer_[0] = '\0';
        category_description_buffer_[0] = '\0';
        category_color_ = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    }

    void move_kpi() {
        move_future_ = std::async(std::launch::async, [this]() {
            try {
                auto kpi = get_kpi_by_id(selected_kpi_id_);
                if (!kpi) return;
                
                models::kpi updated_kpi = *kpi;
                updated_kpi.parent_id = (move_target_parent_id_ == -1) ? 
                                       std::nullopt : 
                                       std::make_optional(move_target_parent_id_);
                
                auto success = kpi_model_->update_kpi(updated_kpi).get();
                if (success) {
                    refresh_data();
                    set_status_message("KPI moved successfully", false);
                } else {
                    set_status_message("Failed to move KPI", true);
                }
                
                selected_kpi_id_ = -1;
                move_target_parent_id_ = -1;
            } catch (const std::exception& e) {
                set_status_message(std::format("Error moving KPI: {}", e.what()), true);
            }
        });
    }

    void delete_kpi() {
        delete_future_ = std::async(std::launch::async, [this]() {
            try {
                auto success = kpi_model_->delete_kpi(delete_target_id_).get();
                if (success) {
                    refresh_data();
                    set_status_message("KPI deleted successfully", false);
                } else {
                    set_status_message("Failed to delete KPI", true);
                }
                delete_target_id_ = -1;
            } catch (const std::exception& e) {
                set_status_message(std::format("Error deleting KPI: {}", e.what()), true);
            }
        });
    }

    void delete_category() {
        delete_future_ = std::async(std::launch::async, [this]() {
            try {
                auto success = kpi_model_->delete_category(delete_target_id_).get();
                if (success) {
                    refresh_data();
                    set_status_message("Category deleted successfully", false);
                } else {
                    set_status_message("Failed to delete category", true);
                }
                delete_target_id_ = -1;
            } catch (const std::exception& e) {
                set_status_message(std::format("Error deleting category: {}", e.what()), true);
            }
        });
    }

    void flatten_all_hierarchies() {
        auto flat_future = std::async(std::launch::async, [this]() {
            try {
                int moved_count = 0;
                for (auto& kpi : kpis_) {
                    if (kpi.parent_id.has_value()) {
                        kpi.parent_id = std::nullopt;
                        kpi_model_->update_kpi(kpi).get();
                        moved_count++;
                    }
                }
                
                if (moved_count > 0) {
                    refresh_data();
                    set_status_message(std::format("Flattened {} KPIs to root level", moved_count), false);
                } else {
                    set_status_message("No KPIs needed flattening", false);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Error flattening hierarchies: {}", e.what()), true);
            }
        });
    }

    void auto_organize_by_category() {
        auto organize_future = std::async(std::launch::async, [this]() {
            try {
                // This is a placeholder for more sophisticated auto-organization
                set_status_message("Auto-organization feature coming soon", false);
            } catch (const std::exception& e) {
                set_status_message(std::format("Error auto-organizing: {}", e.what()), true);
            }
        });
    }

    void check_async_operations() {
        // Check if any futures are ready
        if (refresh_future_.valid() && 
            refresh_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            refresh_future_.get(); // Handle any exceptions
        }
        
        if (create_category_future_.valid() && 
            create_category_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            create_category_future_.get();
        }
        
        if (update_category_future_.valid() && 
            update_category_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            update_category_future_.get();
        }
        
        if (delete_future_.valid() && 
            delete_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            delete_future_.get();
        }
        
        if (move_future_.valid() && 
            move_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            move_future_.get();
        }
    }

    void set_status_message(const std::string& message, bool is_error) {
        status_message_ = message;
        status_is_error_ = is_error;
        status_message_time_ = std::chrono::steady_clock::now();
    }
    
    // Helper function to parse hex color string to ImVec4
    ImVec4 parse_hex_color(const std::string& hex_color) const {
        if (hex_color.empty() || hex_color[0] != '#' || hex_color.length() != 7) {
            // Return default blue color if invalid
            return ImVec4(0.4f, 0.6f, 0.8f, 1.0f);
        }
        
        try {
            unsigned int color = std::stoul(hex_color.substr(1), nullptr, 16);
            float r = ((color >> 16) & 0xFF) / 255.0f;
            float g = ((color >> 8) & 0xFF) / 255.0f;
            float b = (color & 0xFF) / 255.0f;
            return ImVec4(r, g, b, 1.0f);
        } catch (...) {
            // Return default blue color on error
            return ImVec4(0.4f, 0.6f, 0.8f, 1.0f);
        }
    }
};

} // namespace rouen::cards
