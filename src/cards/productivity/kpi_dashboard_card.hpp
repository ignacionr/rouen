#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <format>
#include <chrono>
#include <ctime>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"

// 3. All other includes
#include "../interface/card.hpp"
#include "../../models/kpi_model.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

class kpi_dashboard_card : public card {
public:
    kpi_dashboard_card() {
        // Set custom colors for KPI dashboard theme
        colors[0] = ImVec4{0.2f, 0.6f, 0.8f, 1.0f};  // Primary blue
        colors[1] = ImVec4{0.3f, 0.7f, 0.9f, 0.7f};  // Secondary blue
        get_color(2, ImVec4(0.0f, 0.7f, 0.3f, 1.0f));  // Success green
        get_color(3, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));  // Error red
        get_color(4, ImVec4(0.9f, 0.7f, 0.0f, 1.0f));  // Warning amber
        get_color(5, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));  // Neutral gray
        get_color(6, ImVec4(0.4f, 0.8f, 0.6f, 1.0f));  // Light green
        get_color(7, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));  // Orange
        
        name("KPI Dashboard");
        width = 900.0f;  // Wide for dashboard layout
        requested_fps = 5;
        
        // Initialize KPI model
        kpi_model_ = std::make_shared<models::kpi_model>();
        
        // Load initial data
        refresh_data();
    }
    
    bool render() override {
        return render_window([this]() {
            // Check for async operations completion
            check_async_operations();
            
            if (error_message_.empty()) {
                render_dashboard();
            } else {
                render_error();
            }
        });
    }
    
    std::string get_uri() const override {
        return "kpi-dashboard";
    }

