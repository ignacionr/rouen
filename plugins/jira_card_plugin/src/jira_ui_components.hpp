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

#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/debug.hpp"
#include "helpers/fetch.hpp"
#include "registrar.hpp"
#include <thread>
#include <mutex>

namespace rouen::cards {

inline void create_card_uri(const std::string& uri) {
    auto svc = registrar::try_get<std::function<void(std::string const&)>>("create_card");
    if (svc) {
        (*svc)(uri);
    } else {
        std::thread([uri]() {
            try {
                http::fetch client;
                std::string payload = std::format(R"({{"uri":"{}"}})", uri);
                client.post("http://127.0.0.1:8081/api/cards", payload, {"Content-Type: application/json"});
            } catch (...) {}
        }).detach();
    }
}

} // namespace rouen::cards

namespace rouen::cards::jira_ui {

inline std::mutex& get_task_queue_mutex() {
    static std::mutex mtx;
    return mtx;
}

inline std::vector<std::function<void()>>& get_pending_task_queue() {
    static std::vector<std::function<void()>> queue;
    return queue;
}

inline void post_main_thread_task(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(get_task_queue_mutex());
    get_pending_task_queue().push_back(std::move(task));
}

inline void poll_async_tasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(get_task_queue_mutex());
        tasks.swap(get_pending_task_queue());
    }
    for (auto const& task : tasks) {
        if (task) {
            task();
        }
    }
}

template <typename T>
concept HasKey = requires(T t) {
    { t.key } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept HasStatus = requires(T t) {
    { t.status.category } -> std::convertible_to<std::string_view>;
    { t.status.name } -> std::convertible_to<std::string_view>;
};

inline std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

template <size_t N>
bool render_input_field(const char* label, char (&buffer)[N], ImGuiInputTextFlags flags = 0) {
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    return ImGui::InputText(std::format("##{}",label).c_str(), buffer, N, flags);
}

template <typename ColorArray>
inline void render_status_text(const std::string& category, const std::string& name, const ColorArray& colors) {
    ImVec4 status_color = colors[5];
    
    if (category == "To Do") {
        status_color = colors[5];
    } else if (category == "In Progress") {
        status_color = colors[8];
    } else if (category == "Done") {
        status_color = colors[9];
    }
    
    ImGui::TextColored(status_color, "%s", name.c_str());
}

template <
    typename T,
    typename ColorArray,
    typename OnClickFunc,
    typename RenderExtraFunc
>
requires HasKey<T>
void render_filterable_table(
    const std::vector<T>& items,
    const char* filter_buffer,
    const ColorArray& colors,
    OnClickFunc on_item_click,
    RenderExtraFunc render_extra_columns
) {
    std::string filter = filter_buffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    
    for (const auto& item : items) {
        std::string title_text;
        if constexpr (requires { item.summary; }) {
            title_text = item.summary;
        } else if constexpr (requires { item.name; }) {
            title_text = item.name;
        }
        std::string summary_lower = title_text;
        std::string key_lower = std::string(item.key);
        std::transform(summary_lower.begin(), summary_lower.end(), summary_lower.begin(), ::tolower);
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        
        if (!filter.empty() && 
            summary_lower.find(filter) == std::string::npos && 
            key_lower.find(filter) == std::string::npos) {
            continue;
        }
        
        ImGui::TableNextRow();
        
        ImGui::TableNextColumn();
        ImGui::TextColored(colors[0], "%s", std::string(item.key).c_str());
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", title_text.c_str());
        
        render_extra_columns(item);
        
        ImGui::TableNextColumn();
        std::string view_btn_id = "View##" + std::string(item.key);
        if (ImGui::Button(view_btn_id.c_str())) {
            on_item_click(item);
        }
    }
}

template <
    typename T,
    typename ColorArray,
    typename OnClickFunc
>
requires HasKey<T>
void render_filterable_table(
    const std::vector<T>& items,
    const char* filter_buffer,
    const ColorArray& colors,
    OnClickFunc on_item_click
) {
    render_filterable_table(items, filter_buffer, colors, on_item_click, 
                           [](const T&){});
}

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
        future_obj = std::move(future),
        on_success = std::move(on_success),
        on_error = std::move(on_error),
        on_complete = std::move(on_complete),
        is_loading_flag
    ]() mutable {
        try {
            auto result = future_obj.get();
            post_main_thread_task([on_success, result = std::move(result), on_complete, is_loading_flag]() {
                if (on_success) {
                    on_success(result);
                }
                if (is_loading_flag) {
                    *is_loading_flag = false;
                }
                if (on_complete) {
                    on_complete();
                }
            });
        } catch (const std::exception& e) {
            std::string err = e.what();
            post_main_thread_task([on_error, err = std::move(err), on_complete, is_loading_flag]() {
                if (on_error) {
                    on_error(err);
                }
                if (is_loading_flag) {
                    *is_loading_flag = false;
                }
                if (on_complete) {
                    on_complete();
                }
            });
        } catch (...) {
            post_main_thread_task([on_error, on_complete, is_loading_flag]() {
                if (on_error) {
                    on_error("Unknown error in async operation");
                }
                if (is_loading_flag) {
                    *is_loading_flag = false;
                }
                if (on_complete) {
                    on_complete();
                }
            });
        }
    }).detach();
}

struct TableRenderers {
    static void setup_issue_table_headers(const char* first_col_title = "Key") {
        ImGui::TableSetupColumn(first_col_title, ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
    }
    
    static void setup_project_table_headers() {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
    }
    
    template <typename ColorArray>
    static void render_status_column(const models::jira_issue& issue, const ColorArray& colors) {
        ImGui::TableNextColumn();
        render_status_text(issue.status.category, issue.status.name, colors);
    }
    
    static void render_project_column(const models::jira_issue& issue) {
        ImGui::TableNextColumn();
        std::string project_key = issue.key.substr(0, issue.key.find('-'));
        ImGui::Text("%s", project_key.c_str());
    }
};

} // namespace rouen::cards::jira_ui
