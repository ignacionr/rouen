#pragma once

#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include <fstream>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../models/kpi_model.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class kpi_reports_card : public card {
public:
    kpi_reports_card() {
        // Set custom colors for KPI reports theme
        colors[0] = {0.6f, 0.3f, 0.8f, 1.0f}; // Purple primary color
        colors[1] = {0.7f, 0.4f, 0.9f, 0.7f}; // Light purple secondary color
        
        // Additional colors for report status
        get_color(2, ImVec4(0.2f, 0.7f, 0.2f, 1.0f)); // Green for success
        get_color(3, ImVec4(0.8f, 0.6f, 0.2f, 1.0f)); // Amber for warnings
        get_color(4, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for errors
        get_color(5, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Gray for disabled/secondary text
        
        name("KPI Reports");
        width = 800.0f;
        requested_fps = 2; // Lower refresh rate for reports
        
        // Initialize KPI model
        kpi_model_ = std::make_shared<models::kpi_model>();
        
        // Load initial data
        refresh_data();
        
        // Initialize report settings
        clear_report_form();
    }

    bool render() override {
        render_header();
        ImGui::Separator();
        
        // Check for async operations completion
        check_async_operations();
        
        if (ImGui::BeginTabBar("KPIReportsTabs")) {
            if (ImGui::BeginTabItem("Generate Reports")) {
                render_generate_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Saved Reports")) {
                render_saved_reports_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Scheduled Reports")) {
                render_scheduled_reports_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Export/Import")) {
                render_export_import_tab();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        // Render modals
        render_preview_modal();
        render_schedule_modal();
        
        return true;
    }

    std::string get_uri() const override {
        return "kpi-reports";
    }

private:
    std::shared_ptr<models::kpi_model> kpi_model_;
    
    // Data
    std::vector<models::kpi_category> categories_;
    std::vector<models::kpi> kpis_;
    std::vector<models::kpi_report> saved_reports_;
    
    // Report generation settings
    char report_name_buffer_[256] = {0};
    char report_description_buffer_[512] = {0};
    std::vector<int> selected_kpi_ids_;
    std::vector<int> selected_category_ids_;
    models::time_period report_period_ = models::time_period::LAST_30_DAYS;
    models::report_format report_format_ = models::report_format::PDF;
    bool include_charts_ = true;
    bool include_trends_ = true;
    bool include_targets_ = true;
    bool include_historical_data_ = false;
    
    // Date range for custom periods
    char start_date_buffer_[32] = {0};
    char end_date_buffer_[32] = {0};
    
    // UI state
    bool show_preview_modal_ = false;
    bool show_schedule_modal_ = false;
    int selected_report_id_ = -1;
    std::string generated_report_content_;
    
    // Scheduling settings
    char schedule_name_buffer_[256] = {0};
    models::report_frequency schedule_frequency_ = models::report_frequency::WEEKLY;
    char schedule_email_buffer_[256] = {0};
    bool schedule_enabled_ = true;
    
    // Export/Import
    char export_path_buffer_[512] = {0};
    char import_path_buffer_[512] = {0};
    
    // Async operation tracking
    std::future<void> refresh_future_;
    std::future<void> generate_future_;
    std::future<void> save_future_;
    std::future<void> delete_future_;
    std::future<void> export_future_;
    std::future<void> import_future_;
    
    // Status messages
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_time_;
    bool status_is_error_ = false;

    void render_header() {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[0]);
        ImGui::Text("KPI Reports Management");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        if (ImGui::Button("Refresh Data", ImVec2(140, 0))) {
            refresh_data();
        }
        
        // Status message
        render_status_message();
    }

    void render_generate_tab() {
        ImGui::Text("Create New Report:");
        ImGui::Separator();
        
        // Report basic info
        ImGui::Text("Report Name:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##report_name", report_name_buffer_, sizeof(report_name_buffer_));
        
        ImGui::Text("Description:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextMultiline("##report_desc", report_description_buffer_, 
                                  sizeof(report_description_buffer_), ImVec2(400, 60));
        
        // Time period selection
        ImGui::Spacing();
        ImGui::Text("Time Period:");
        const char* period_items[] = {"Last 7 Days", "Last 30 Days", "Last 90 Days", "Last Year", "Custom Range"};
        int current_period = static_cast<int>(report_period_);
        if (ImGui::Combo("##time_period", &current_period, period_items, IM_ARRAYSIZE(period_items))) {
            report_period_ = static_cast<models::time_period>(current_period);
        }
        
        // Custom date range if selected
        if (report_period_ == models::time_period::CUSTOM) {
            ImGui::Text("Start Date (YYYY-MM-DD):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("##start_date", start_date_buffer_, sizeof(start_date_buffer_));
            
            ImGui::SameLine();
            ImGui::Text("End Date (YYYY-MM-DD):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("##end_date", end_date_buffer_, sizeof(end_date_buffer_));
        }
        
        // Format selection
        ImGui::Spacing();
        ImGui::Text("Export Format:");
        const char* format_items[] = {"PDF", "Excel", "CSV", "JSON"};
        int current_format = static_cast<int>(report_format_);
        if (ImGui::Combo("##report_format", &current_format, format_items, IM_ARRAYSIZE(format_items))) {
            report_format_ = static_cast<models::report_format>(current_format);
        }
        
        // Content options
        ImGui::Spacing();
        ImGui::Text("Include in Report:");
        ImGui::Checkbox("Charts and Visualizations", &include_charts_);
        ImGui::SameLine();
        ImGui::Checkbox("Trend Analysis", &include_trends_);
        ImGui::Checkbox("Target vs Actual", &include_targets_);
        ImGui::SameLine();
        ImGui::Checkbox("Historical Data", &include_historical_data_);
        
        // KPI Selection
        ImGui::Spacing();
        ImGui::Separator();
        render_kpi_selection();
        
        // Action buttons
        ImGui::Spacing();
        if (ImGui::Button("Preview Report", ImVec2(150, 0))) {
            generate_preview();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Generate & Save", ImVec2(150, 0))) {
            generate_and_save_report();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Schedule Report", ImVec2(150, 0))) {
            show_schedule_modal_ = true;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Clear Form", ImVec2(100, 0))) {
            clear_report_form();
        }
    }

    void render_kpi_selection() {
        ImGui::Text("Select KPIs and Categories:");
        
        if (ImGui::BeginChild("KPISelection", ImVec2(0, 200), true)) {
            // Category-based selection
            ImGui::Text("Select by Category:");
            for (const auto& category : categories_) {
                bool is_category_selected = std::find(selected_category_ids_.begin(), 
                                                     selected_category_ids_.end(), 
                                                     category.id) != selected_category_ids_.end();
                
                if (ImGui::Checkbox(("##cat_" + std::to_string(category.id)).c_str(), &is_category_selected)) {
                    if (is_category_selected) {
                        selected_category_ids_.push_back(category.id);
                        // Add all KPIs from this category
                        for (const auto& kpi : kpis_) {
                            if (kpi.category_id == category.id) {
                                if (std::find(selected_kpi_ids_.begin(), selected_kpi_ids_.end(), kpi.id) == selected_kpi_ids_.end()) {
                                    selected_kpi_ids_.push_back(kpi.id);
                                }
                            }
                        }
                    } else {
                        selected_category_ids_.erase(
                            std::remove(selected_category_ids_.begin(), selected_category_ids_.end(), category.id),
                            selected_category_ids_.end());
                        // Remove all KPIs from this category
                        for (const auto& kpi : kpis_) {
                            if (kpi.category_id == category.id) {
                                selected_kpi_ids_.erase(
                                    std::remove(selected_kpi_ids_.begin(), selected_kpi_ids_.end(), kpi.id),
                                    selected_kpi_ids_.end());
                            }
                        }
                    }
                }
                
                ImGui::SameLine();
                ImVec4 cat_color = {category.color_r, category.color_g, category.color_b, 1.0f};
                ImGui::PushStyleColor(ImGuiCol_Text, cat_color);
                ImGui::Text("%s", category.name.c_str());
                ImGui::PopStyleColor();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Select Individual KPIs:");
            
            // Individual KPI selection
            for (const auto& category : categories_) {
                ImVec4 cat_color = {category.color_r, category.color_g, category.color_b, 1.0f};
                ImGui::PushStyleColor(ImGuiCol_Text, cat_color);
                if (ImGui::TreeNode(("kpis_" + std::to_string(category.id)).c_str(), "%s", category.name.c_str())) {
                    ImGui::PopStyleColor();
                    
                    for (const auto& kpi : kpis_) {
                        if (kpi.category_id == category.id) {
                            bool is_kpi_selected = std::find(selected_kpi_ids_.begin(), 
                                                             selected_kpi_ids_.end(), 
                                                             kpi.id) != selected_kpi_ids_.end();
                            
                            if (ImGui::Checkbox(("##kpi_" + std::to_string(kpi.id)).c_str(), &is_kpi_selected)) {
                                if (is_kpi_selected) {
                                    selected_kpi_ids_.push_back(kpi.id);
                                } else {
                                    selected_kpi_ids_.erase(
                                        std::remove(selected_kpi_ids_.begin(), selected_kpi_ids_.end(), kpi.id),
                                        selected_kpi_ids_.end());
                                }
                            }
                            
                            ImGui::SameLine();
                            ImGui::Text("%s (%s)", kpi.name.c_str(), kpi.unit.c_str());
                        }
                    }
                    
                    ImGui::TreePop();
                } else {
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndChild();
        
        // Selection summary
        ImGui::Text("Selected: %zu KPIs from %zu categories", 
                   selected_kpi_ids_.size(), selected_category_ids_.size());
    }

    void render_saved_reports_tab() {
        ImGui::Text("Saved Reports:");
        ImGui::Separator();
        
        if (saved_reports_.empty()) {
            ImGui::TextColored(colors[5], "No saved reports found");
            return;
        }
        
        if (ImGui::BeginTable("SavedReportsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("KPIs", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableHeadersRow();
            
            for (const auto& report : saved_reports_) {
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", report.name.c_str());
                
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", report.description.c_str());
                
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%zu", report.included_kpi_ids.size());
                
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", format_timestamp(report.created_at).c_str());
                
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s", models::report_format_to_string(report.format).c_str());
                
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton(("View##" + std::to_string(report.id)).c_str())) {
                    view_saved_report(report.id);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(("Regenerate##" + std::to_string(report.id)).c_str())) {
                    regenerate_report(report.id);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(("Delete##" + std::to_string(report.id)).c_str())) {
                    delete_saved_report(report.id);
                }
            }
            
            ImGui::EndTable();
        }
    }

    void render_scheduled_reports_tab() {
        ImGui::Text("Scheduled Reports:");
        ImGui::Separator();
        
        ImGui::Text("Create New Schedule:");
        ImGui::Spacing();
        
        // Schedule form
        ImGui::Text("Schedule Name:");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##schedule_name", schedule_name_buffer_, sizeof(schedule_name_buffer_));
        
        ImGui::Text("Frequency:");
        ImGui::SameLine();
        const char* freq_items[] = {"Daily", "Weekly", "Monthly", "Quarterly"};
        int current_freq = static_cast<int>(schedule_frequency_);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("##frequency", &current_freq, freq_items, IM_ARRAYSIZE(freq_items))) {
            schedule_frequency_ = static_cast<models::report_frequency>(current_freq);
        }
        
        ImGui::Text("Email Recipients:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##email_recipients", schedule_email_buffer_, sizeof(schedule_email_buffer_));
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Separate multiple emails with commas");
        }
        
        ImGui::Checkbox("Schedule Enabled", &schedule_enabled_);
        
        if (ImGui::Button("Create Schedule", ImVec2(150, 0))) {
            create_schedule();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Existing Schedules:");
        ImGui::TextColored(colors[5], "Schedule management will be implemented in future version");
    }

    void render_export_import_tab() {
        ImGui::Text("Export KPI Data:");
        ImGui::Separator();
        
        ImGui::Text("Export Path:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##export_path", export_path_buffer_, sizeof(export_path_buffer_));
        ImGui::SameLine();
        if (ImGui::Button("Browse##export")) {
            // File dialog would go here
            set_status_message("File dialog not implemented yet", false);
        }
        
        if (ImGui::Button("Export All KPI Data", ImVec2(200, 0))) {
            export_all_data();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export Selected KPIs", ImVec2(200, 0))) {
            export_selected_data();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Import KPI Data:");
        
        ImGui::Text("Import Path:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##import_path", import_path_buffer_, sizeof(import_path_buffer_));
        ImGui::SameLine();
        if (ImGui::Button("Browse##import")) {
            // File dialog would go here
            set_status_message("File dialog not implemented yet", false);
        }
        
        if (ImGui::Button("Import KPI Data", ImVec2(200, 0))) {
            import_data();
        }
        
        ImGui::Spacing();
        ImGui::Text("Supported formats: JSON, CSV, Excel");
        ImGui::TextColored(colors[3], "Warning: Import will overwrite existing data with same IDs");
    }

    void render_preview_modal() {
        if (!show_preview_modal_) return;
        
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Report Preview", &show_preview_modal_, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Actions")) {
                    if (ImGui::MenuItem("Save Report")) {
                        save_current_report();
                    }
                    if (ImGui::MenuItem("Export to File")) {
                        export_current_report();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            
            // Preview content
            if (ImGui::BeginChild("PreviewContent", ImVec2(0, -30), true)) {
                if (generated_report_content_.empty()) {
                    ImGui::Text("Generating preview...");
                } else {
                    ImGui::TextWrapped("%s", generated_report_content_.c_str());
                }
            }
            ImGui::EndChild();
            
            if (ImGui::Button("Close", ImVec2(100, 0))) {
                show_preview_modal_ = false;
            }
            
            ImGui::EndPopup();
        }
    }

    void render_schedule_modal() {
        if (!show_schedule_modal_) return;
        
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Schedule Report", &show_schedule_modal_)) {
            ImGui::Text("Schedule this report for regular generation:");
            ImGui::Separator();
            
            // Use the same schedule form as in the scheduled reports tab
            ImGui::Text("Schedule Name:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##modal_schedule_name", schedule_name_buffer_, sizeof(schedule_name_buffer_));
            
            ImGui::Text("Frequency:");
            const char* freq_items[] = {"Daily", "Weekly", "Monthly", "Quarterly"};
            int current_freq = static_cast<int>(schedule_frequency_);
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo("##modal_frequency", &current_freq, freq_items, IM_ARRAYSIZE(freq_items))) {
                schedule_frequency_ = static_cast<models::report_frequency>(current_freq);
            }
            
            ImGui::Text("Email Recipients:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##modal_email_recipients", schedule_email_buffer_, sizeof(schedule_email_buffer_));
            
            ImGui::Checkbox("Schedule Enabled", &schedule_enabled_);
            
            ImGui::Spacing();
            if (ImGui::Button("Create Schedule", ImVec2(150, 0))) {
                create_schedule();
                show_schedule_modal_ = false;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                show_schedule_modal_ = false;
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

    // Helper methods
    std::string format_timestamp(const std::chrono::system_clock::time_point& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        auto tm = *std::localtime(&time_t);
        return std::format("{:04d}-{:02d}-{:02d}", 
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }

    void clear_report_form() {
        report_name_buffer_[0] = '\0';
        report_description_buffer_[0] = '\0';
        selected_kpi_ids_.clear();
        selected_category_ids_.clear();
        report_period_ = models::time_period::LAST_30_DAYS;
        report_format_ = models::report_format::PDF;
        include_charts_ = true;
        include_trends_ = true;
        include_targets_ = true;
        include_historical_data_ = false;
        start_date_buffer_[0] = '\0';
        end_date_buffer_[0] = '\0';
    }

    void refresh_data() {
        refresh_future_ = std::async(std::launch::async, [this]() {
            try {
                categories_ = kpi_model_->get_categories().get();
                kpis_ = kpi_model_->get_all_kpis().get();
                saved_reports_ = kpi_model_->get_reports().get();
                
                set_status_message("Data refreshed successfully", false);
            } catch (const std::exception& e) {
                set_status_message(std::format("Failed to refresh data: {}", e.what()), true);
            }
        });
    }

    void generate_preview() {
        if (selected_kpi_ids_.empty()) {
            set_status_message("Please select at least one KPI for the report", true);
            return;
        }
        
        generate_future_ = std::async(std::launch::async, [this]() {
            try {
                // Generate report content
                generated_report_content_ = generate_report_content();
                show_preview_modal_ = true;
                set_status_message("Preview generated successfully", false);
            } catch (const std::exception& e) {
                set_status_message(std::format("Failed to generate preview: {}", e.what()), true);
            }
        });
    }

    void generate_and_save_report() {
        if (strlen(report_name_buffer_) == 0) {
            set_status_message("Report name is required", true);
            return;
        }
        
        if (selected_kpi_ids_.empty()) {
            set_status_message("Please select at least one KPI for the report", true);
            return;
        }
        
        save_future_ = std::async(std::launch::async, [this]() {
            try {
                // Create report object
                models::kpi_report report;
                report.name = report_name_buffer_;
                report.description = report_description_buffer_;
                report.included_kpi_ids = selected_kpi_ids_;
                report.time_period = report_period_;
                report.format = report_format_;
                report.created_at = std::chrono::system_clock::now();
                
                // Save to database
                auto report_id = kpi_model_->create_report(report).get();
                if (report_id > 0) {
                    // Generate and save content
                    auto content = generate_report_content();
                    save_report_file(report_id, content);
                    
                    refresh_data();
                    clear_report_form();
                    set_status_message("Report generated and saved successfully", false);
                } else {
                    set_status_message("Failed to save report", true);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Error generating report: {}", e.what()), true);
            }
        });
    }

    std::string generate_report_content() {
        std::string content;
        
        // Report header
        content += std::format("KPI Report: {}\n", report_name_buffer_);
        content += std::format("Generated: {}\n", format_timestamp(std::chrono::system_clock::now()));
        content += std::format("Period: {}\n", models::time_period_to_string(report_period_));
        content += "\n";
        
        if (!std::string(report_description_buffer_).empty()) {
            content += std::format("Description: {}\n\n", report_description_buffer_);
        }
        
        // KPI summaries
        content += "KPI Summary:\n";
        content += "============\n\n";
        
        for (int kpi_id : selected_kpi_ids_) {
            auto kpi_it = std::find_if(kpis_.begin(), kpis_.end(), 
                                      [kpi_id](const auto& k) { return k.id == kpi_id; });
            if (kpi_it != kpis_.end()) {
                const auto& kpi = *kpi_it;
                
                content += std::format("KPI: {}\n", kpi.name);
                content += std::format("Current Value: {:.2f} {}\n", kpi.current_value, kpi.unit);
                
                if (kpi.target_value.has_value()) {
                    double performance = (kpi.current_value / kpi.target_value.value()) * 100.0;
                    content += std::format("Target: {:.2f} (Performance: {:.1f}%)\n", 
                                          kpi.target_value.value(), performance);
                }
                
                content += std::format("Trend: {}\n", models::trend_to_string(kpi.current_trend));
                
                if (!kpi.description.empty()) {
                    content += std::format("Description: {}\n", kpi.description);
                }
                
                content += "\n";
            }
        }
        
        // Add sections based on options
        if (include_trends_) {
            content += "\nTrend Analysis:\n";
            content += "===============\n";
            content += "Detailed trend analysis would be generated here...\n\n";
        }
        
        if (include_targets_) {
            content += "\nTarget Performance:\n";
            content += "==================\n";
            content += "Target vs actual performance analysis would be generated here...\n\n";
        }
        
        if (include_historical_data_) {
            content += "\nHistorical Data:\n";
            content += "===============\n";
            content += "Historical data points would be included here...\n\n";
        }
        
        content += "\n--- End of Report ---\n";
        return content;
    }

    void save_report_file(int report_id, const std::string& content) {
        std::string filename = std::format("kpi_report_{}.txt", report_id);
        std::ofstream file(filename);
        if (file.is_open()) {
            file << content;
            file.close();
        }
    }

    void view_saved_report(int report_id) {
        selected_report_id_ = report_id;
        // Load and display saved report
        set_status_message("Loading saved report...", false);
    }

    void regenerate_report(int report_id) {
        // Find the report and regenerate with current data
        auto report_it = std::find_if(saved_reports_.begin(), saved_reports_.end(),
                                     [report_id](const auto& r) { return r.id == report_id; });
        if (report_it != saved_reports_.end()) {
            const auto& report = *report_it;
            selected_kpi_ids_ = report.included_kpi_ids;
            report_period_ = report.time_period;
            report_format_ = report.format;
            
            generate_and_save_report();
        }
    }

    void delete_saved_report(int report_id) {
        delete_future_ = std::async(std::launch::async, [this, report_id]() {
            try {
                auto success = kpi_model_->delete_report(report_id).get();
                if (success) {
                    refresh_data();
                    set_status_message("Report deleted successfully", false);
                } else {
                    set_status_message("Failed to delete report", true);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Error deleting report: {}", e.what()), true);
            }
        });
    }

    void create_schedule() {
        set_status_message("Schedule creation not yet implemented", false);
    }

    void save_current_report() {
        set_status_message("Saving current report...", false);
        generate_and_save_report();
    }

    void export_current_report() {
        set_status_message("Export to file not yet implemented", false);
    }

    void export_all_data() {
        export_future_ = std::async(std::launch::async, [this]() {
            try {
                auto result = kpi_model_->export_to_json().get();
                if (!result.empty()) {
                    std::string filename = export_path_buffer_;
                    if (filename.empty()) {
                        filename = "kpi_export.json";
                    }
                    
                    std::ofstream file(filename);
                    if (file.is_open()) {
                        file << result;
                        file.close();
                        set_status_message(std::format("Data exported to {}", filename), false);
                    } else {
                        set_status_message("Failed to write export file", true);
                    }
                } else {
                    set_status_message("No data to export", true);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Export failed: {}", e.what()), true);
            }
        });
    }

    void export_selected_data() {
        set_status_message("Selected data export not yet implemented", false);
    }

    void import_data() {
        import_future_ = std::async(std::launch::async, [this]() {
            try {
                std::string filename = import_path_buffer_;
                if (filename.empty()) {
                    set_status_message("Please specify import file path", true);
                    return;
                }
                
                std::ifstream file(filename);
                if (!file.is_open()) {
                    set_status_message("Cannot open import file", true);
                    return;
                }
                
                std::string content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
                file.close();
                
                auto success = kpi_model_->import_from_json(content).get();
                if (success) {
                    refresh_data();
                    set_status_message("Data imported successfully", false);
                } else {
                    set_status_message("Import failed", true);
                }
            } catch (const std::exception& e) {
                set_status_message(std::format("Import error: {}", e.what()), true);
            }
        });
    }

    void check_async_operations() {
        // Check if any futures are ready
        if (refresh_future_.valid() && 
            refresh_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            refresh_future_.get(); // Handle any exceptions
        }
        
        if (generate_future_.valid() && 
            generate_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            generate_future_.get();
        }
        
        if (save_future_.valid() && 
            save_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            save_future_.get();
        }
        
        if (delete_future_.valid() && 
            delete_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            delete_future_.get();
        }
        
        if (export_future_.valid() && 
            export_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            export_future_.get();
        }
        
        if (import_future_.valid() && 
            import_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            import_future_.get();
        }
    }

    void set_status_message(const std::string& message, bool is_error) {
        status_message_ = message;
        status_is_error_ = is_error;
        status_message_time_ = std::chrono::steady_clock::now();
    }
};

} // namespace rouen::cards