private:
    // Data members
    std::shared_ptr<models::kpi_model> kpi_model_;
    std::vector<models::kpi_category> categories_;
    std::vector<models::kpi> kpis_;
    std::vector<models::kpi_dashboard> dashboards_;
    std::unordered_map<std::string, std::vector<models::kpi_data_point>> kpi_history_;
    
    // UI state
    std::string error_message_;
    std::string selected_dashboard_id_;
    std::string selected_category_id_;
    models::kpi_period selected_period_ = models::kpi_period::MONTHLY;
    bool is_loading_ = false;
    
    // Filter and search
    char search_filter_[256] = {0};
    bool show_only_critical_ = false;
    
    void refresh_data() {
        is_loading_ = true;
        error_message_.clear();
        
        // Load categories
        auto categories_future = kpi_model_->get_categories();
        auto dashboards_future = kpi_model_->get_dashboards();
        
        // Handle categories
        models::kpi_model::execute_async<std::vector<models::kpi_category>>(
            std::move(categories_future),
            [this](const std::vector<models::kpi_category>& result) {
                categories_ = result;
                load_kpis_for_categories();
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading categories: {}", error);
                is_loading_ = false;
            }
        );
        
        // Handle dashboards
        models::kpi_model::execute_async<std::vector<models::kpi_dashboard>>(
            std::move(dashboards_future),
            [this](const std::vector<models::kpi_dashboard>& result) {
                dashboards_ = result;
                if (!dashboards_.empty() && selected_dashboard_id_.empty()) {
                    selected_dashboard_id_ = dashboards_[0].id;
                }
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading dashboards: {}", error);
            }
        );
    }
    
    void load_kpis_for_categories() {
        if (categories_.empty()) return;
        
        // Load KPIs for all categories
        for (const auto& category : categories_) {
            auto kpis_future = kpi_model_->get_kpis_by_category(category.id);
            
            models::kpi_model::execute_async<std::vector<models::kpi>>(
                std::move(kpis_future),
                [this, category_id = category.id](const std::vector<models::kpi>& result) {
                    // Append KPIs to the main list
                    kpis_.insert(kpis_.end(), result.begin(), result.end());
                    
                    // Load historical data for each KPI
                    for (const auto& kpi : result) {
                        load_kpi_history(kpi.id);
                    }
                    
                    is_loading_ = false;
                },
                [this](const std::string& error) {
                    error_message_ = std::format("Error loading KPIs: {}", error);
                    is_loading_ = false;
                }
            );
        }
    }
    
    void load_kpi_history(const std::string& kpi_id) {
        std::string start_date = get_period_start_date(selected_period_);
        auto history_future = kpi_model_->get_kpi_history(kpi_id, start_date);
        
        models::kpi_model::execute_async<std::vector<models::kpi_data_point>>(
            std::move(history_future),
            [this, kpi_id](const std::vector<models::kpi_data_point>& result) {
                kpi_history_[kpi_id] = result;
            },
            [this](const std::string& error) {
                // Non-critical error, just log it
                error_message_ = std::format("Warning: Could not load history for some KPIs: {}", error);
            }
        );
    }
    
    void check_async_operations() {
        // This method would check for completed async operations
        // The actual implementation depends on the async framework used
    }
    
    void render_dashboard() {
        // Dashboard selector
        render_dashboard_selector();
        
        ImGui::Separator();
        
        // Filters and controls
        render_filters_and_controls();
        
        ImGui::Separator();
        
        if (is_loading_) {
            ImGui::TextColored(colors[1], "Loading dashboard data...");
            return;
        }
        
        // Main dashboard content
        if (ImGui::BeginTabBar("DashboardTabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                render_overview_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("KPI Grid")) {
                render_kpi_grid_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Trends")) {
                render_trends_tab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Alerts")) {
                render_alerts_tab();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    
    void render_dashboard_selector() {
        ImGui::TextColored(colors[0], "Dashboard:");
        ImGui::SameLine();
        
        if (ImGui::BeginCombo("##dashboard", 
            selected_dashboard_id_.empty() ? "Select Dashboard" : 
            get_dashboard_name(selected_dashboard_id_).c_str())) {
            
            for (const auto& dashboard : dashboards_) {
                bool is_selected = (selected_dashboard_id_ == dashboard.id);
                if (ImGui::Selectable(dashboard.name.c_str(), is_selected)) {
                    selected_dashboard_id_ = dashboard.id;
                    // Refresh KPIs for this dashboard
                    refresh_data();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            refresh_data();
        }
    }
    
    void render_filters_and_controls() {
        // Search filter
        ImGui::PushItemWidth(200.0f);
        ImGui::InputTextWithHint("##search", "Search KPIs...", search_filter_, sizeof(search_filter_));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        
        // Category filter
        ImGui::PushItemWidth(150.0f);
        if (ImGui::BeginCombo("##category", 
            selected_category_id_.empty() ? "All Categories" : 
            get_category_name(selected_category_id_).c_str())) {
            
            // "All Categories" option
            if (ImGui::Selectable("All Categories", selected_category_id_.empty())) {
                selected_category_id_.clear();
            }
            
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
        
        ImGui::SameLine();
        
        // Time period filter
        ImGui::PushItemWidth(120.0f);
        const char* period_names[] = {"Daily", "Weekly", "Monthly", "Quarterly", "Yearly"};
        int current_period = static_cast<int>(selected_period_);
        if (ImGui::Combo("##period", &current_period, period_names, IM_ARRAYSIZE(period_names))) {
            selected_period_ = static_cast<models::kpi_period>(current_period);
            // Reload history data with new period
            for (const auto& kpi : kpis_) {
                load_kpi_history(kpi.id);
            }
        }
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        ImGui::Checkbox("Critical Only", &show_only_critical_);
    }
    
    void render_overview_tab() {
        // KPI summary cards
        render_kpi_summary_cards();
        
        ImGui::Spacing();
        
        // Category breakdown
        render_category_breakdown();
        
        ImGui::Spacing();
        
        // Recent alerts/notifications
        render_recent_alerts();
    }
    
    void render_kpi_summary_cards() {
        auto filtered_kpis = get_filtered_kpis();
        
        if (filtered_kpis.empty()) {
            ImGui::TextColored(colors[5], "No KPIs found for the selected filters.");
            return;
        }
        
        // Calculate summary statistics
        int total_kpis = static_cast<int>(filtered_kpis.size());
        int green_kpis = 0, amber_kpis = 0, red_kpis = 0;
        
        for (const auto& kpi : filtered_kpis) {
            auto trend = calculate_kpi_trend(kpi);
            switch (trend) {
                case models::kpi_trend::IMPROVING:
                case models::kpi_trend::up:
                    green_kpis++;
                    break;
                case models::kpi_trend::STABLE:
                case models::kpi_trend::stable:
                    green_kpis++;
                    break;
                case models::kpi_trend::DECLINING:
                case models::kpi_trend::down:
                    amber_kpis++;
                    break;
                case models::kpi_trend::CRITICAL:
                case models::kpi_trend::unknown:
                    red_kpis++;
                    break;
            }
        }
        
        // Render summary cards in columns
        ImGui::Columns(4, "SummaryCards", false);
        
        // Total KPIs card
        ImGui::BeginChild("TotalKPIs", ImVec2(0, 80), true);
        ImGui::TextColored(colors[0], "Total KPIs");
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.size() > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::Text("%d", total_kpis);
        if (ImGui::GetIO().Fonts->Fonts.size() > 1) ImGui::PopFont();
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Green KPIs card
        ImGui::BeginChild("GreenKPIs", ImVec2(0, 80), true);
        ImGui::TextColored(colors[2], "On Track");
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.size() > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(colors[2], "%d", green_kpis);
        if (ImGui::GetIO().Fonts->Fonts.size() > 1) ImGui::PopFont();
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Amber KPIs card
        ImGui::BeginChild("AmberKPIs", ImVec2(0, 80), true);
        ImGui::TextColored(colors[4], "At Risk");
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.size() > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(colors[4], "%d", amber_kpis);
        if (ImGui::GetIO().Fonts->Fonts.size() > 1) ImGui::PopFont();
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Red KPIs card
        ImGui::BeginChild("RedKPIs", ImVec2(0, 80), true);
        ImGui::TextColored(colors[3], "Critical");
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.size() > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(colors[3], "%d", red_kpis);
        if (ImGui::GetIO().Fonts->Fonts.size() > 1) ImGui::PopFont();
        ImGui::EndChild();
        
        ImGui::Columns(1);
    }
    
    void render_category_breakdown() {
        ImGui::TextColored(colors[0], "Category Breakdown");
        
        if (categories_.empty()) {
            ImGui::TextColored(colors[5], "No categories available.");
            return;
        }
        
        // Create category statistics
        std::unordered_map<std::string, int> category_counts;
        for (const auto& category : categories_) {
            category_counts[category.id] = 0;
        }
        
        auto filtered_kpis = get_filtered_kpis();
        for (const auto& kpi : filtered_kpis) {
            if (category_counts.find(kpi.category_id) != category_counts.end()) {
                category_counts[kpi.category_id]++;
            }
        }
        
        // Render as a simple bar chart
        for (const auto& category : categories_) {
            int count = category_counts[category.id];
            float percentage = filtered_kpis.empty() ? 0.0f : 
                static_cast<float>(count) / static_cast<float>(filtered_kpis.size());
            
            ImGui::Text("%s:", category.name.c_str());
            ImGui::SameLine();
            
            // Simple progress bar
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[1]);
            ImGui::ProgressBar(percentage, ImVec2(200, 0), std::format("{} KPIs", count).c_str());
            ImGui::PopStyleColor();
        }
    }
    
    void render_recent_alerts() {
        ImGui::TextColored(colors[0], "Recent Alerts");
        
        // Find KPIs that are critical or have recent data points outside targets
        auto filtered_kpis = get_filtered_kpis();
        std::vector<std::pair<models::kpi, std::string>> alerts;
        
        for (const auto& kpi : filtered_kpis) {
            auto trend = calculate_kpi_trend(kpi);
            if (trend == models::kpi_trend::CRITICAL) {
                alerts.emplace_back(kpi, "Critical status");
            } else if (trend == models::kpi_trend::DECLINING) {
                alerts.emplace_back(kpi, "Declining trend");
            }
        }
        
        if (alerts.empty()) {
            ImGui::TextColored(colors[2], "No recent alerts. All KPIs are performing well.");
        } else {
            for (const auto& [kpi, alert_msg] : alerts) {
                ImColor alert_color = (alert_msg == "Critical status") ? 
                    ImColor(colors[3]) : ImColor(colors[4]);
                
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(alert_color, "%s: %s", kpi.name.c_str(), alert_msg.c_str());
            }
        }
    }
    
    void render_kpi_grid_tab() {
        auto filtered_kpis = get_filtered_kpis();
        
        if (filtered_kpis.empty()) {
            ImGui::TextColored(colors[5], "No KPIs found for the selected filters.");
            return;
        }
        
        // Render KPIs in a grid layout
        const int columns = 3;
        int current_column = 0;
        
        for (const auto& kpi : filtered_kpis) {
            if (current_column == 0) {
                ImGui::Columns(columns, "KPIGrid", false);
            }
            
            render_kpi_card(kpi);
            
            current_column = (current_column + 1) % columns;
            if (current_column != 0) {
                ImGui::NextColumn();
            } else {
                ImGui::Columns(1);
                ImGui::Spacing();
            }
        }
        
        if (current_column != 0) {
            ImGui::Columns(1);
        }
    }
    
    void render_kpi_card(const models::kpi& kpi) {
        ImGui::BeginChild(std::format("KPI_{}", kpi.id).c_str(), ImVec2(0, 120), true);
        
        // KPI name
        ImGui::TextColored(colors[0], "%s", kpi.name.c_str());
        
        // Current value
        std::string current_value = format_kpi_value(kpi.current_value, kpi.value_type);
        ImGui::Text("Current: %s", current_value.c_str());
        
        // Target (if applicable)
        if (kpi.target.has_value()) {
            std::string target_value = format_kpi_value(kpi.target->target_value, kpi.value_type);
            ImGui::Text("Target: %s", target_value.c_str());
        }
        
        // Trend indicator
        auto trend = calculate_kpi_trend(kpi);
        ImVec4 trend_color = get_trend_color(trend);
        const char* trend_text = get_trend_text(trend);
        
        ImGui::TextColored(trend_color, "Trend: %s", trend_text);
        
        // Mini sparkline if we have historical data
        auto history_it = kpi_history_.find(kpi.id);
        if (history_it != kpi_history_.end() && !history_it->second.empty()) {
            render_mini_sparkline(history_it->second, kpi.value_type);
        }
        
        ImGui::EndChild();
    }
    
    void render_mini_sparkline(const std::vector<models::kpi_data_point>& data_points, 
                              models::kpi_value_type /* value_type */) {
        if (data_points.size() < 2) return;
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, 30);
        
        // Find min/max values for scaling
        double min_val = data_points[0].value;
        double max_val = data_points[0].value;
        
        for (const auto& point : data_points) {
            min_val = std::min(min_val, point.value);
            max_val = std::max(max_val, point.value);
        }
        
        if (max_val == min_val) max_val = min_val + 1.0; // Avoid division by zero
        
        // Draw sparkline
        for (size_t i = 1; i < data_points.size(); ++i) {
            float x1 = canvas_pos.x + (static_cast<float>(i - 1) / static_cast<float>(data_points.size() - 1)) * canvas_size.x;
            float y1 = canvas_pos.y + canvas_size.y - 
                      (static_cast<float>((data_points[i - 1].value - min_val) / (max_val - min_val)) * canvas_size.y);
            
            float x2 = canvas_pos.x + (static_cast<float>(i) / static_cast<float>(data_points.size() - 1)) * canvas_size.x;
            float y2 = canvas_pos.y + canvas_size.y - 
                      (static_cast<float>((data_points[i].value - min_val) / (max_val - min_val)) * canvas_size.y);
            
            draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), 
                              ImGui::GetColorU32(colors[1]), 1.0f);
        }
        
        ImGui::Dummy(canvas_size);
    }
    
    void render_trends_tab() {
        ImGui::TextColored(colors[0], "KPI Trends Analysis");
        
        // This would contain more detailed trend visualizations
        // For now, show a placeholder
        ImGui::TextColored(colors[5], "Detailed trend analysis coming soon...");
        ImGui::Text("This tab will include:");
        ImGui::BulletText("Interactive charts and graphs");
        ImGui::BulletText("Correlation analysis");
        ImGui::BulletText("Predictive trends");
        ImGui::BulletText("Comparative analysis");
    }
    
    void render_alerts_tab() {
        ImGui::TextColored(colors[0], "KPI Alerts & Notifications");
        
        // This would contain alert management functionality
        ImGui::TextColored(colors[5], "Alert management coming soon...");
        ImGui::Text("This tab will include:");
        ImGui::BulletText("Alert configuration");
        ImGui::BulletText("Notification settings");
        ImGui::BulletText("Alert history");
        ImGui::BulletText("Escalation rules");
    }
    
    void render_error() {
        ImGui::TextColored(colors[3], "Error:");
        ImGui::TextWrapped("%s", error_message_.c_str());
        
        if (ImGui::Button("Retry")) {
            refresh_data();
        }
    }
    
    // Helper methods
    std::vector<models::kpi> get_filtered_kpis() const {
        std::vector<models::kpi> filtered;
        
        std::string search_term = search_filter_;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        
        for (const auto& kpi : kpis_) {
            // Apply category filter
            if (!selected_category_id_.empty() && kpi.category_id != selected_category_id_) {
                continue;
            }
            
            // Apply search filter
            if (!search_term.empty()) {
                std::string kpi_name = kpi.name;
                std::transform(kpi_name.begin(), kpi_name.end(), kpi_name.begin(), ::tolower);
                if (kpi_name.find(search_term) == std::string::npos) {
                    continue;
                }
            }
            
            // Apply critical filter
            if (show_only_critical_) {
                auto trend = calculate_kpi_trend(kpi);
                if (trend != models::kpi_trend::CRITICAL) {
                    continue;
                }
            }
            
            filtered.push_back(kpi);
        }
        
        return filtered;
    }
    
    models::kpi_trend calculate_kpi_trend(const models::kpi& kpi) const {
        // Simple trend calculation based on current value vs target
        if (!kpi.target.has_value()) {
            return models::kpi_trend::STABLE;
        }
        
        const auto& target = kpi.target.value();
        double current = kpi.current_value;
        double target_val = target.target_value;
        double threshold = target_val * 0.1; // 10% threshold
        
        switch (target.target_type) {
            case models::kpi_target_type::MINIMUM:
                if (current >= target_val) return models::kpi_trend::IMPROVING;
                if (current >= target_val - threshold) return models::kpi_trend::STABLE;
                if (current >= target_val - (threshold * 2)) return models::kpi_trend::DECLINING;
                return models::kpi_trend::CRITICAL;
                
            case models::kpi_target_type::MAXIMUM:
                if (current <= target_val) return models::kpi_trend::IMPROVING;
                if (current <= target_val + threshold) return models::kpi_trend::STABLE;
                if (current <= target_val + (threshold * 2)) return models::kpi_trend::DECLINING;
                return models::kpi_trend::CRITICAL;
                
            case models::kpi_target_type::EXACT:
                double diff = std::abs(current - target_val);
                if (diff <= threshold * 0.5) return models::kpi_trend::IMPROVING;
                if (diff <= threshold) return models::kpi_trend::STABLE;
                if (diff <= threshold * 2) return models::kpi_trend::DECLINING;
                return models::kpi_trend::CRITICAL;
        }
        
        return models::kpi_trend::STABLE;
    }
    
    ImVec4 get_trend_color(models::kpi_trend trend) const {
        switch (trend) {
            case models::kpi_trend::IMPROVING: return colors[2]; // Green
            case models::kpi_trend::STABLE: return colors[6]; // Light green
            case models::kpi_trend::DECLINING: return colors[4]; // Amber
            case models::kpi_trend::CRITICAL: return colors[3]; // Red
            default: return colors[5]; // Gray
        }
    }
    
    const char* get_trend_text(models::kpi_trend trend) const {
        switch (trend) {
            case models::kpi_trend::IMPROVING: return "Improving";
            case models::kpi_trend::STABLE: return "Stable";
            case models::kpi_trend::DECLINING: return "Declining";
            case models::kpi_trend::CRITICAL: return "Critical";
            default: return "Unknown";
        }
    }
    
    std::string format_kpi_value(double value, models::kpi_value_type type) const {
        switch (type) {
            case models::kpi_value_type::count:
            case models::kpi_value_type::number:
                return std::format("{}", static_cast<long long>(value));
            case models::kpi_value_type::percentage:
                return std::format("{:.1f}%", value);
            case models::kpi_value_type::currency:
                return std::format("${:.2f}", value);
            case models::kpi_value_type::ratio:
                return std::format("{:.1f}:1", value);
            case models::kpi_value_type::boolean:
                return value > 0.5 ? "Yes" : "No";
            case models::kpi_value_type::text:
                return std::format("{:.2f}", value); // Fallback for text values
            default:
                return std::format("{:.2f}", value);
        }
    }
    
    std::string get_dashboard_name(const std::string& dashboard_id) const {
        auto it = std::find_if(dashboards_.begin(), dashboards_.end(),
            [&dashboard_id](const models::kpi_dashboard& d) { return d.id == dashboard_id; });
        return it != dashboards_.end() ? it->name : "Unknown Dashboard";
    }
    
    std::string get_category_name(const std::string& category_id) const {
        auto it = std::find_if(categories_.begin(), categories_.end(),
            [&category_id](const models::kpi_category& c) { return c.id == category_id; });
        return it != categories_.end() ? it->name : "Unknown Category";
    }
    
    std::string get_period_start_date(models::kpi_period period) const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        switch (period) {
            case models::kpi_period::DAILY:
            case models::kpi_period::daily:
                tm.tm_mday -= 30; // Last 30 days
                break;
            case models::kpi_period::WEEKLY:
            case models::kpi_period::weekly:
                tm.tm_mday -= 7 * 12; // Last 12 weeks
                break;
            case models::kpi_period::MONTHLY:
            case models::kpi_period::monthly:
                tm.tm_mon -= 12; // Last 12 months
                break;
            case models::kpi_period::QUARTERLY:
            case models::kpi_period::quarterly:
                tm.tm_mon -= 24; // Last 24 months (8 quarters)
                break;
            case models::kpi_period::YEARLY:
            case models::kpi_period::yearly:
                tm.tm_year -= 5; // Last 5 years
                break;
            default:
                tm.tm_mon -= 6; // Default to 6 months
                break;
        }
        
        auto adjusted_time = std::mktime(&tm);
        
        // Format as ISO 8601 date string
        auto adjusted_tm = *std::localtime(&adjusted_time);
        return std::format("{:04d}-{:02d}-{:02d}", 
                          adjusted_tm.tm_year + 1900, 
                          adjusted_tm.tm_mon + 1, 
                          adjusted_tm.tm_mday);
    }
};

} // namespace rouen::cards
