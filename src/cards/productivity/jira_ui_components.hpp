#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <concepts>
#include <functional>
#include <expected>
#include <type_traits>
#include <future>
#include <optional>

#include "../../helpers/imgui_include.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards::jira_ui {

// Concept for any type that has a key and summary field
template <typename T>
concept HasKeyAndSummary = requires(T t) {
    { t.key } -> std::convertible_to<std::string_view>;
    { t.summary } -> std::convertible_to<std::string_view>;
};

// Concept for any type that has a status field
template <typename T>
concept HasStatus = requires(T t) {
    { t.status.category } -> std::convertible_to<std::string_view>;
    { t.status.name } -> std::convertible_to<std::string_view>;
};

// Helper function to transform string to lowercase
inline std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

// Function to render a labeled input field
template <size_t N>
bool render_input_field(const char* label, char (&buffer)[N], ImGuiInputTextFlags flags = 0) {
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    return ImGui::InputText(std::format("##{}",label).c_str(), buffer, N, flags);
}

// Function to render a color-coded status text
template <typename ColorArray>
inline void render_status_text(const std::string& category, const std::string& name, const ColorArray& colors) {
    ImVec4 status_color = colors[5]; // Default gray
    
    // Map status category to color
    if (category == "To Do") {
        status_color = colors[5]; // Gray
    } else if (category == "In Progress") {
        status_color = colors[8]; // Yellow
    } else if (category == "Done") {
        status_color = colors[9]; // Green
    }
    
    ImGui::TextColored(status_color, "%s", name.c_str());
}

// Function to render a filterable table of items
template <
    typename T,
    typename ColorArray,
    typename OnClickFunc,
    typename RenderExtraFunc
>
requires HasKeyAndSummary<T>
void render_filterable_table(
    const std::vector<T>& items,
    const char* filter_buffer,
    const ColorArray& colors,
    OnClickFunc on_item_click,
    RenderExtraFunc render_extra_columns
) {
    // Apply filter
    std::string filter = filter_buffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    
    // Render each row that passes the filter
    for (const auto& item : items) {
        // Apply filter
        std::string summary_lower = item.summary;
        std::string key_lower = item.key;
        std::transform(summary_lower.begin(), summary_lower.end(), summary_lower.begin(), ::tolower);
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        
        if (!filter.empty() && 
            summary_lower.find(filter) == std::string::npos && 
            key_lower.find(filter) == std::string::npos) {
            continue;
        }
        
        // Item row
        ImGui::TableNextRow();
        
        // Key column
        ImGui::TableNextColumn();
        ImGui::TextColored(colors[0], "%s", item.key.c_str());
        
        // Summary column
        ImGui::TableNextColumn();
        ImGui::Text("%s", item.summary.c_str());
        
        // Let caller render any additional columns
        render_extra_columns(item);
        
        // Actions column
        ImGui::TableNextColumn();
        std::string view_btn_id = "View##" + std::string(item.key);
        if (ImGui::Button(view_btn_id.c_str())) {
            on_item_click(item);
        }
    }
}

// Overload with no extra columns renderer
template <
    typename T,
    typename ColorArray,
    typename OnClickFunc
>
requires HasKeyAndSummary<T>
void render_filterable_table(
    const std::vector<T>& items,
    const char* filter_buffer,
    const ColorArray& colors,
    OnClickFunc on_item_click
) {
    // Using a no-op lambda for the extra columns
    render_filterable_table(items, filter_buffer, colors, on_item_click, 
                           [](const T&){});
}

// Generic async operation handler
template <typename T>
using AsyncResult = std::expected<T, std::string>;

template <typename T>
using AsyncFuture = std::future<AsyncResult<T>>;

template <typename T>
void execute_async(
    std::future<T> future,
    std::function<void(const T&)> on_success,
    std::function<void(const std::string&)> on_error,
    std::function<void()> on_complete = nullptr,
    bool* is_loading_flag = nullptr
) {
    std::thread([
        future = std::move(future),
        on_success,
        on_error,
        on_complete,
        is_loading_flag
    ]() mutable {
        try {
            auto result = future.get();
            on_success(result);
        } catch (const std::exception& e) {
            on_error(e.what());
        }
        
        if (is_loading_flag) {
            *is_loading_flag = false;
        }
        
        if (on_complete) {
            on_complete();
        }
    }).detach();
}

// Common rendering functions for tables
struct TableRenderers {
    // Standard issue table headers setup
    static void setup_issue_table_headers(const char* first_col_title = "Key") {
        ImGui::TableSetupColumn(first_col_title, ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
    }
    
    // Setup for project table headers
    static void setup_project_table_headers() {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
    }
    
    // Status column rendering
    template <typename ColorArray>
    static void render_status_column(const models::jira_issue& issue, const ColorArray& colors) {
        ImGui::TableNextColumn();
        render_status_text(issue.status.category, issue.status.name, colors);
    }
    
    // Project column rendering (extracts project key from issue key)
    static void render_project_column(const models::jira_issue& issue) {
        ImGui::TableNextColumn();
        // Extract project key from issue key (e.g., "PROJ-123" -> "PROJ")
        std::string project_key = issue.key.substr(0, issue.key.find('-'));
        ImGui::Text("%s", project_key.c_str());
    }
};

} // namespace rouen::cards::jira_ui
