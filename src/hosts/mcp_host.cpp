#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../helpers/glaze_include.hpp"

// 2. Libraries used in the project, in alphabetic order
#include "config_service.hpp"
#include "ytdlp_service.hpp"

// 3. All other includes
#include "debug.hpp"
#include "mcp_host.hpp"
#include "models/contacts/contact.hpp"
#include "platform_utils.hpp"
#include "process_helper.hpp"
#include "string_helper.hpp"
#include "fetch.hpp"
#include "../helpers/presence_service.hpp"
#include "../registrar.hpp"
#include "../models/notes/notes_repository.hpp"
#include "../models/series/series_repository.hpp"
#include "../models/adaptive_cards/adaptive_cards_repository.hpp"
#include "../models/contacts/contacts_repository.hpp"
#include "../models/calendar/calendar_fetcher.hpp"
#include "universal_sync_host.hpp"
#include "persona_manager.hpp"
#include "../helpers/theme_manager.hpp"
#include <SDL3/SDL_video.h>
#include "../cards/information/rss.hpp"
#include "../cards/interface/card.hpp"
#include "../cards/interface/factory.hpp"
#include "../helpers/adlib_engine.hpp"
#include "../helpers/card_render_metrics.hpp"
#include "../helpers/deferred_operations.hpp"
#include "../helpers/media_player.hpp"
#include "../helpers/ui_automation_explorer.hpp"
#include "../hosts/process_host.hpp"
#include "../hosts/video_feed_host.hpp"
#include "../helpers/toolchain_service.hpp"
#include "../helpers/syntax_checker.hpp"
#include "../helpers/code_indexer.hpp"
#include "../helpers/code_editor_service.hpp"
#include "../helpers/conventional_commit.hpp"

namespace rouen::hosts {

struct mcp_run_unit_tests_params {
    std::string target_test;
    std::string filter;
    int timeout_seconds{120};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_run_unit_tests_params;
        static constexpr auto value = glz::object(
            "target_test", &T::target_test,
            "filter", &T::filter,
            "timeout_seconds", &T::timeout_seconds,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_build_and_deploy_params {
    std::string target{"rouen"};
    bool quick_build{true};
    bool deploy{true};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_build_and_deploy_params;
        static constexpr auto value = glz::object(
            "target", &T::target,
            "quick_build", &T::quick_build,
            "deploy", &T::deploy,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_update_inbox_item_params {
    std::string file_path;
    std::string status;
    std::string resolution_summary;
    std::string workspace_dir;
    struct glaze {
        using T = mcp_update_inbox_item_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "status", &T::status,
            "resolution_summary", &T::resolution_summary,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_announce_completion_params {
    std::string message;
    struct glaze {
        using T = mcp_announce_completion_params;
        static constexpr auto value = glz::object(
            "message", &T::message
        );
    };
};

struct mcp_code_generate_conventional_commit_params {
    std::string diff_context;
    std::string status_context;
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_generate_conventional_commit_params;
        static constexpr auto value = glz::object(
            "diff_context", &T::diff_context,
            "status_context", &T::status_context,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_discover_toolchain_params {
    std::string workspace_path;
    struct glaze {
        using T = mcp_code_discover_toolchain_params;
        static constexpr auto value = glz::object(
            "workspace_path", &T::workspace_path
        );
    };
};

struct mcp_code_check_syntax_params {
    std::string file_path;
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_check_syntax_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_clear_diagnostics_params {
    std::string file_path;
    struct glaze {
        using T = mcp_code_clear_diagnostics_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path
        );
    };
};

struct mcp_code_search_params {
    std::string query;
    int max_results{50};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_search_params;
        static constexpr auto value = glz::object(
            "query", &T::query,
            "max_results", &T::max_results,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_find_symbol_params {
    std::string name;
    std::string kind;
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_find_symbol_params;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "kind", &T::kind,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_index_workspace_params {
    std::string workspace_dir;
    bool force{false};
    struct glaze {
        using T = mcp_code_index_workspace_params;
        static constexpr auto value = glz::object(
            "workspace_dir", &T::workspace_dir,
            "force", &T::force
        );
    };
};

struct mcp_code_read_file_params {
    std::string file_path;
    std::string path;
    int start_line{1};
    int end_line{0};
    bool show_line_numbers{true};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_read_file_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "path", &T::path,
            "start_line", &T::start_line,
            "end_line", &T::end_line,
            "show_line_numbers", &T::show_line_numbers,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_write_file_params {
    std::string file_path;
    std::string path;
    std::string content;
    bool overwrite{false};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_write_file_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "path", &T::path,
            "content", &T::content,
            "overwrite", &T::overwrite,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_apply_patch_params {
    std::string file_path;
    std::string path;
    std::string target_content;
    std::string replacement_content;
    int start_line{1};
    int end_line{0};
    bool allow_multiple{false};
    std::string workspace_dir;
    struct glaze {
        using T = mcp_code_apply_patch_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "path", &T::path,
            "target_content", &T::target_content,
            "replacement_content", &T::replacement_content,
            "start_line", &T::start_line,
            "end_line", &T::end_line,
            "allow_multiple", &T::allow_multiple,
            "workspace_dir", &T::workspace_dir
        );
    };
};

struct mcp_code_diff_params {
    std::string file_path;
    std::string old_content;
    std::string new_content;
    struct glaze {
        using T = mcp_code_diff_params;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "old_content", &T::old_content,
            "new_content", &T::new_content
        );
    };
};

struct mcp_close_card_params {
    int index{-1};
    std::string uri;
    struct glaze {
        using T = mcp_close_card_params;
        static constexpr auto value = glz::object(
            "index", &T::index,
            "uri", &T::uri
        );
    };
};

struct mcp_notify_operator_telegram_params {
    std::string message;
    bool urgent{true};
    struct glaze {
        using T = mcp_notify_operator_telegram_params;
        static constexpr auto value = glz::object(
            "message", &T::message,
            "urgent", &T::urgent
        );
    };
};

struct mcp_focus_card_params {
    int index{-1};
    std::string uri;
    struct glaze {
        using T = mcp_focus_card_params;
        static constexpr auto value = glz::object(
            "index", &T::index,
            "uri", &T::uri
        );
    };
};

struct mcp_scroll_deck_params {
    int section{0};
    struct glaze {
        using T = mcp_scroll_deck_params;
        static constexpr auto value = glz::object(
            "section", &T::section
        );
    };
};

struct mcp_execute_card_action_params {
    int index{-1};
    std::string uri;
    std::string action;
    struct glaze {
        using T = mcp_execute_card_action_params;
        static constexpr auto value = glz::object(
            "index", &T::index,
            "uri", &T::uri,
            "action", &T::action
        );
    };
};

struct mcp_get_active_card_adaptive_params {
    int index{-1};
    std::string uri;
    struct glaze {
        using T = mcp_get_active_card_adaptive_params;
        static constexpr auto value = glz::object(
            "index", &T::index,
            "uri", &T::uri
        );
    };
};

struct mcp_set_window_geometry_params {
    int x{-1};
    int y{-1};
    int width{-1};
    int height{-1};
    struct glaze {
        using T = mcp_set_window_geometry_params;
        static constexpr auto value = glz::object(
            "x", &T::x,
            "y", &T::y,
            "width", &T::width,
            "height", &T::height
        );
    };
};

struct mcp_take_screenshot_params {
    std::string target{"deck"};
    std::string filename{"/tmp/snapshot.png"};
    int width{0};
    int height{0};
    struct glaze {
        using T = mcp_take_screenshot_params;
        static constexpr auto value = glz::object(
            "target", &T::target,
            "filename", &T::filename,
            "width", &T::width,
            "height", &T::height
        );
    };
};

struct mcp_set_camera_layout_params {
    std::string layout;
    int preset{-1};
    struct glaze {
        using T = mcp_set_camera_layout_params;
        static constexpr auto value = glz::object(
            "layout", &T::layout,
            "preset", &T::preset
        );
    };
};

struct mcp_save_process_definition_params {
    int64_t id{-1};
    std::string name;
    std::string executable_path;
    std::string arguments;
    std::string working_directory;
    std::string icon_source;
    struct glaze {
        using T = mcp_save_process_definition_params;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "executable_path", &T::executable_path,
            "arguments", &T::arguments,
            "working_directory", &T::working_directory,
            "icon_source", &T::icon_source
        );
    };
};

struct mcp_delete_process_definition_params {
    int64_t id{-1};
    std::string name;
    struct glaze {
        using T = mcp_delete_process_definition_params;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name
        );
    };
};

struct mcp_start_process_params {
    int64_t definition_id{0};
    std::string definition_name;
    struct glaze {
        using T = mcp_start_process_params;
        static constexpr auto value = glz::object(
            "definition_id", &T::definition_id,
            "definition_name", &T::definition_name
        );
    };
};

struct mcp_attach_process_params {
    int64_t pid{0};
    std::string name;
    struct glaze {
        using T = mcp_attach_process_params;
        static constexpr auto value = glz::object(
            "pid", &T::pid,
            "name", &T::name
        );
    };
};

struct mcp_kill_process_params {
    std::string run_id;
    struct glaze {
        using T = mcp_kill_process_params;
        static constexpr auto value = glz::object(
            "run_id", &T::run_id
        );
    };
};

struct mcp_get_process_ui_tree_params {
    std::string run_id;
    int64_t definition_id{0};
    int64_t pid{0};
    uint64_t hwnd{0};
    std::string window_title;
    std::string window_class;
    int max_depth{6};
    struct glaze {
        using T = mcp_get_process_ui_tree_params;
        static constexpr auto value = glz::object(
            "run_id", &T::run_id,
            "definition_id", &T::definition_id,
            "pid", &T::pid,
            "hwnd", &T::hwnd,
            "window_title", &T::window_title,
            "window_class", &T::window_class,
            "max_depth", &T::max_depth
        );
    };
};

struct mcp_get_process_ui_values_params {
    std::string run_id;
    int64_t definition_id{0};
    int64_t pid{0};
    uint64_t hwnd{0};
    std::string window_title;
    std::string window_class;
    int max_depth{8};
    bool edit_boxes_only{true};
    struct glaze {
        using T = mcp_get_process_ui_values_params;
        static constexpr auto value = glz::object(
            "run_id", &T::run_id,
            "definition_id", &T::definition_id,
            "pid", &T::pid,
            "hwnd", &T::hwnd,
            "window_title", &T::window_title,
            "window_class", &T::window_class,
            "max_depth", &T::max_depth,
            "edit_boxes_only", &T::edit_boxes_only
        );
    };
};

struct mcp_interact_process_ui_params {
    std::string run_id;
    int64_t definition_id{0};
    int64_t pid{0};
    uint64_t hwnd{0};
    std::string window_title;
    std::string window_class;
    std::string target;
    std::string action{"click"};
    std::string value;
    float x{0.0f};
    float y{0.0f};
    struct glaze {
        using T = mcp_interact_process_ui_params;
        static constexpr auto value = glz::object(
            "run_id", &T::run_id,
            "definition_id", &T::definition_id,
            "pid", &T::pid,
            "hwnd", &T::hwnd,
            "window_title", &T::window_title,
            "window_class", &T::window_class,
            "target", &T::target,
            "action", &T::action,
            "value", &T::value,
            "x", &T::x,
            "y", &T::y
        );
    };
};

struct mcp_capture_process_ui_screenshot_params {
    std::string run_id;
    int64_t definition_id{0};
    int64_t pid{0};
    uint64_t hwnd{0};
    std::string title_pattern;
    std::string filename;
    struct glaze {
        using T = mcp_capture_process_ui_screenshot_params;
        static constexpr auto value = glz::object(
            "run_id", &T::run_id,
            "definition_id", &T::definition_id,
            "pid", &T::pid,
            "hwnd", &T::hwnd,
            "title_pattern", &T::title_pattern,
            "filename", &T::filename
        );
    };
};

struct mcp_control_adlib_params {
    std::string command;
    std::string intro_video_path;
    std::string background_path;
    std::string outro_video_path;
    std::string output_mp4_path{"/Users/ignaciorodriguez/Downloads/adlib_output.mp4"};
    std::string mode{"recorded"};
    std::string mic_device_name;
    uint32_t mic_device_id{0};
    int duration_seconds{3};
    struct glaze {
        using T = mcp_control_adlib_params;
        static constexpr auto value = glz::object(
            "command", &T::command,
            "intro_video_path", &T::intro_video_path,
            "background_path", &T::background_path,
            "outro_video_path", &T::outro_video_path,
            "output_mp4_path", &T::output_mp4_path,
            "mode", &T::mode,
            "mic_device_name", &T::mic_device_name,
            "mic_device_id", &T::mic_device_id,
            "duration_seconds", &T::duration_seconds
        );
    };
};

struct mcp_control_cast_playback_params {
    std::string command{"play"};
    std::string url;
    std::string uri;
    struct glaze {
        using T = mcp_control_cast_playback_params;
        static constexpr auto value = glz::object(
            "command", &T::command,
            "url", &T::url,
            "uri", &T::uri
        );
    };
};

struct mcp_get_card_metrics_params {
    bool reset{false};
    bool include_all{false};
    struct glaze {
        using T = mcp_get_card_metrics_params;
        static constexpr auto value = glz::object(
            "reset", &T::reset,
            "include_all", &T::include_all
        );
    };
};

static int64_t mcp_resolve_process_pid(const std::string& run_id, int64_t definition_id, int64_t pid, uint64_t hwnd = 0) {
    if (pid > 0) return pid;
    if (!run_id.empty()) {
        auto snap = rouen::hosts::process_host::instance().snapshot(run_id);
        if (snap && snap->state == rouen::hosts::process_run_state::running) {
            return snap->pid;
        }
    }
    if (definition_id > 0) {
        auto latest = rouen::hosts::process_host::instance().latest_run_id(definition_id);
        if (latest) {
            auto snap = rouen::hosts::process_host::instance().snapshot(*latest);
            if (snap && snap->state == rouen::hosts::process_run_state::running) {
                return snap->pid;
            }
        }
    }
    if (hwnd != 0) {
#if defined(_WIN32)
        DWORD wpid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(hwnd), &wpid);
        if (wpid > 0) return static_cast<int64_t>(wpid);
#elif defined(__APPLE__)
        CFArrayRef window_list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
        if (window_list) {
            CFIndex count = CFArrayGetCount(window_list);
            for (CFIndex i = 0; i < count; ++i) {
                auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(window_list, i));
                if (!info) continue;
                CGWindowID wid = 0;
                auto num_ref = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
                if (num_ref) CFNumberGetValue(num_ref, kCFNumberSInt32Type, &wid);
                if (static_cast<uint64_t>(wid) == hwnd) {
                    pid_t wpid = 0;
                    auto pid_ref = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
                    if (pid_ref) CFNumberGetValue(pid_ref, kCFNumberIntType, &wpid);
                    CFRelease(window_list);
                    return static_cast<int64_t>(wpid);
                }
            }
            CFRelease(window_list);
        }
#endif
    }
    return 0;
}

static glz::json_t mcp_serialize_ui_node(const rouen::helpers::ui_element_node& node) {
    glz::json_t obj;
    if (!node.id.empty()) obj["id"] = node.id;
    if (!node.name.empty()) obj["name"] = node.name;
    if (!node.role.empty()) obj["role"] = node.role;
    if (!node.subrole.empty()) obj["subrole"] = node.subrole;
    if (!node.description.empty()) obj["description"] = node.description;
    if (!node.value.empty()) obj["value"] = node.value;
    obj["x"] = static_cast<double>(node.x);
    obj["y"] = static_cast<double>(node.y);
    obj["width"] = static_cast<double>(node.width);
    obj["height"] = static_cast<double>(node.height);
    obj["enabled"] = node.enabled;
    obj["focused"] = node.focused;

    if (!node.attributes.empty()) {
        glz::json_t attrs;
        for (const auto& a : node.attributes) {
            attrs[a.name] = a.value;
        }
        obj["attributes"] = std::move(attrs);
    }

    if (!node.children.empty()) {
        std::vector<glz::json_t> children_arr;
        for (const auto& child : node.children) {
            children_arr.push_back(mcp_serialize_ui_node(child));
        }
        obj["children"] = std::move(children_arr);
    }

    return obj;
}

struct local_command_request {
    std::string command;
    std::string working_directory;
};

struct mcp_wikipedia_search_params {
    std::string query;
    struct glaze {
        using T = mcp_wikipedia_search_params;
        static constexpr auto value = glz::object(
            "query", &T::query
        );
    };
};

struct mcp_wikipedia_get_article_params {
    std::string title;
    struct glaze {
        using T = mcp_wikipedia_get_article_params;
        static constexpr auto value = glz::object(
            "title", &T::title
        );
    };
};

struct mcp_wikipedia_create_card_params {
    std::string query;
    struct glaze {
        using T = mcp_wikipedia_create_card_params;
        static constexpr auto value = glz::object(
            "query", &T::query
        );
    };
};

struct mcp_wikipedia_result_item {
    std::string title;
    int pageid{0};
    std::string snippet;
    struct glaze {
        using T = mcp_wikipedia_result_item;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "pageid", &T::pageid,
            "snippet", &T::snippet
        );
    };
};

struct mcp_wikipedia_article_result {
    std::string title;
    std::string content;
    std::string url;
    struct glaze {
        using T = mcp_wikipedia_article_result;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "content", &T::content,
            "url", &T::url
        );
    };
};

struct mcp_create_card_params {
    std::string uri;
    struct glaze {
        using T = mcp_create_card_params;
        static constexpr auto value = glz::object(
            "uri", &T::uri
        );
    };
};

struct mcp_create_alarm_params {
    std::string datetime;
    struct glaze {
        using T = mcp_create_alarm_params;
        static constexpr auto value = glz::object(
            "datetime", &T::datetime
        );
    };
};

struct edit_file_request {
    std::string path;
};

struct mcp_youtube_search_params {
    std::string query;
    struct glaze {
        using T = mcp_youtube_search_params;
        static constexpr auto value = glz::object(
            "query", &T::query
        );
    };
};

struct mcp_youtube_play_params {
    std::string url;
    std::string title;
    struct glaze {
        using T = mcp_youtube_play_params;
        static constexpr auto value = glz::object(
            "url", &T::url,
            "title", &T::title
        );
    };
};

struct mcp_youtube_create_card_params {
    std::string query;
    struct glaze {
        using T = mcp_youtube_create_card_params;
        static constexpr auto value = glz::object(
            "query", &T::query
        );
    };
};

struct mcp_youtube_video {
    std::string id;
    std::string title;
    std::string url;
    std::string duration;
    std::string channel;
    struct glaze {
        using T = mcp_youtube_video;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "title", &T::title,
            "url", &T::url,
            "duration", &T::duration,
            "channel", &T::channel
        );
    };
};

struct mcp_time_series_point {
    std::string label;
    float value{0.0f};
    
    struct glaze {
        using T = mcp_time_series_point;
        static constexpr auto value = glz::object(
            "label", &T::label,
            "value", &T::value
        );
    };
};

struct mcp_create_time_series_params {
    std::string title;
    std::string unit;
    bool is_bar_chart{true};
    int color_index{0};
    std::vector<mcp_time_series_point> points;

    struct glaze {
        using T = mcp_create_time_series_params;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "unit", &T::unit,
            "is_bar_chart", &T::is_bar_chart,
            "color_index", &T::color_index,
            "points", &T::points
        );
    };
};

struct mcp_create_adaptive_card_params {
    std::string title;
    std::string card_json;
    std::string context_json;
    std::string name;

    struct glaze {
        using T = mcp_create_adaptive_card_params;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "card_json", &T::card_json,
            "context_json", &T::context_json,
            "name", &T::name
        );
    };
};

struct mcp_get_adaptive_card_params {
    std::string name;

    struct glaze {
        using T = mcp_get_adaptive_card_params;
        static constexpr auto value = glz::object(
            "name", &T::name
        );
    };
};

struct mcp_enable_persona_params {
    std::string name;
    int index{-1};
    struct glaze {
        using T = mcp_enable_persona_params;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "index", &T::index
        );
    };
};

struct mcp_themes_response {
    std::string status = "success";
    size_t active_index = 0;
    std::string active_theme;
    std::vector<std::string> themes;
    struct glaze {
        using T = mcp_themes_response;
        static constexpr auto value = glz::object(
            "status", &T::status,
            "active_index", &T::active_index,
            "active_theme", &T::active_theme,
            "themes", &T::themes
        );
    };
};

struct mcp_select_theme_params {
    std::string name;
    int index{-1};
    struct glaze {
        using T = mcp_select_theme_params;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "index", &T::index
        );
    };
};

struct mcp_notes_list_params {
    std::string search;
    std::string tag;
    struct glaze {
        using T = mcp_notes_list_params;
        static constexpr auto value = glz::object(
            "search", &T::search,
            "tag", &T::tag
        );
    };
};

struct mcp_note_summary {
    int id{0};
    std::string title;
    std::string tags;
    struct glaze {
        using T = mcp_note_summary;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "title", &T::title,
            "tags", &T::tags
        );
    };
};

struct mcp_notes_get_params {
    std::string title;
    struct glaze {
        using T = mcp_notes_get_params;
        static constexpr auto value = glz::object(
            "title", &T::title
        );
    };
};

struct mcp_contacts_list_params {
    std::string query;
    struct glaze {
        using T = mcp_contacts_list_params;
        static constexpr auto value = glz::object(
            "query", &T::query
        );
    };
};

struct mcp_contacts_get_params {
    int64_t id{-1};
    std::string name;
    struct glaze {
        using T = mcp_contacts_get_params;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name
        );
    };
};

struct mcp_contacts_save_params {
    int64_t id{-1};
    std::string first_name;
    std::string last_name;
    std::string display_name;
    std::string organization;
    std::string job_title;
    std::string email;
    std::string phone;
    std::string address;
    std::string notes;
    std::string picture_url;
    struct glaze {
        using T = mcp_contacts_save_params;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "first_name", &T::first_name,
            "last_name", &T::last_name,
            "display_name", &T::display_name,
            "organization", &T::organization,
            "job_title", &T::job_title,
            "email", &T::email,
            "phone", &T::phone,
            "address", &T::address,
            "notes", &T::notes,
            "picture_url", &T::picture_url
        );
    };
};

struct mcp_contacts_delete_params {
    int64_t id{-1};
    struct glaze {
        using T = mcp_contacts_delete_params;
        static constexpr auto value = glz::object(
            "id", &T::id
        );
    };
};

struct mcp_note_detail {
    int id{0};
    std::string title;
    std::string content;
    std::string tags;
    std::string created_at;
    std::string updated_at;
    struct glaze {
        using T = mcp_note_detail;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "title", &T::title,
            "content", &T::content,
            "tags", &T::tags,
            "created_at", &T::created_at,
            "updated_at", &T::updated_at
        );
    };
};

struct mcp_notes_save_params {
    std::string title;
    std::string content;
    std::string tags;
    struct glaze {
        using T = mcp_notes_save_params;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "content", &T::content,
            "tags", &T::tags
        );
    };
};

struct mcp_notes_append_params {
    std::string title;
    std::string content_to_append;
    struct glaze {
        using T = mcp_notes_append_params;
        static constexpr auto value = glz::object(
            "title", &T::title,
            "content_to_append", &T::content_to_append
        );
    };
};

struct mcp_notes_delete_params {
    std::string title;
    struct glaze {
        using T = mcp_notes_delete_params;
        static constexpr auto value = glz::object(
            "title", &T::title
        );
    };
};

struct mcp_notes_operation_result {
    std::string status;
    std::string message;
    int id{0};
    struct glaze {
        using T = mcp_notes_operation_result;
        static constexpr auto value = glz::object(
            "status", &T::status,
            "message", &T::message,
            "id", &T::id
        );
    };
};

struct mcp_get_calendar_events_params {
    std::string start_date;
    std::string end_date;
    struct glaze {
        using T = mcp_get_calendar_events_params;
        static constexpr auto value = glz::object(
            "start_date", &T::start_date,
            "end_date", &T::end_date
        );
    };
};

struct mcp_calendar_event_dto {
    std::string id;
    std::string summary;
    std::string description;
    std::string location;
    std::string start;
    std::string end;
    std::string creator;
    std::string organizer;
    bool all_day{false};
    struct glaze {
        using T = mcp_calendar_event_dto;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "summary", &T::summary,
            "description", &T::description,
            "location", &T::location,
            "start", &T::start,
            "end", &T::end,
            "creator", &T::creator,
            "organizer", &T::organizer,
            "all_day", &T::all_day
        );
    };
};

struct mcp_create_calendar_event_params {
    std::string calendar_name;
    std::string summary;
    std::string description;
    std::string location;
    int start_year{0};
    int start_month{0};
    int start_day{0};
    int start_hour{0};
    int start_min{0};
    int end_year{0};
    int end_month{0};
    int end_day{0};
    int end_hour{0};
    int end_min{0};
    bool is_all_day{false};
    struct glaze {
        using T = mcp_create_calendar_event_params;
        static constexpr auto value = glz::object(
            "calendar_name", &T::calendar_name,
            "summary", &T::summary,
            "description", &T::description,
            "location", &T::location,
            "start_year", &T::start_year,
            "start_month", &T::start_month,
            "start_day", &T::start_day,
            "start_hour", &T::start_hour,
            "start_min", &T::start_min,
            "end_year", &T::end_year,
            "end_month", &T::end_month,
            "end_day", &T::end_day,
            "end_hour", &T::end_hour,
            "end_min", &T::end_min,
            "is_all_day", &T::is_all_day
        );
    };
};

mcp_host::mcp_host() {
    detect_system_info();

    // Register default run_local_command function associated with terminal
    function_definition const run_cmd_def(
        "run_local_command",
        "Execute a local shell command and return combined stdout/stderr output. Supports any local command, including curl. Host system info: " + cached_system_info_,
        R"mcp({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute locally (for example: curl -sS http://127.0.0.1:8099/health)"},"working_directory":{"type":"string","description":"Optional directory where the command should run"}},"required":["command"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return "Error: Missing params. Expected JSON with a non-empty 'command' field.";
            }

            local_command_request request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.command.empty()) {
                return R"(Error: Invalid params. Expected JSON: {"command":"...","working_directory":"optional"}.)";
            }

            std::string work_dir = request.working_directory;
            if (work_dir.empty()) {
                try {
                    auto cwd = std::filesystem::current_path();
                    if (cwd == "/" || cwd == "C:\\" || cwd == "c:\\") {
                        work_dir = helpers::CodeEditorService::instance().get_default_workspace();
                    }
                } catch (...) {}
            }

            const std::string command_with_stderr = request.command + " 2>&1";
            std::string output;
            if (!work_dir.empty()) {
                output = ProcessHelper::executeCommandInDirectory(work_dir, command_with_stderr);
            } else {
                output = ProcessHelper::executeCommand(command_with_stderr);
            }
            if (output.empty()) {
                return "[Command returned no output]";
            }
            return output;
        },
        "terminal"
    );
    
    register_function("terminal", run_cmd_def);

    // Register global create_card function
    function_definition const create_card_def(
        "create_card",
        "Create and add a new card to Rouen by its URI (e.g. 'pomodoro', 'terminal', 'git', 'calendar'). Use 'pomodoro' to open/create a Pomodoro timer card.",
        R"mcp({"type":"object","properties":{"uri":{"type":"string","description":"The URI of the card to create (e.g. 'pomodoro')"}},"required":["uri"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_create_card_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.uri.empty()) {
                return R"({"status":"error","message":"Invalid params"})";
            }
            
            try {
                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                (*create_card_fn)(request.uri);
                return R"({"status":"success","message":"Card created successfully"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"create_card service is not available: {}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", create_card_def);

    // Register global create_number_series_card function
    function_definition const create_time_series_def(
        "create_number_series_card",
        "Create a card with a custom number series, category comparison, or time series visualization (e.g. monthly sales, country achievements, or system stats).",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"Title of the visualization"},"unit":{"type":"string","description":"Unit of measurement label (e.g. '$', '%', 'wins', 'C')"},"is_bar_chart":{"type":"boolean","description":"True for a bar chart, false for a line chart"},"color_index":{"type":"integer","description":"Accent color index: 0=Accent, 1=Secondary, 2=Error, 3=Success, 4=Warning, 5=Info, 6=Purple, 7=Pink, 8=Orange, 9=Gray"},"points":{"type":"array","description":"Array of data points","items":{"type":"object","properties":{"label":{"type":"string","description":"X-axis label (can be a date, name, country, or category, e.g. 'Jan', 'Brazil', 'To Do', 'Day 1')"},"value":{"type":"number","description":"Data value"}},"required":["label","value"]}}},"required":["title","points"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_create_time_series_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty() || request.points.empty()) {
                return R"({"status":"error","message":"Invalid params"})";
            }
            
            try {
                models::series::series_record series_rec{};
                series_rec.title = request.title;
                series_rec.name = models::series::series_repository::slugify(request.title);
                series_rec.unit = request.unit;
                series_rec.is_bar_chart = request.is_bar_chart;
                series_rec.color_index = request.color_index;
                for (const auto& pt : request.points) {
                    series_rec.points.push_back({pt.label, pt.value});
                }

                models::series::series_repository repo;
                repo.save_series(series_rec);

                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                std::string const card_uri = std::format("number-series:{}", series_rec.name);
                (*create_card_fn)(card_uri);
                
                return R"({"status":"success","message":"Number series card created and persisted successfully"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"create_card service is not available: {}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", create_time_series_def);

    // Register global create_adaptive_card function
    function_definition const create_adaptive_card_def(
        "create_adaptive_card",
        "Create, persist, and present an Adaptive Card in Rouen. Accepts card title, JSON template structure, optional context data, and optional name/slug.",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"Title of the Adaptive Card"},"card_json":{"type":"string","description":"Adaptive Card JSON template structure"},"context_json":{"type":"string","description":"Optional context JSON data object for template variable binding (e.g. '{\"name\":\"Rouen\"}')"},"name":{"type":"string","description":"Optional unique slug or identifier for the card"}},"required":["title","card_json"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_create_adaptive_card_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty() || request.card_json.empty()) {
                return R"({"status":"error","message":"Invalid params. Required fields: 'title', 'card_json'."})";
            }
            
            try {
                models::adaptive_cards::adaptive_card_record card_rec{};
                card_rec.title = request.title;
                card_rec.card_json = request.card_json;
                card_rec.context_json = request.context_json.empty() ? "{}" : request.context_json;
                if (!request.name.empty()) {
                    card_rec.name = models::adaptive_cards::adaptive_cards_repository::slugify(request.name);
                } else {
                    card_rec.name = models::adaptive_cards::adaptive_cards_repository::slugify(request.title);
                }

                models::adaptive_cards::adaptive_cards_repository repo;
                repo.save_card(card_rec);

                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                std::string card_uri = std::format("adaptive-card:{}", card_rec.name);
                (*create_card_fn)(card_uri);
                
                return std::format(R"({{"status":"success","message":"Adaptive Card created, saved, and presented successfully","name":"{}","uri":"{}"}})", card_rec.name, card_uri);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"Failed to create Adaptive Card: {}"}})", e.what());
            }
        },
        "adaptive_card"
    );
    
    register_function("adaptive_card", create_adaptive_card_def);
    register_function("deck", create_adaptive_card_def);

    // Register list_adaptive_cards function
    function_definition const list_adaptive_cards_def(
        "list_adaptive_cards",
        "List all persisted Adaptive Cards in Rouen.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                models::adaptive_cards::adaptive_cards_repository repo;
                auto cards = repo.list_cards();
                std::string json_res;
                auto ec = glz::write_json(cards, json_res);
                if (!ec) {
                    return json_res;
                }
                return R"({"status":"error","message":"Failed to serialize adaptive cards list"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "adaptive_card"
    );

    register_function("adaptive_card", list_adaptive_cards_def);

    // Register get_adaptive_card function
    function_definition const get_adaptive_card_def(
        "get_adaptive_card",
        "Get definition and data of a persisted Adaptive Card by name.",
        R"mcp({"type":"object","properties":{"name":{"type":"string","description":"Unique slug or name of the Adaptive Card"}},"required":["name"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            mcp_get_adaptive_card_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.name.empty()) {
                return R"({"status":"error","message":"Invalid params. Required field: 'name'."})";
            }

            try {
                models::adaptive_cards::adaptive_cards_repository repo;
                auto card_opt = repo.get_card_by_name(request.name);
                if (!card_opt.has_value()) {
                    return R"({"status":"error","message":"Adaptive card not found"})";
                }
                std::string json_res;
                auto ec = glz::write_json(card_opt.value(), json_res);
                if (!ec) {
                    return json_res;
                }
                return R"({"status":"error","message":"Failed to serialize card"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "adaptive_card"
    );

    register_function("adaptive_card", get_adaptive_card_def);

    // Register global create_alarm function
    function_definition const create_alarm_def(
        "create_alarm",
        "Create a new alarm card in the deck for a specific date and time. The datetime should be in ISO format 'YYYY-MM-DDTHH:MM:SS' or 'YYYY-MM-DD HH:MM'. If only a relative time is requested (e.g., 'in 20 minutes'), calculate the target date and time based on the current local time first and pass it to this tool.",
        R"mcp({"type":"object","properties":{"datetime":{"type":"string","description":"The target date and time. Can be full ISO format 'YYYY-MM-DDTHH:MM:SS' or 'YYYY-MM-DD HH:MM'. For relative times (e.g. 'in 20 minutes') or natural language (e.g. '5pm'), calculate the exact future date/time first based on the current local time."}},"required":["datetime"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_create_alarm_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.datetime.empty()) {
                return R"({"status":"error","message":"Invalid params"})";
            }
            
            try {
                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                std::string const card_uri = "alarm:" + request.datetime;
                (*create_card_fn)(card_uri);
                return R"({"status":"success","message":"Alarm created successfully"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"create_card service is not available: {}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", create_alarm_def);

    // Register default edit_file function associated with editor
    function_definition const edit_file_def(
        "edit_file",
        "Open a file in the Rouen internal text/image editor for viewing or editing. Accepts a file path.",
        R"mcp({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "The absolute or relative path of the file to open in the editor"
                }
            },
            "required": ["path"]
        })mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return "Error: Missing params. Expected JSON with a non-empty 'path' field.";
            }
            edit_file_request request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.path.empty()) {
                return "Error: Invalid params. Expected JSON with a non-empty 'path' field.";
            }
            try {
                auto edit_func = registrar::get<std::function<void(std::string const &)>>("edit");
                if (edit_func) {
                    (*edit_func)(request.path);
                    return "Successfully opened " + request.path + " in the editor.";
                }
            } catch (...) {
                (void)0;
            }
            return "Error: Editor service is not available.";
        },
        "editor"
    );
    
    register_function("editor", edit_file_def);

    // Register code_discover_toolchain function
    function_definition const code_discover_toolchain_def(
        "code_discover_toolchain",
        "Discover available C/C++ compilers, build tools (ninja/cmake), linters, Nix development environment, and compile_commands.json in the workspace.",
        R"mcp({"type":"object","properties":{"workspace_path":{"type":"string","description":"Optional root path of the project workspace. If omitted, current directory is used."}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_discover_toolchain_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                auto tc = helpers::ToolchainService::instance().discover_toolchain(req.workspace_path);
                std::string buffer;
                static_cast<void>(glz::write_json(tc, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_discover_toolchain_def);
    register_function("terminal", code_discover_toolchain_def);

    // Register code_check_syntax function
    function_definition const code_check_syntax_def(
        "code_check_syntax",
        "Fast non-linking syntax and type verification for source code files (C/C++ using -fsyntax-only, Python using py_compile, JavaScript using node --check, Shell using bash -n). Automatically parses errors and updates open editor card markers in real time.",
        R"mcp({"type":"object","properties":{"file_path":{"type":"string","description":"Path to the source file to check"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["file_path"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing file_path parameter"})";
                }
                mcp_code_check_syntax_params req{};
                auto err = glz::read_json(req, params);
                if (err || req.file_path.empty()) {
                    return R"({"status":"error","message":"Invalid parameters. Expected {\"file_path\":\"...\"}"})";
                }

                auto result = helpers::SyntaxChecker::instance().check_file(req.file_path, req.workspace_dir);
                std::string buffer;
                static_cast<void>(glz::write_json(result, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_check_syntax_def);
    register_function("terminal", code_check_syntax_def);

    // Register code_clear_diagnostics function
    function_definition const code_clear_diagnostics_def(
        "code_clear_diagnostics",
        "Clear all active visual error markers and diagnostics from the open text editor.",
        R"mcp({"type":"object","properties":{"file_path":{"type":"string","description":"Optional file path to verify before clearing"}},"required":[]})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto clear_fn = registrar::get<std::function<void()>>("editor_clear_error_markers");
                if (clear_fn && *clear_fn) {
                    (*clear_fn)();
                    return R"({"status":"success","message":"Editor error markers cleared."})";
                }
                return R"({"status":"info","message":"Editor service not active."})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_clear_diagnostics_def);
    register_function("terminal", code_clear_diagnostics_def);

    // Register code_search function
    function_definition const code_search_def(
        "code_search",
        "Fast full-text codebase search across C++ and CMake source files using SQLite FTS5 trigrams. Returns matching files, exact line numbers, and snippets.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"Text query, symbol name, or substring to search for"},"max_results":{"type":"integer","description":"Maximum number of results to return (default 50)"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["query"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing parameters. Expected {\"query\":\"...\"}"})";
                }
                mcp_code_search_params req{};
                auto err = glz::read_json(req, params);
                if (err || req.query.empty()) {
                    return R"({"status":"error","message":"Invalid parameters. Expected non-empty 'query' field."})";
                }

                auto matches = helpers::CodeIndexer::instance().search(req.query, static_cast<size_t>(req.max_results > 0 ? req.max_results : 50));
                std::string buffer;
                static_cast<void>(glz::write_json(matches, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_search_def);
    register_function("terminal", code_search_def);

    // Register code_find_symbol function
    function_definition const code_find_symbol_def(
        "code_find_symbol",
        "Search indexed C++ symbol table for class, struct, function, enum, or macro definitions and declarations. Supports wildcards like 'Render*'.",
        R"mcp({"type":"object","properties":{"name":{"type":"string","description":"Exact symbol name or wildcard pattern (e.g. 'SetErrorMarkers' or 'Render*')"},"kind":{"type":"string","description":"Optional kind filter: 'class', 'struct', 'function', 'enum', 'namespace', 'macro', 'alias'"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["name"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing parameters. Expected {\"name\":\"...\"}"})";
                }
                mcp_code_find_symbol_params req{};
                auto err = glz::read_json(req, params);
                if (err || req.name.empty()) {
                    return R"({"status":"error","message":"Invalid parameters. Expected non-empty 'name' field."})";
                }

                auto symbols = helpers::CodeIndexer::instance().find_symbol(req.name, req.kind);
                std::string buffer;
                static_cast<void>(glz::write_json(symbols, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_find_symbol_def);
    register_function("terminal", code_find_symbol_def);

    // Register code_index_workspace function
    function_definition const code_index_workspace_def(
        "code_index_workspace",
        "Scan and index the workspace directory into the local SQLite FTS5 code and symbol database. Incremental updates skip unmodified files.",
        R"mcp({"type":"object","properties":{"workspace_dir":{"type":"string","description":"Optional workspace root directory. Defaults to current directory."},"force":{"type":"boolean","description":"Force re-indexing all files regardless of modification timestamp"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_index_workspace_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                auto stats = helpers::CodeIndexer::instance().index_workspace(req.workspace_dir, req.force);
                std::string buffer;
                static_cast<void>(glz::write_json(stats, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_index_workspace_def);
    register_function("terminal", code_index_workspace_def);

    // Register code_read_file function
    function_definition const code_read_file_def(
        "code_read_file",
        "Read a line-bounded slice or full content of a file with line numbers. Inspect files before applying targeted patches.",
        R"mcp({"type":"object","properties":{"path":{"type":"string","description":"File path to read"},"file_path":{"type":"string","description":"Alternative parameter name for path"},"start_line":{"type":"integer","description":"1-based start line (default: 1)"},"end_line":{"type":"integer","description":"1-based end line (default: read to end or 2000 lines)"},"show_line_numbers":{"type":"boolean","description":"Include line numbers (default: true)"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_read_file_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                std::string p = !req.path.empty() ? req.path : req.file_path;
                if (p.empty()) {
                    return R"({"status":"error","message":"Missing required file path."})";
                }
                auto res = helpers::CodeEditorService::instance().read_file(p, req.start_line, req.end_line, req.show_line_numbers, req.workspace_dir);
                std::string buffer;
                static_cast<void>(glz::write_json(res, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_read_file_def);
    register_function("terminal", code_read_file_def);

    // Register code_write_file function
    function_definition const code_write_file_def(
        "code_write_file",
        "Write or overwrite a full file with automated syntax verification and index update. Creates directories as needed.",
        R"mcp({"type":"object","properties":{"path":{"type":"string","description":"Target file path"},"file_path":{"type":"string","description":"Alternative parameter name for path"},"content":{"type":"string","description":"Full text content to write"},"overwrite":{"type":"boolean","description":"Allow overwriting existing file (default: false)"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["content"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_write_file_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                std::string p = !req.path.empty() ? req.path : req.file_path;
                if (p.empty()) {
                    return R"({"status":"error","message":"Missing required file path."})";
                }
                auto res = helpers::CodeEditorService::instance().write_file(p, req.content, req.overwrite, req.workspace_dir);
                std::string buffer;
                static_cast<void>(glz::write_json(res, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_write_file_def);
    register_function("terminal", code_write_file_def);

    // Register code_apply_patch function
    function_definition const code_apply_patch_def(
        "code_apply_patch",
        "Surgically replace a unique contiguous block of code in a file. Automatically invokes compiler syntax checks and returns diagnostic errors if syntax is broken, enabling the self-correction loop.",
        R"mcp({"type":"object","properties":{"path":{"type":"string","description":"Target file path"},"file_path":{"type":"string","description":"Alternative parameter name for path"},"target_content":{"type":"string","description":"Exact character sequence to be replaced (including leading whitespace and newlines)"},"replacement_content":{"type":"string","description":"Replacement text to substitute in place of target_content"},"start_line":{"type":"integer","description":"Optional 1-based start line to constrain search range"},"end_line":{"type":"integer","description":"Optional 1-based end line to constrain search range"},"allow_multiple":{"type":"boolean","description":"Allow replacing multiple occurrences if found (default: false)"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["target_content","replacement_content"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_apply_patch_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                std::string p = !req.path.empty() ? req.path : req.file_path;
                if (p.empty()) {
                    return R"({"status":"error","message":"Missing required file path."})";
                }
                auto res = helpers::CodeEditorService::instance().apply_patch(
                    p, req.target_content, req.replacement_content,
                    req.start_line, req.end_line, req.allow_multiple, req.workspace_dir
                );
                std::string buffer;
                static_cast<void>(glz::write_json(res, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_apply_patch_def);
    register_function("terminal", code_apply_patch_def);

    // Register code_undo function
    function_definition const code_undo_def(
        "code_undo",
        "Undo the most recent file edit applied by AI or editor. Reverts disk content and updates index.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                bool ok = helpers::CodeEditorService::instance().undo();
                return ok ? R"({"status":"success","message":"Last edit undone successfully."})"
                          : R"({"status":"error","message":"No edits available in undo stack."})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_undo_def);
    register_function("terminal", code_undo_def);

    // Register code_redo function
    function_definition const code_redo_def(
        "code_redo",
        "Redo the most recently undone file edit. Re-applies content to disk and updates index.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                bool ok = helpers::CodeEditorService::instance().redo();
                return ok ? R"({"status":"success","message":"Last undone edit redone successfully."})"
                          : R"({"status":"error","message":"No edits available in redo stack."})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_redo_def);
    register_function("terminal", code_redo_def);

    // Register code_get_history function
    function_definition const code_get_history_def(
        "code_get_history",
        "Retrieve recent file edit history and undo stack entries with timestamps, descriptions, and compiler syntax status.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto history = helpers::CodeEditorService::instance().get_history();
                std::string buffer;
                static_cast<void>(glz::write_json(history, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_get_history_def);
    register_function("terminal", code_get_history_def);

    // Register code_diff function
    function_definition const code_diff_def(
        "code_diff",
        "Generate a structured unified diff comparing proposed edits against original content or comparing a file against Git HEAD.",
        R"mcp({"type":"object","properties":{"file_path":{"type":"string","description":"Target file path to compare against Git HEAD or staging"},"old_content":{"type":"string","description":"Optional base content for arbitrary comparison"},"new_content":{"type":"string","description":"Optional modified content for arbitrary comparison"}}})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_diff_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                if (!req.old_content.empty() || !req.new_content.empty()) {
                    auto diff = helpers::DiffEngine::compute_diff(req.old_content, req.new_content, "old", "new");
                    std::string unified = helpers::DiffEngine::to_unified_diff(diff);
                    return std::format(R"({{"has_changes":{},"additions":{},"deletions":{},"diff":{}}})",
                        diff.has_changes ? "true" : "false", diff.total_additions, diff.total_deletions,
                        glz::write_json(unified).value_or("\"\""));
                }
                if (!req.file_path.empty()) {
                    std::string resolved = ProcessHelper::expandTilde(req.file_path);
                    if (!std::filesystem::exists(resolved)) {
                        return R"({"status":"error","message":"File does not exist."})";
                    }
                    std::ifstream f(resolved, std::ios::binary);
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    std::string disk_content = ss.str();

                    // Compare with HEAD
                    std::string git_path = CONFIG_SERVICE()->get_git_path();
                    std::string head_cmd = std::format("{} show HEAD:\"{}\"", git_path, resolved);
                    std::string head_content = models::GitProcessHelper::executeCommandInDirectory(".", head_cmd);
                    if (head_content.starts_with("fatal:")) head_content.clear();

                    auto diff = helpers::DiffEngine::compute_diff(head_content, disk_content, resolved, resolved);
                    std::string unified = helpers::DiffEngine::to_unified_diff(diff);
                    return std::format(R"({{"has_changes":{},"additions":{},"deletions":{},"diff":{}}})",
                        diff.has_changes ? "true" : "false", diff.total_additions, diff.total_deletions,
                        glz::write_json(unified).value_or("\"\""));
                }
                return R"({"status":"error","message":"Provide file_path or old_content/new_content."})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_diff_def);
    register_function("terminal", code_diff_def);

    // Register code_stage_patch function
    function_definition const code_stage_patch_def(
        "code_stage_patch",
        "Stage a proposed patch into the Visual Diff & Staging Card without immediately modifying disk. Allows developers to review side-by-side diffs and accept/discard individual hunks.",
        R"mcp({"type":"object","properties":{"path":{"type":"string","description":"Target file path"},"file_path":{"type":"string","description":"Alternative parameter name for path"},"target_content":{"type":"string","description":"Exact character sequence to be replaced"},"replacement_content":{"type":"string","description":"Replacement text"},"start_line":{"type":"integer","description":"Optional 1-based start line"},"end_line":{"type":"integer","description":"Optional 1-based end line"},"allow_multiple":{"type":"boolean","description":"Allow replacing multiple occurrences"},"workspace_dir":{"type":"string","description":"Optional workspace directory"}},"required":["target_content","replacement_content"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_apply_patch_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }
                std::string p = !req.path.empty() ? req.path : req.file_path;
                if (p.empty()) {
                    return R"({"status":"error","message":"Missing required file path."})";
                }
                std::string staged_id = helpers::CodeEditorService::instance().stage_patch(
                    p, req.target_content, req.replacement_content,
                    req.start_line, req.end_line, req.allow_multiple, req.workspace_dir
                );
                if (staged_id.empty()) {
                    return R"({"status":"error","message":"Failed to stage patch: target content not found or file does not exist."})";
                }
                // Also open or notify diff card
                try {
                    auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                    if (create_card_fn && *create_card_fn) {
                        (*create_card_fn)("diff:staged");
                    }
                } catch (...) {}

                return std::format(R"({{"status":"staged","staged_id":"{}","message":"Patch staged in Visual Diff Card for developer review."}})", staged_id);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_stage_patch_def);
    register_function("terminal", code_stage_patch_def);

    // Register code_generate_conventional_commit function
    function_definition const code_generate_commit_def(
        "code_generate_conventional_commit",
        "Generate a standardized commit message following the Conventional Commits 1.0.0 specification (e.g. feat(...), fix(...), refactor(...)) based on git diff and status context.",
        R"mcp({"type":"object","properties":{"diff_context":{"type":"string","description":"Optional git diff or code changes to summarize"},"status_context":{"type":"string","description":"Optional git status summary"},"workspace_dir":{"type":"string","description":"Optional workspace directory path"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_code_generate_conventional_commit_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }

                std::string diff = req.diff_context;
                std::string status = req.status_context;
                std::string ws = req.workspace_dir.empty() ? "." : req.workspace_dir;

                if (diff.empty()) {
                    std::string git_path = CONFIG_SERVICE()->get_git_path();
                    diff = models::GitProcessHelper::executeCommandInDirectory(ws, git_path + " diff --cached");
                    if (diff.empty()) {
                        diff = models::GitProcessHelper::executeCommandInDirectory(ws, git_path + " diff");
                    }
                }
                if (status.empty()) {
                    std::string git_path = CONFIG_SERVICE()->get_git_path();
                    status = models::GitProcessHelper::executeCommandInDirectory(ws, git_path + " status --short");
                }

                std::string msg = helpers::ConventionalCommitGenerator::generate_commit_message(diff, status, ws);
                auto parsed = helpers::ConventionalCommitGenerator::parse_conventional_commit(msg);

                std::string buffer;
                static_cast<void>(glz::write_json(parsed, buffer));
                return buffer;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "editor"
    );
    register_function("editor", code_generate_commit_def);
    register_function("terminal", code_generate_commit_def);
    register_function("git", code_generate_commit_def);

    // Register Engineering Lifecycle MCP Tools
    function_definition const run_unit_tests_def(
        "run_unit_tests",
        "Executes the test suite under tests/ or a specific test target binary, returning exit code, execution time, and parsed test results.",
        R"mcp({"type":"object","properties":{"target_test":{"type":"string","description":"Specific test binary name (e.g. test_workspace_rules, test_gemini_adapter) or empty to run ctest"},"filter":{"type":"string","description":"Optional test filter pattern (passed to --gtest_filter or ctest -R)"},"timeout_seconds":{"type":"integer","description":"Timeout in seconds (default: 120)"},"workspace_dir":{"type":"string","description":"Workspace directory root"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_run_unit_tests_params p{};
                if (!params.empty()) {
                    auto err = glz::read_json(p, params);
                    if (err) {
                        return R"({"status":"error","message":"Invalid JSON parameters for run_unit_tests"})";
                    }
                }
                std::string ws = p.workspace_dir;
                if (ws.empty()) {
                    ws = helpers::CodeEditorService::instance().get_default_workspace();
                }
                if (ws.empty()) {
                    try { ws = std::filesystem::current_path().string(); } catch (...) {}
                }

                std::string cmd;
                if (p.target_test.empty()) {
                    cmd = "ctest --test-dir build --output-on-failure";
                    if (!p.filter.empty()) {
                        cmd += " -R \"" + p.filter + "\"";
                    }
                } else {
                    cmd = "./build/tests/" + p.target_test;
                    if (!p.filter.empty()) {
                        cmd += " --gtest_filter=\"" + p.filter + "\"";
                    }
                }

                int timeout = p.timeout_seconds > 0 ? p.timeout_seconds : 120;
                auto start_time = std::chrono::steady_clock::now();
                auto res = ProcessHelper::executeCommandWithTimeout(cmd, ws, std::chrono::seconds(timeout));
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

                bool success = (res.exit_code == 0 && !res.timed_out);
                std::string escaped_output;
                static_cast<void>(glz::write_json(res.output, escaped_output));

                return std::format(
                    R"({{"status":"{}","success":{},"exit_code":{},"timed_out":{},"duration_ms":{},"output":{}}})",
                    success ? "success" : "failed",
                    success ? "true" : "false",
                    res.exit_code,
                    res.timed_out ? "true" : "false",
                    duration,
                    escaped_output
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "terminal"
    );
    register_function("terminal", run_unit_tests_def);
    register_function("editor", run_unit_tests_def);

    function_definition const build_and_deploy_def(
        "build_and_deploy",
        "Compiles a target using Ninja with strict -j2 parallelism, and if deploying rouen on macOS, preserves .env in Contents/Resources/.env and ad-hoc signs with designated requirement com.rouen.app.",
        R"mcp({"type":"object","properties":{"target":{"type":"string","description":"Target binary name (default: rouen)"},"quick_build":{"type":"boolean","description":"Use ninja quick build (default: true)"},"deploy":{"type":"boolean","description":"Deploy to ~/Applications/Rouen.app and code sign if target is rouen (default: true)"},"workspace_dir":{"type":"string","description":"Workspace directory root"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_build_and_deploy_params p{};
                if (!params.empty()) {
                    auto err = glz::read_json(p, params);
                    if (err) {
                        return R"({"status":"error","message":"Invalid JSON parameters for build_and_deploy"})";
                    }
                }
                if (p.target.empty()) p.target = "rouen";

                std::string ws = p.workspace_dir;
                if (ws.empty()) {
                    ws = helpers::CodeEditorService::instance().get_default_workspace();
                }
                if (ws.empty()) {
                    try { ws = std::filesystem::current_path().string(); } catch (...) {}
                }

                // Strict -j2 parallelism enforced
                std::string build_cmd = "nix develop --command cmake --build build --target " + p.target + " -j2";
                auto build_res = ProcessHelper::executeCommandWithTimeout(build_cmd, ws, std::chrono::seconds(600));

                bool success = (build_res.exit_code == 0 && !build_res.timed_out);
                bool deployed = false;
                std::string deploy_output;

                if (success && p.deploy && p.target == "rouen" && !rouen::platform::is_windows) {
                    std::string deploy_cmd = R"(cp build/rouen.app/Contents/MacOS/rouen $HOME/Applications/Rouen.app/Contents/MacOS/rouen && ([ ! -f $HOME/Applications/Rouen.app/Contents/MacOS/.env ] || cp $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/Resources/.env) && rm -f $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/MacOS/.rouen-wrapped && chmod +x $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib 2>/dev/null || true && codesign --force --sign - $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib && codesign --force --sign - --requirements '=designated => identifier "com.rouen.app"' $HOME/Applications/Rouen.app/Contents/MacOS/rouen)";
                    auto dep_res = ProcessHelper::executeCommandWithTimeout(deploy_cmd, ws, std::chrono::seconds(60));
                    deployed = (dep_res.exit_code == 0);
                    deploy_output = dep_res.output;
                }

                std::string escaped_build_out, escaped_deploy_out;
                static_cast<void>(glz::write_json(build_res.output, escaped_build_out));
                static_cast<void>(glz::write_json(deploy_output, escaped_deploy_out));

                return std::format(
                    R"({{"status":"{}","success":{},"exit_code":{},"deployed":{},"build_output":{},"deploy_output":{}}})",
                    success ? "success" : "failed",
                    success ? "true" : "false",
                    build_res.exit_code,
                    deployed ? "true" : "false",
                    escaped_build_out,
                    escaped_deploy_out
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "terminal"
    );
    register_function("terminal", build_and_deploy_def);
    register_function("editor", build_and_deploy_def);

    function_definition const update_inbox_item_def(
        "update_inbox_item",
        "Updates an inbox report status header, appends Resolution & Outcome Summary, and renames the file according to inbox/README.md conventions (done_<slug>.md).",
        R"mcp({"type":"object","properties":{"file_path":{"type":"string","description":"Relative or absolute path to the inbox markdown file"},"status":{"type":"string","description":"Lifecycle status: done, in-progress, awaiting-answer, or rejected"},"resolution_summary":{"type":"string","description":"Summary details to append under Resolution & Outcome Summary"},"workspace_dir":{"type":"string","description":"Workspace directory root"}},"required":["file_path","status"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_update_inbox_item_params p{};
                auto err = glz::read_json(p, params);
                if (err || p.file_path.empty() || p.status.empty()) {
                    return R"({"status":"error","message":"file_path and status parameters are required"})";
                }

                std::string resolved = helpers::CodeEditorService::instance().resolve_path(p.file_path, p.workspace_dir);
                if (resolved.empty() || !std::filesystem::exists(resolved)) {
                    return std::format(R"({{"status":"error","message":"File not found: {}"}})", p.file_path);
                }

                std::ifstream in(resolved);
                if (!in.is_open()) {
                    return std::format(R"({{"status":"error","message":"Failed to read file: {}"}})", resolved);
                }
                std::stringstream buffer;
                buffer << in.rdbuf();
                in.close();
                std::string content = buffer.str();

                // Format status
                std::string status_lower = p.status;
                std::transform(status_lower.begin(), status_lower.end(), status_lower.begin(), ::tolower);
                std::string display_status = "Open";
                if (status_lower == "done") display_status = "Done";
                else if (status_lower == "in-progress" || status_lower == "in_progress") display_status = "In Progress";
                else if (status_lower == "awaiting-answer" || status_lower == "awaiting_answer") display_status = "Awaiting Answer";
                else if (status_lower == "rejected") display_status = "Rejected";

                // Update Status header
                size_t status_pos = content.find("**Status**:");
                if (status_pos != std::string::npos) {
                    size_t line_end = content.find('\n', status_pos);
                    std::string new_status_line = "**Status**: " + display_status;
                    if (display_status == "Done" || display_status == "Rejected") {
                        auto now = std::chrono::system_clock::now();
                        auto now_time_t = std::chrono::system_clock::to_time_t(now);
                        std::tm const now_tm = *std::localtime(&now_time_t);
                        char date_buf[32];
                        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &now_tm);
                        new_status_line += "  \n**Resolution Date**: " + std::string(date_buf);
                    }
                    content.replace(status_pos, (line_end == std::string::npos ? content.size() - status_pos : line_end - status_pos), new_status_line);
                }

                // Append resolution summary if provided
                if (!p.resolution_summary.empty()) {
                    if (content.find("## Resolution & Outcome Summary") == std::string::npos) {
                        content += "\n\n---\n\n## Resolution & Outcome Summary\n\n" + p.resolution_summary + "\n";
                    } else {
                        content += "\n" + p.resolution_summary + "\n";
                    }
                }

                // Write file back
                {
                    std::ofstream out(resolved);
                    if (!out.is_open()) {
                        return std::format(R"({{"status":"error","message":"Failed to write updated file: {}"}})", resolved);
                    }
                    out << content;
                }

                std::filesystem::path current_path(resolved);
                std::filesystem::path new_path = current_path;

                if (display_status == "Done") {
                    std::string filename = current_path.filename().string();
                    if (!filename.starts_with("done_")) {
                        new_path = current_path.parent_path() / ("done_" + filename);
                        std::error_code ec;
                        std::filesystem::rename(current_path, new_path, ec);
                        if (ec) {
                            new_path = current_path;
                        }
                    }
                }

                return std::format(
                    R"({{"status":"success","original_path":"{}","new_path":"{}","updated_status":"{}"}})",
                    resolved, new_path.string(), display_status
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "terminal"
    );
    register_function("terminal", update_inbox_item_def);
    register_function("editor", update_inbox_item_def);

    function_definition const announce_completion_def(
        "announce_completion",
        "Speaks a completion announcement using macOS say command without robotic -v flags, or desktop notification.",
        R"mcp({"type":"object","properties":{"message":{"type":"string","description":"Phrase to announce (e.g. 'Build succeeded and unit tests passed')"}},"required":["message"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_announce_completion_params p{};
                auto err = glz::read_json(p, params);
                if (err || p.message.empty()) {
                    return R"({"status":"error","message":"message parameter is required"})";
                }

                if (!rouen::platform::is_windows) {
                    std::string clean_msg = p.message;
                    std::string escaped;
                    for (char c : clean_msg) {
                        if (c == '"' || c == '\\' || c == '$' || c == '`') escaped.push_back('\\');
                        escaped.push_back(c);
                    }
                    ProcessHelper::executeCommand("say \"" + escaped + "\" &");
                }
                return std::format(R"({{"status":"success","message":"{}"}})", p.message);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "terminal"
    );
    register_function("terminal", announce_completion_def);
    register_function("editor", announce_completion_def);


    // Register YouTube search videos function
    function_definition const youtube_search_def(
        "youtube_search_videos",
        "Search YouTube for videos using yt-dlp. Returns a list of video objects with id, title, url, duration, and channel.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"The search query term (e.g. 'cpp tutorial' or 'lofi hip hop')"}},"required":["query"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_youtube_search_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.query.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'query' field."})";
            }
            
            auto raw_results = helpers::ytdlp_service::search(request.query, 10);
            std::vector<mcp_youtube_video> results;
            for (const auto& item : raw_results) {
                mcp_youtube_video video;
                video.id = item.id;
                video.title = item.title;
                video.url = item.url;
                video.duration = item.duration_string;
                video.channel = item.channel;
                results.push_back(std::move(video));
            }
            
            std::string response_str;
            auto ec_write = glz::write_json(results, response_str);
            (void)ec_write;
            return response_str;
        },
        "deck"
    );
    
    register_function("deck", youtube_search_def);

    // Register YouTube play video function
    function_definition const youtube_play_def(
        "youtube_play_video",
        "Play a YouTube video in the media player and bring up the YouTube card.",
        R"mcp({"type":"object","properties":{"url":{"type":"string","description":"The YouTube video URL to play"},"title":{"type":"string","description":"The title of the video"}},"required":["url","title"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_youtube_play_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.url.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'url' and 'title' fields."})";
            }
            
            try {
                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                if (create_card_fn) {
                    std::string const card_uri = "youtube:play:" + ::helpers::StringHelper::url_encode(request.url) + "|" + ::helpers::StringHelper::url_encode(request.title);
                    (*create_card_fn)(card_uri);
                    return R"({"status":"success","message":"Started playing video"})";
                }
                return R"({"status":"error","message":"create_card service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", youtube_play_def);

    // Register YouTube create search card function
    function_definition const youtube_create_card_def(
        "youtube_create_search_card",
        "Create a new YouTube search card with the search query pre-filled.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"The search query to pre-fill"}},"required":["query"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_youtube_create_card_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.query.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'query' field."})";
            }
            
            try {
                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                if (create_card_fn) {
                    std::string const card_uri = "youtube:" + ::helpers::StringHelper::url_encode(request.query);
                    (*create_card_fn)(card_uri);
                    return R"({"status":"success","message":"YouTube search card created successfully"})";
                }
                return R"({"status":"error","message":"create_card service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", youtube_create_card_def);

    // Register Wikipedia search concepts function
    function_definition const wikipedia_search_def(
        "wikipedia_search_concepts",
        "Search Wikipedia for articles/concepts and return a list of matching titles and snippets. Use this first when asked to summarize or answer questions about a topic to find the correct title.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"The search query term (e.g. 'c++' or 'albert einstein')"}},"required":["query"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_wikipedia_search_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.query.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'query' field."})";
            }
            
            try {
                std::string const encoded_query = ::helpers::StringHelper::url_encode(request.query);
                std::string const url = "https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=" + encoded_query + "&format=json&utf8=";
                
                std::vector<std::string> const headers = {
                    "User-Agent: RouenWikipediaCard/1.0 (ignacionr@github.com; ignacionr) libcurl/8.x",
                    "Accept: application/json"
                };
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t resp;
                auto ec = glz::read_json(resp, response);
                std::vector<mcp_wikipedia_result_item> results;
                
                if (!ec && resp.contains("query") && resp["query"].contains("search") && resp["query"]["search"].is_array()) {
                    auto& search_arr = resp["query"]["search"].get<glz::json_t::array_t>();
                    for (auto& item : search_arr) {
                        mcp_wikipedia_result_item res;
                        if (item.contains("title") && item["title"].is_string()) {
                            res.title = item["title"].get<std::string>();
                        }
                        if (item.contains("pageid") && item["pageid"].is_number()) {
                            res.pageid = static_cast<int>(item["pageid"].get<double>());
                        }
                        if (item.contains("snippet") && item["snippet"].is_string()) {
                            res.snippet = ::helpers::StringHelper::strip_html_tags(item["snippet"].get<std::string>());
                        }
                        results.push_back(std::move(res));
                    }
                }
                
                std::string response_str;
                auto ec_write = glz::write_json(results, response_str);
                (void)ec_write;
                return response_str;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", wikipedia_search_def);

    // Register Wikipedia get article text function
    function_definition const wikipedia_get_article_def(
        "wikipedia_get_article_text",
        "Obtain the full plain text content and URL of a Wikipedia article by its title. Use this to read the article contents to summarize or answer questions in the chat. DO NOT open a card on the screen unless the user explicitly requests to show/view the Wikipedia card.",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"The exact title of the Wikipedia page (e.g. 'C++' or 'Albert Einstein')"}},"required":["title"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_wikipedia_get_article_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'title' field."})";
            }
            
            try {
                std::string const encoded_title = ::helpers::StringHelper::url_encode(request.title);
                std::string const url = "https://en.wikipedia.org/w/api.php?action=query&prop=extracts&explaintext=1&titles=" + encoded_title + "&format=json";
                
                std::vector<std::string> const headers = {
                    "User-Agent: RouenWikipediaCard/1.0 (ignacionr@github.com; ignacionr) libcurl/8.x",
                    "Accept: application/json"
                };
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t resp;
                auto ec = glz::read_json(resp, response);
                std::string extract;
                std::string found_title = request.title;
                
                if (!ec && resp.contains("query") && resp["query"].contains("pages") && resp["query"]["pages"].is_object()) {
                    auto& pages = resp["query"]["pages"].get<glz::json_t::object_t>();
                    for (auto& [page_id, page_data] : pages) {
                        if (page_data.contains("extract") && page_data["extract"].is_string()) {
                            extract = page_data["extract"].get<std::string>();
                        }
                        if (page_data.contains("title") && page_data["title"].is_string()) {
                            found_title = page_data["title"].get<std::string>();
                        }
                    }
                }
                
                if (extract.empty()) {
                    return R"({"status":"error","message":"Article not found or empty"})";
                }
                
                mcp_wikipedia_article_result res;
                res.title = found_title;
                res.content = extract;
                
                std::string title_under = found_title;
                std::replace(title_under.begin(), title_under.end(), ' ', '_');
                res.url = "https://en.wikipedia.org/wiki/" + ::helpers::StringHelper::url_encode(title_under);
                
                std::string response_str;
                auto ec_write = glz::write_json(res, response_str);
                (void)ec_write;
                return response_str;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", wikipedia_get_article_def);

    // Register Wikipedia create card function
    function_definition const wikipedia_create_card_def(
        "wikipedia_create_card",
        "Create a Wikipedia search and browsing card on the user's screen for them to browse. DO NOT use this if you need to summarize or answer questions in the chat - use wikipedia_get_article_text instead.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"Optional search query or page title to display (e.g. 'c++' or 'title:C++')"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            std::string query;
            if (!params.empty()) {
                mcp_wikipedia_create_card_params request{};
                auto parse_result = glz::read_json(request, params);
                if (!parse_result) {
                    query = request.query;
                }
            }
            
            try {
                auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                if (create_card_fn) {
                    std::string card_uri = "wikipedia";
                    if (!query.empty()) {
                        card_uri += ":" + ::helpers::StringHelper::url_encode(query);
                    }
                    (*create_card_fn)(card_uri);
                    return R"({"status":"success","message":"Wikipedia card created successfully"})";
                }
                return R"({"status":"error","message":"create_card service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    
    register_function("deck", wikipedia_create_card_def);

    // Register notes functions
    function_definition const notes_list_def(
        "notes_list",
        "Search and list markdown notes. Call this with empty parameters {} to list all note titles currently available. You can also filter by an optional search query or a specific tag. Returns title and tags for each matching note.",
        R"mcp({"type":"object","properties":{"search":{"type":"string","description":"Optional search term to match in note title or content"},"tag":{"type":"string","description":"Optional tag to filter notes by"}}})mcp",
        [](const std::string& params) -> std::string {
            std::string search;
            std::string tag;
            if (!params.empty()) {
                mcp_notes_list_params request{};
                auto parse_result = glz::read_json(request, params);
                if (!parse_result) {
                    search = request.search;
                    tag = request.tag;
                }
            }

            try {
                models::notes::notes_repository repo;
                auto notes = repo.list_notes(search, tag);
                std::vector<mcp_note_summary> summaries;
                summaries.reserve(notes.size());

                for (const auto& note : notes) {
                    mcp_note_summary summary;
                    summary.id = note.id;
                    summary.title = note.title;
                    summary.tags = note.tags;
                    summaries.push_back(std::move(summary));
                }

                std::string response;
                auto ec = glz::write_json(summaries, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize notes list"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "notes"
    );
    register_function("notes", notes_list_def);

    // 2. notes_get
    function_definition const notes_get_def(
        "notes_get",
        "Retrieve the full markdown content, title, tags, and timestamps of a specific note by its title (for example, 'Personal Data').",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"The exact title of the note to retrieve"}},"required":["title"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }

            mcp_notes_get_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'title' field."})";
            }

            try {
                models::notes::notes_repository repo;
                auto note = repo.get_note_by_title(request.title);
                if (!note.has_value()) {
                    return std::format(R"({{"status":"error","message":"Note with title '{}' not found"}})", request.title);
                }

                mcp_note_detail detail;
                detail.id = note->id;
                detail.title = note->title;
                detail.content = note->content;
                detail.tags = note->tags;
                detail.created_at = note->created_at;
                detail.updated_at = note->updated_at;

                std::string response;
                auto ec = glz::write_json(detail, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize note details"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "notes"
    );
    register_function("notes", notes_get_def);

    // 3. notes_save
    function_definition const notes_save_def(
        "notes_save",
        "Create a new markdown note or overwrite an existing one with the specified title, content, and tags.",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"The title of the note"},"content":{"type":"string","description":"The full markdown content of the note"},"tags":{"type":"string","description":"Optional comma-separated tags (e.g. 'work,notes')"}},"required":["title","content"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }

            mcp_notes_save_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty() || request.content.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'title' and 'content' fields."})";
            }

            try {
                models::notes::notes_repository repo;
                int const note_id = repo.save_note(request.title, request.content, request.tags);
                
                mcp_notes_operation_result result{"success", "Note saved successfully", note_id};
                std::string response;
                auto ec = glz::write_json(result, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize response"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "notes"
    );
    register_function("notes", notes_save_def);

    // 4. notes_append
    function_definition const notes_append_def(
        "notes_append",
        "Append text content to the end of an existing note. If the note does not exist, an error is returned.",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"The title of the note to append to"},"content_to_append":{"type":"string","description":"The text content to append to the end of the note"}},"required":["title","content_to_append"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }

            mcp_notes_append_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty() || request.content_to_append.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'title' and 'content_to_append' fields."})";
            }

            try {
                models::notes::notes_repository repo;
                auto note = repo.get_note_by_title(request.title);
                if (!note.has_value()) {
                    return std::format(R"({{"status":"error","message":"Note with title '{}' not found"}})", request.title);
                }

                std::string new_content = note->content;
                if (!new_content.empty() && new_content.back() != '\n') {
                    new_content += "\n";
                }
                if (!new_content.empty()) {
                    new_content += "\n";
                }
                new_content += request.content_to_append;

                int const note_id = repo.save_note(note->title, new_content, note->tags);
                
                mcp_notes_operation_result result{"success", "Content appended successfully", note_id};
                std::string response;
                auto ec = glz::write_json(result, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize response"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "notes"
    );
    register_function("notes", notes_append_def);

    // 5. notes_delete
    function_definition const notes_delete_def(
        "notes_delete",
        "Delete a note by its title.",
        R"mcp({"type":"object","properties":{"title":{"type":"string","description":"The exact title of the note to delete"}},"required":["title"]})mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }

            mcp_notes_delete_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.title.empty()) {
                return R"({"status":"error","message":"Invalid params. Expected 'title' field."})";
            }

            try {
                models::notes::notes_repository repo;
                auto note = repo.get_note_by_title(request.title);
                if (!note.has_value()) {
                    return std::format(R"({{"status":"error","message":"Note with title '{}' not found"}})", request.title);
                }

                bool const deleted = repo.delete_note(note->id);
                if (!deleted) {
                    return R"({"status":"error","message":"Failed to delete note"})";
                }

                mcp_notes_operation_result result{"success", "Note deleted successfully", note->id};
                std::string response;
                auto ec = glz::write_json(result, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize response"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "notes"
    );
    register_function("notes", notes_delete_def);

    // ----------------------------------------------------
    // Contacts Directory MCP Functions
    // ----------------------------------------------------
    function_definition const contacts_list_def(
        "contacts_list",
        "List or search contacts in the Directory card database.",
        R"mcp({"type":"object","properties":{"query":{"type":"string","description":"Search term to filter contacts"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                std::string query;
                if (!params.empty()) {
                    mcp_contacts_list_params req{};
                    (void)glz::read_json(req, params);
                    query = req.query;
                }
                models::contacts::contacts_repository repo;
                auto contacts = repo.search_contacts(query);
                std::vector<models::contacts::contact_dto> dtos;
                dtos.reserve(contacts.size());
                for (const auto& c : contacts) dtos.push_back(models::contacts::to_dto(c));

                std::string response;
                if (auto ec = glz::write_json(dtos, response); ec) {
                    return R"({"status":"error","message":"Failed to serialize contacts"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_list_def);
    register_function("contacts", contacts_list_def);

    function_definition const contacts_get_def(
        "contacts_get",
        "Get detailed information about a specific contact by ID or name.",
        R"mcp({"type":"object","properties":{"id":{"type":"integer","description":"Contact ID"},"name":{"type":"string","description":"Contact display name"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_contacts_get_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);

                models::contacts::contacts_repository repo;
                std::optional<models::contacts::contact> target;

                if (req.id > 0) {
                    target = repo.get_contact_by_id(req.id);
                } else if (!req.name.empty()) {
                    auto search_res = repo.search_contacts(req.name);
                    if (!search_res.empty()) target = search_res.front();
                }

                if (!target.has_value()) {
                    return R"({"status":"error","message":"Contact not found"})";
                }

                auto dto = models::contacts::to_dto(target.value());
                std::string response;
                if (auto ec = glz::write_json(dto, response); ec) {
                    return R"({"status":"error","message":"Failed to serialize contact"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_get_def);
    register_function("contacts", contacts_get_def);

    function_definition const contacts_save_def(
        "contacts_save",
        "Create a new contact or update an existing contact in the Contacts Directory.",
        R"mcp({"type":"object","properties":{"id":{"type":"integer"},"first_name":{"type":"string"},"last_name":{"type":"string"},"display_name":{"type":"string"},"organization":{"type":"string"},"job_title":{"type":"string"},"email":{"type":"string"},"phone":{"type":"string"},"address":{"type":"string"},"notes":{"type":"string"},"picture_url":{"type":"string"}},"required":["display_name"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_contacts_save_params req{};
                if (auto ec = glz::read_json(req, params); ec) {
                    return R"({"status":"error","message":"Invalid contact save parameters"})";
                }

                models::contacts::contact c;
                c.id = req.id;
                c.first_name = req.first_name;
                c.last_name = req.last_name;
                c.display_name = req.display_name;
                c.organization = req.organization;
                c.job_title = req.job_title;
                c.email = req.email;
                c.phone = req.phone;
                c.address = req.address;
                c.notes = req.notes;
                c.picture_url = req.picture_url;
                c.source = "mcp";

                models::contacts::contacts_repository repo;
                int64_t saved_id = repo.upsert_contact(c);
                helpers::UniversalSyncService::instance().sync_out("MCP saved contact: " + c.get_full_name());

                return std::format(R"({{"status":"success","message":"Contact saved","id":{}}})", saved_id);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_save_def);
    register_function("contacts", contacts_save_def);

    function_definition const contacts_delete_def(
        "contacts_delete",
        "Delete a contact by ID from the directory.",
        R"mcp({"type":"object","properties":{"id":{"type":"integer","description":"ID of contact to delete"}},"required":["id"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_contacts_delete_params req{};
                if (auto ec = glz::read_json(req, params); ec || req.id <= 0) {
                    return R"mcp({"status":"error","message":"Invalid contact delete parameters (must specify positive id)"})mcp";
                }

                models::contacts::contacts_repository repo;
                repo.delete_contact(req.id);
                helpers::UniversalSyncService::instance().sync_out("MCP deleted contact ID: " + std::to_string(req.id));

                return std::format(R"({{"status":"success","message":"Contact deleted","id":{}}})", req.id);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_delete_def);
    register_function("contacts", contacts_delete_def);

    function_definition const contacts_import_macos_def(
        "contacts_import_macos",
        "Import contacts from local macOS Contacts app into the directory.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                models::contacts::contacts_repository repo;
                int count = repo.import_macos_contacts();
                helpers::UniversalSyncService::instance().sync_out("Imported macOS contacts via MCP");
                return std::format(R"({{"status":"success","imported_count":{}}})", count);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_import_macos_def);
    register_function("contacts", contacts_import_macos_def);

    // Register Calendar MCP functions
    function_definition const get_calendar_events_def(
        "get_calendar_events",
        "Fetch upcoming or date-filtered calendar events from the local system/macOS Calendar app or delegate service. Call with empty parameters {} to list all current/upcoming events, or optionally pass start_date and end_date (e.g. '2026-09-11' or ISO-8601 strings).",
        R"mcp({"type":"object","properties":{"start_date":{"type":"string","description":"Optional start date for filtering (YYYY-MM-DD or ISO string)"},"end_date":{"type":"string","description":"Optional end date for filtering (YYYY-MM-DD or ISO string)"}}})mcp",
        [](const std::string& params) -> std::string {
            std::string start_date;
            std::string end_date;
            if (!params.empty()) {
                mcp_get_calendar_events_params request{};
                auto parse_result = glz::read_json(request, params);
                if (!parse_result) {
                    start_date = request.start_date;
                    end_date = request.end_date;
                }
            }

            try {
                calendar::calendar_fetcher fetcher;
                auto events = fetcher.fetch_events(start_date, end_date);
                if (fetcher.has_error() && events.empty()) {
                    return std::format(R"({{"status":"error","message":"{}"}})", fetcher.last_error());
                }

                std::vector<mcp_calendar_event_dto> dtos;
                dtos.reserve(events.size());
                for (const auto& ev : events) {
                    dtos.push_back(mcp_calendar_event_dto{
                        .id = ev.id,
                        .summary = ev.summary,
                        .description = ev.description,
                        .location = ev.location,
                        .start = ev.start,
                        .end = ev.end,
                        .creator = ev.creator,
                        .organizer = ev.organizer,
                        .all_day = ev.all_day
                    });
                }

                std::string response;
                auto ec = glz::write_json(dtos, response);
                if (ec) {
                    return R"({"status":"error","message":"Failed to serialize calendar events list"})";
                }
                return response;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "calendar"
    );
    register_function("calendar", get_calendar_events_def);

    function_definition const create_calendar_event_def(
        "create_calendar_event",
        "Create a new calendar appointment/event in the local calendar. Parameters specify title, description, location, calendar name, start and end dates/times.",
        R"mcp({"type":"object","properties":{"calendar_name":{"type":"string","description":"Target calendar name (e.g. 'Calendar' or 'Work')"},"summary":{"type":"string","description":"Event title / summary"},"description":{"type":"string","description":"Event description"},"location":{"type":"string","description":"Event location"},"start_year":{"type":"integer"},"start_month":{"type":"integer"},"start_day":{"type":"integer"},"start_hour":{"type":"integer"},"start_min":{"type":"integer"},"end_year":{"type":"integer"},"end_month":{"type":"integer"},"end_day":{"type":"integer"},"end_hour":{"type":"integer"},"end_min":{"type":"integer"},"is_all_day":{"type":"boolean"}},"required":["summary","start_year","start_month","start_day"]})mcp",
        [](const std::string& params) -> std::string {
            mcp_create_calendar_event_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result) {
                return R"({"status":"error","message":"Invalid parameters for create_calendar_event"})";
            }

            try {
                calendar::calendar_fetcher fetcher;
                bool ok = fetcher.create_event(
                    request.calendar_name,
                    request.summary,
                    request.description,
                    request.location,
                    request.start_year, request.start_month, request.start_day, request.start_hour, request.start_min,
                    request.end_year, request.end_month, request.end_day, request.end_hour, request.end_min,
                    request.is_all_day
                );

                if (ok) {
                    return std::format(R"({{"status":"success","message":"Calendar event '{}' created successfully"}})", request.summary);
                } else {
                    return std::format(R"({{"status":"error","message":"{}"}})", fetcher.has_error() ? fetcher.last_error() : "Failed to create calendar event");
                }
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "calendar"
    );
    register_function("calendar", create_calendar_event_def);

    // Register Persona MCP functions
    function_definition const list_personas_def(
        "list_personas",
        "List all available AI personas in Rouen, including their descriptions, system prompts, allowed MCP tools, and current active selection status.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& pm = helpers::PersonaManager::instance();
                const auto& personas = pm.get_personas();
                size_t active_idx = pm.get_active_persona_index();

                std::string full_buffer;
                (void)glz::write_json(personas, full_buffer);

                return std::format(
                    R"({{"status":"success","active_index":{},"active_persona_name":"{}","personas":{}}})",
                    active_idx,
                    active_idx < personas.size() ? personas[active_idx].name : "",
                    full_buffer
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", list_personas_def);
    register_function("persona", list_personas_def);

    function_definition const enable_persona_def(
        "enable_persona",
        "Enable or switch the active AI persona in Rouen by name or 0-based index (e.g. 'Data Cruncher', 'Terminal Hack', 'Rouen Assistant').",
        R"mcp({"type":"object","properties":{"name":{"type":"string","description":"Name of the persona to enable (e.g. 'Data Cruncher', 'Terminal Hack', 'Rouen Assistant')"},"index":{"type":"integer","description":"0-based index of the persona in the persona list"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_enable_persona_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }

                auto& pm = helpers::PersonaManager::instance();
                const auto& personas = pm.get_personas();

                int target_idx = -1;
                if (!req.name.empty()) {
                    std::string const target_name = ::helpers::StringHelper::to_lower(req.name);
                    for (size_t i = 0; i < personas.size(); ++i) {
                        if (::helpers::StringHelper::to_lower(personas[i].name) == target_name) {
                            target_idx = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (target_idx < 0 && req.index >= 0 && req.index < static_cast<int>(personas.size())) {
                    target_idx = req.index;
                }

                if (target_idx < 0) {
                    return R"({"status":"error","message":"Persona not found by given name or index"})";
                }

                pm.select_persona(static_cast<size_t>(target_idx));
                const auto& active = pm.get_active_persona();

                return std::format(
                    R"({{"status":"success","message":"Persona '{}' enabled successfully","active_index":{},"active_persona_name":"{}"}})",
                    active.name, target_idx, active.name
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", enable_persona_def);
    register_function("persona", enable_persona_def);

    function_definition const get_active_persona_def(
        "get_active_persona",
        "Get information about the currently active AI persona in Rouen.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& pm = helpers::PersonaManager::instance();
                const auto& active = pm.get_active_persona();
                size_t active_idx = pm.get_active_persona_index();

                std::string persona_json;
                (void)glz::write_json(active, persona_json);

                return std::format(
                    R"({{"status":"success","active_index":{},"persona":{}}})",
                    active_idx, persona_json
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", get_active_persona_def);
    register_function("persona", get_active_persona_def);

    // Theme Management Functions
    function_definition const list_themes_def(
        "list_themes",
        "Lists all available color themes in Rouen (e.g. Amber, Dark, Light, Cyberpunk, Nord) and reports the currently active theme.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& tm = rouen::theme::theme_manager::get();
                const auto& themes = tm.get_themes();
                size_t const active_idx = tm.get_active_theme_index();

                std::vector<std::string> names;
                names.reserve(themes.size());
                for (const auto& t : themes) {
                    names.push_back(t.name);
                }

                std::string const active_name = (active_idx < themes.size()) ? themes[active_idx].name : "Unknown";

                mcp_themes_response resp;
                resp.active_index = active_idx;
                resp.active_theme = active_name;
                resp.themes = std::move(names);

                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", list_themes_def);
    register_function("theme", list_themes_def);

    function_definition const select_theme_def(
        "select_theme",
        "Selects and applies an active color theme in Rouen by name or 0-based index (e.g. 'Amber', 'Dark', 'Light', 'Cyberpunk', 'Nord').",
        R"mcp({"type":"object","properties":{"name":{"type":"string","description":"Name of the theme to apply (e.g. 'Amber', 'Dark', 'Light', 'Cyberpunk', 'Nord')"},"index":{"type":"integer","description":"0-based index of the theme"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                auto& tm = rouen::theme::theme_manager::get();
                const auto& themes = tm.get_themes();

                mcp_select_theme_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }

                int target_idx = -1;
                if (!req.name.empty()) {
                    std::string const target_lower = ::helpers::StringHelper::to_lower(req.name);
                    for (size_t i = 0; i < themes.size(); ++i) {
                        if (::helpers::StringHelper::to_lower(themes[i].name) == target_lower) {
                            target_idx = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (target_idx < 0 && req.index >= 0 && req.index < static_cast<int>(themes.size())) {
                    target_idx = req.index;
                }

                if (target_idx < 0) {
                    return R"({"status":"error","message":"Theme not found by given name or index"})";
                }

                tm.select_theme(static_cast<size_t>(target_idx));
                const auto& active = tm.get_active_theme();
                return std::format(
                    R"({{"status":"success","message":"Theme '{}' selected successfully","active_index":{},"active_theme":"{}"}})",
                    active.name, target_idx, active.name
                );
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", select_theme_def);
    register_function("theme", select_theme_def);

    // ----------------------------------------------------
    // REST API Parity MCP Functions
    // ----------------------------------------------------

    // 1. list_open_cards
    function_definition const list_open_cards_def(
        "list_open_cards",
        "List all cards currently open and active in the Rouen deck view, returning their 0-based indices, titles, URIs, and widths.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto active_cards_func = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
                if (!active_cards_func || !*active_cards_func) {
                    return R"({"status":"error","message":"Active cards service not available"})";
                }
                auto cards = (*active_cards_func)();
                std::vector<glz::json_t> cards_arr;
                cards_arr.reserve(cards.size());
                for (size_t i = 0; i < cards.size(); ++i) {
                    if (!cards[i]) continue;
                    glz::json_t card_obj;
                    card_obj["index"] = static_cast<double>(i);
                    card_obj["title"] = cards[i]->window_title;
                    card_obj["uri"] = cards[i]->get_uri();
                    card_obj["width"] = static_cast<double>(cards[i]->width);
                    cards_arr.push_back(std::move(card_obj));
                }
                std::string out;
                (void)glz::write_json(cards_arr, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", list_open_cards_def);

    // 2. close_card
    function_definition const close_card_def(
        "close_card",
        "Close an active card in the deck by its 0-based index or URI. If no index or URI is provided, closes the currently focused card.",
        R"mcp({"type":"object","properties":{"index":{"type":"integer","description":"0-based index of the card to close"},"uri":{"type":"string","description":"URI of the card to close"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                int target_index = -1;
                std::string target_uri;
                if (!params.empty()) {
                    mcp_close_card_params req{};
                    (void)glz::read_json(req, params);
                    target_index = req.index;
                    target_uri = req.uri;
                }
                bool closed = false;
                auto close_by_index_fn = registrar::get<std::function<bool(size_t)>>("close_card_index");
                auto close_by_uri_fn = registrar::get<std::function<bool(const std::string&)>>("close_card");

                if (target_index >= 0 && close_by_index_fn && *close_by_index_fn) {
                    closed = (*close_by_index_fn)(static_cast<size_t>(target_index));
                } else if (!target_uri.empty() && close_by_uri_fn && *close_by_uri_fn) {
                    closed = (*close_by_uri_fn)(target_uri);
                } else {
                    auto close_focused_fn = registrar::get<std::function<bool()>>("close_focused_card");
                    if (close_focused_fn && *close_focused_fn) {
                        closed = (*close_focused_fn)();
                    }
                }

                if (closed) {
                    return R"({"status":"success","message":"Card closed successfully"})";
                } else {
                    return R"({"status":"error","message":"Card not found or could not be closed"})";
                }
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", close_card_def);

    // 3. focus_card
    function_definition const focus_card_def(
        "focus_card",
        "Focus and bring an open card into active view in the deck by its 0-based index or URI.",
        R"mcp({"type":"object","properties":{"index":{"type":"integer","description":"0-based index of card to focus"},"uri":{"type":"string","description":"URI of card to focus"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_focus_card_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                if (req.index >= 0) {
                    auto fn = registrar::get<std::function<void(size_t)>>("focus_card_index");
                    if (fn && *fn) {
                        (*fn)(static_cast<size_t>(req.index));
                        return std::format(R"({{"status":"success","message":"Focused card at index {}"}})", req.index);
                    }
                }
                if (!req.uri.empty()) {
                    auto fn = registrar::get<std::function<void(const std::string&)>>("focus_card");
                    if (fn && *fn) {
                        (*fn)(req.uri);
                        return std::format(R"({{"status":"success","message":"Focused card with URI {}"}})", req.uri);
                    }
                }
                return R"({"status":"error","message":"Focus card service not available or invalid parameters"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", focus_card_def);

    // 4. scroll_deck
    function_definition const scroll_deck_def(
        "scroll_deck",
        "Scroll the deck view to a specific section index, or get current deck status.",
        R"mcp({"type":"object","properties":{"section":{"type":"integer","description":"Section index to scroll deck to"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                if (params.empty() || params == "{}") {
                    auto fn = registrar::get<std::function<std::string()>>("get_deck_status");
                    if (fn && *fn) {
                        return (*fn)();
                    }
                }
                mcp_scroll_deck_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                auto fn = registrar::get<std::function<void(int)>>("scroll_to_section");
                if (fn && *fn) {
                    (*fn)(req.section);
                    return std::format(R"({{"status":"success","message":"Scrolled deck to section {}"}})", req.section);
                }
                return R"({"status":"error","message":"Scroll deck service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", scroll_deck_def);

    // 5. execute_card_action
    function_definition const execute_card_action_def(
        "execute_card_action",
        "Dispatch an Adaptive Cards action payload (e.g. Action.Execute or form submission) to an active open card by index or URI.",
        R"mcp({"type":"object","properties":{"index":{"type":"integer","description":"Target card index"},"uri":{"type":"string","description":"Target card URI"},"action":{"type":"string","description":"JSON action payload string to dispatch"}},"required":["action"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_execute_card_action_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                auto active_cards_func = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
                if (!active_cards_func || !*active_cards_func) {
                    return R"({"status":"error","message":"Active cards service not available"})";
                }
                auto cards = (*active_cards_func)();
                std::shared_ptr<card> target_card;
                size_t found_index = 0;
                if (req.index >= 0 && static_cast<size_t>(req.index) < cards.size()) {
                    target_card = cards[static_cast<size_t>(req.index)];
                    found_index = static_cast<size_t>(req.index);
                } else if (!req.uri.empty()) {
                    for (size_t i = 0; i < cards.size(); ++i) {
                        if (cards[i] && (cards[i]->get_uri() == req.uri || cards[i]->get_uri().starts_with(req.uri))) {
                            target_card = cards[i];
                            found_index = i;
                            break;
                        }
                    }
                }
                if (!target_card) {
                    return R"({"status":"error","message":"Card not found for action dispatch"})";
                }
                target_card->handle_action(req.action);
                glz::json_t resp_obj;
                resp_obj["status"] = "success";
                resp_obj["message"] = "Action payload dispatched successfully";
                resp_obj["index"] = static_cast<double>(found_index);
                resp_obj["title"] = target_card->window_title;
                resp_obj["uri"] = target_card->get_uri();
                std::string out;
                (void)glz::write_json(resp_obj, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", execute_card_action_def);
    register_function("adaptive_card", execute_card_action_def);

    // 6. get_active_card_adaptive
    function_definition const get_active_card_adaptive_def(
        "get_active_card_adaptive",
        "Get Adaptive Card JSON structure and state for all active open cards or a specific card by index/URI.",
        R"mcp({"type":"object","properties":{"index":{"type":"integer","description":"Optional card index"},"uri":{"type":"string","description":"Optional card URI"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_get_active_card_adaptive_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                auto active_cards_func = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
                if (!active_cards_func || !*active_cards_func) {
                    return R"({"status":"error","message":"Active cards service not available"})";
                }
                auto cards = (*active_cards_func)();
                auto parse_adaptive = [](const std::shared_ptr<card>& c) -> glz::json_t {
                    if (!c) return nullptr;
                    std::string card_json = c->get_adaptive_card_json();
                    if (card_json.empty()) return nullptr;
                    glz::json_t obj;
                    auto err = glz::read_json(obj, card_json);
                    if (!err) return obj;
                    return card_json;
                };

                std::shared_ptr<card> target_card;
                size_t found_index = 0;
                if (req.index >= 0 && static_cast<size_t>(req.index) < cards.size()) {
                    target_card = cards[static_cast<size_t>(req.index)];
                    found_index = static_cast<size_t>(req.index);
                } else if (!req.uri.empty()) {
                    for (size_t i = 0; i < cards.size(); ++i) {
                        if (cards[i] && (cards[i]->get_uri() == req.uri || cards[i]->get_uri().starts_with(req.uri))) {
                            target_card = cards[i];
                            found_index = i;
                            break;
                        }
                    }
                }

                if (req.index >= 0 || !req.uri.empty()) {
                    if (!target_card) {
                        return R"({"status":"error","message":"Card not found for specified index or uri"})";
                    }
                    glz::json_t resp_obj;
                    resp_obj["status"] = "success";
                    resp_obj["index"] = static_cast<double>(found_index);
                    resp_obj["title"] = target_card->window_title;
                    resp_obj["uri"] = target_card->get_uri();
                    resp_obj["adaptive_card"] = parse_adaptive(target_card);
                    std::string out;
                    (void)glz::write_json(resp_obj, out);
                    return out;
                }

                std::vector<glz::json_t> cards_arr;
                cards_arr.reserve(cards.size());
                for (size_t i = 0; i < cards.size(); ++i) {
                    if (!cards[i]) continue;
                    glz::json_t card_obj;
                    card_obj["index"] = static_cast<double>(i);
                    card_obj["title"] = cards[i]->window_title;
                    card_obj["uri"] = cards[i]->get_uri();
                    card_obj["adaptive_card"] = parse_adaptive(cards[i]);
                    cards_arr.push_back(std::move(card_obj));
                }
                glz::json_t resp_obj;
                resp_obj["status"] = "success";
                resp_obj["cards"] = std::move(cards_arr);
                std::string out;
                (void)glz::write_json(resp_obj, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", get_active_card_adaptive_def);
    register_function("adaptive_card", get_active_card_adaptive_def);

    // 7. get_window_geometry & set_window_geometry
    function_definition const get_window_geometry_def(
        "get_window_geometry",
        "Get main Rouen application window position (x, y) and dimensions (width, height).",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto get_window_fn = registrar::get<std::function<SDL_Window*()>>("get_window");
                if (!get_window_fn || !*get_window_fn) {
                    return R"({"status":"error","message":"Window service not available"})";
                }
                SDL_Window* window = (*get_window_fn)();
                if (!window) {
                    return R"({"status":"error","message":"Window instance not available"})";
                }
                int x = 0, y = 0, w = 0, h = 0;
                SDL_GetWindowPosition(window, &x, &y);
                SDL_GetWindowSize(window, &w, &h);
                return std::format(R"({{"status":"success","x":{},"y":{},"width":{},"height":{}}})", x, y, w, h);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "window"
    );
    register_function("window", get_window_geometry_def);
    register_function("deck", get_window_geometry_def);

    function_definition const set_window_geometry_def(
        "set_window_geometry",
        "Set main Rouen window position (x, y) and/or size dimensions (width, height).",
        R"mcp({"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"},"width":{"type":"integer"},"height":{"type":"integer"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                auto get_window_fn = registrar::get<std::function<SDL_Window*()>>("get_window");
                if (!get_window_fn || !*get_window_fn) {
                    return R"({"status":"error","message":"Window service not available"})";
                }
                SDL_Window* window = (*get_window_fn)();
                if (!window) {
                    return R"({"status":"error","message":"Window instance not available"})";
                }
                mcp_set_window_geometry_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                int cur_x = 0, cur_y = 0, cur_w = 0, cur_h = 0;
                SDL_GetWindowPosition(window, &cur_x, &cur_y);
                SDL_GetWindowSize(window, &cur_w, &cur_h);
                int new_x = (req.x != -1) ? req.x : cur_x;
                int new_y = (req.y != -1) ? req.y : cur_y;
                int new_w = (req.width > 0) ? req.width : cur_w;
                int new_h = (req.height > 0) ? req.height : cur_h;
                auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
                if (deferred_ops) {
                    deferred_ops->queue([window, req, new_x, new_y, new_w, new_h] {
                        if (req.x != -1 || req.y != -1) {
                            SDL_SetWindowPosition(window, new_x, new_y);
                        }
                        if (req.width > 0 || req.height > 0) {
                            SDL_SetWindowSize(window, new_w, new_h);
                        }
                    });
                }
                return std::format(R"({{"status":"success","message":"Window position and size updated","x":{},"y":{},"width":{},"height":{}}})", new_x, new_y, new_w, new_h);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "window"
    );
    register_function("window", set_window_geometry_def);
    register_function("deck", set_window_geometry_def);

    // 8. take_screenshot
    function_definition const take_screenshot_def(
        "take_screenshot",
        "Capture a visual screenshot of the Rouen application window, deck, selected card, or editor snapshot.",
        R"mcp({"type":"object","properties":{"target":{"type":"string","description":"Target to snapshot: 'deck', 'selected', or 'editor'"},"filename":{"type":"string","description":"Output image filepath (e.g. /tmp/snapshot.png)"},"width":{"type":"integer"},"height":{"type":"integer"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_take_screenshot_params req{};
                if (!params.empty()) {
                    (void)glz::read_json(req, params);
                }
                if (req.target.empty()) req.target = "deck";
                if (req.filename.empty()) {
                    std::error_code ec;
                    auto temp_dir = std::filesystem::temp_directory_path(ec);
                    req.filename = (ec ? std::filesystem::path("snapshot.png") : (temp_dir / "snapshot.png")).string();
                }
                auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
                if (deferred_ops) {
                    auto promise = std::make_shared<std::promise<std::string>>();
                    auto future = promise->get_future();
                    deferred_ops->queue([req, promise]() {
                        try {
                            auto screenshot_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("take_screenshot");
                            if (screenshot_fn && *screenshot_fn) {
                                promise->set_value((*screenshot_fn)(req.target, req.filename, req.width, req.height));
                                return;
                            }
                            if (req.target == "editor") {
                                auto ed_fn = registrar::get<std::function<std::string(const std::string&, int, int)>>("editor_save_snapshot");
                                if (ed_fn && *ed_fn) {
                                    promise->set_value((*ed_fn)(req.filename, req.width, req.height));
                                    return;
                                }
                            }
                            promise->set_value(R"({"status":"error","message":"Screenshot service not available"})");
                        } catch (const std::exception& e) {
                            promise->set_value(std::format(R"({{"status":"error","message":"{}"}})", e.what()));
                        }
                    });
                    if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
                        return future.get();
                    } else {
                        return R"({"status":"error","message":"Screenshot operation timed out waiting for main thread"})";
                    }
                }
                auto screenshot_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("take_screenshot");
                if (screenshot_fn && *screenshot_fn) {
                    return (*screenshot_fn)(req.target, req.filename, req.width, req.height);
                }
                return R"({"status":"error","message":"Screenshot service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    register_function("deck", take_screenshot_def);
    register_function("system", take_screenshot_def);

    // 9. Camera Tools
    function_definition const get_camera_status_def(
        "get_camera_status",
        "Get camera service status, resolution, and active streaming state.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto fn = registrar::get<std::function<std::string()>>("camera_get_status");
                if (fn && *fn) return (*fn)();
                return R"({"active":false,"message":"Camera service not active"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "camera"
    );
    register_function("camera", get_camera_status_def);

    function_definition const take_camera_snapshot_def(
        "take_camera_snapshot",
        "Save a frame snapshot from the active camera feed to a file.",
        R"mcp({"type":"object","properties":{"filename":{"type":"string","description":"Output snapshot file path (e.g. /tmp/camera_snapshot.ppm)"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                std::string filename = "/tmp/camera_snapshot.ppm";
                if (!params.empty()) {
                    glz::json_t obj;
                    if (!glz::read_json(obj, params) && obj.contains("filename") && obj["filename"].is_string()) {
                        filename = obj["filename"].get<std::string>();
                    }
                }
                auto fn = registrar::get<std::function<std::string(const std::string&)>>("camera_save_snapshot");
                if (fn && *fn) return (*fn)(filename);
                return R"({"status":"error","message":"Camera snapshot service not available"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "camera"
    );
    register_function("camera", take_camera_snapshot_def);

    function_definition const set_camera_layout_def(
        "set_camera_layout",
        "Get or set the camera tile grid layout / preset.",
        R"mcp({"type":"object","properties":{"layout":{"type":"string","description":"Layout descriptor string (e.g. '1x1', '2x2')"},"preset":{"type":"integer","description":"Preset index"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                auto set_fn = registrar::get<std::function<std::string(const std::string&)>>("camera_set_layout");
                auto get_fn = registrar::get<std::function<std::string()>>("camera_get_layout");
                if (params.empty() || params == "{}") {
                    if (get_fn && *get_fn) return (*get_fn)();
                    return R"({"status":"error","message":"Camera layout service not available"})";
                }
                if (!set_fn || !*set_fn) return R"({"status":"error","message":"Camera layout service not available"})";
                mcp_set_camera_layout_params req{};
                (void)glz::read_json(req, params);
                std::string const target = !req.layout.empty() ? req.layout : (req.preset >= 0 ? std::to_string(req.preset) : "0");
                return (*set_fn)(target);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "camera"
    );
    register_function("camera", set_camera_layout_def);

    // 10. Process Management & UI Automation Tools
    function_definition const list_processes_def(
        "list_processes",
        "List all configured background process definitions and their current running state, PIDs, and run IDs.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                rouen::models::productivity::process_definition_repository repo;
                auto defs = repo.get_all();
                glz::json_t root;
                std::vector<glz::json_t> defs_arr;
                for (const auto& def : defs) {
                    glz::json_t item;
                    item["id"] = def.id;
                    item["name"] = def.name;
                    item["executable_path"] = def.executable_path;
                    item["arguments"] = def.arguments;
                    item["working_directory"] = def.working_directory;
                    item["has_active_run"] = rouen::hosts::process_host::instance().has_active_run(def.id);
                    auto latest_run_id = rouen::hosts::process_host::instance().latest_run_id(def.id);
                    if (latest_run_id) {
                        item["latest_run_id"] = *latest_run_id;
                        auto snap = rouen::hosts::process_host::instance().snapshot(*latest_run_id);
                        if (snap) {
                            item["state"] = (snap->state == rouen::hosts::process_run_state::running) ? "running" :
                                            ((snap->state == rouen::hosts::process_run_state::exited) ? "exited" : "failed_to_start");
                            item["pid"] = snap->pid;
                            if (snap->exit_code) item["exit_code"] = *snap->exit_code;
                        }
                    } else {
                        item["state"] = "stopped";
                    }
                    defs_arr.push_back(std::move(item));
                }
                root["processes"] = std::move(defs_arr);
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", list_processes_def);

    function_definition const save_process_definition_def(
        "save_process_definition",
        "Create or update a background process definition. Provide id to update an existing definition, or omit id (or id <= 0) to create a new one.",
        R"mcp({"type":"object","properties":{"id":{"type":"integer","description":"Process definition numeric ID (omit or <= 0 for create, > 0 for update)"},"name":{"type":"string","description":"Process name"},"executable_path":{"type":"string","description":"Executable path"},"arguments":{"type":"string","description":"Command-line arguments"},"working_directory":{"type":"string","description":"Working directory"},"icon_source":{"type":"string","description":"Custom icon path"}},"required":["name","executable_path"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_save_process_definition_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                rouen::models::productivity::process_definition_repository repo;
                rouen::models::productivity::process_definition def;

                if (req.id > 0) {
                    auto existing = repo.get_by_id(req.id);
                    if (!existing) return std::format(R"({{"status":"error","message":"Process definition not found with id {}"}})", req.id);
                    def = *existing;
                    if (!req.name.empty()) def.name = req.name;
                    if (!req.executable_path.empty()) def.executable_path = req.executable_path;
                    if (!req.arguments.empty()) def.arguments = req.arguments;
                    if (!req.working_directory.empty()) def.working_directory = req.working_directory;
                    if (!req.icon_source.empty()) def.icon_source = req.icon_source;
                } else {
                    if (req.name.empty() || req.executable_path.empty()) {
                        return R"({"status":"error","message":"name and executable_path are required"})";
                    }
                    def.id = -1;
                    def.name = req.name;
                    def.executable_path = req.executable_path;
                    def.arguments = req.arguments;
                    def.working_directory = req.working_directory;
                    def.icon_source = req.icon_source;
                }

                int64_t saved_id = repo.upsert(def);
                auto saved = repo.get_by_id(saved_id);
                glz::json_t resp;
                resp["status"] = "success";
                resp["id"] = saved_id;
                if (saved) {
                    resp["name"] = saved->name;
                    resp["executable_path"] = saved->executable_path;
                    resp["arguments"] = saved->arguments;
                    resp["working_directory"] = saved->working_directory;
                    resp["icon_source"] = saved->icon_source;
                    resp["created"] = saved->created;
                    resp["updated"] = saved->updated;
                }
                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", save_process_definition_def);

    function_definition const delete_process_definition_def(
        "delete_process_definition",
        "Delete a background process definition by definition ID or name.",
        R"mcp({"type":"object","properties":{"id":{"type":"integer","description":"Process definition numeric ID"},"name":{"type":"string","description":"Process definition name"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_delete_process_definition_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                rouen::models::productivity::process_definition_repository repo;
                int64_t target_id = req.id;
                if (target_id <= 0 && !req.name.empty()) {
                    auto all = repo.get_all();
                    for (const auto& d : all) {
                        if (d.name == req.name) { target_id = d.id; break; }
                    }
                }
                if (target_id <= 0) return R"({"status":"error","message":"Valid process definition id or name required"})";
                auto existing = repo.get_by_id(target_id);
                if (!existing) return std::format(R"({{"status":"error","message":"Process definition not found with id {}"}})", target_id);
                repo.remove(target_id);
                glz::json_t resp;
                resp["status"] = "success";
                resp["id"] = target_id;
                resp["message"] = "Process definition deleted successfully";
                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", delete_process_definition_def);

    function_definition const start_process_def(
        "start_process",
        "Launch a configured process definition by definition ID or definition name.",
        R"mcp({"type":"object","properties":{"definition_id":{"type":"integer"},"definition_name":{"type":"string"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_start_process_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                rouen::models::productivity::process_definition_repository repo;
                std::optional<rouen::models::productivity::process_definition> def;
                if (req.definition_id > 0) {
                    def = repo.get_by_id(req.definition_id);
                } else if (!req.definition_name.empty()) {
                    auto all = repo.get_all();
                    for (const auto& d : all) {
                        if (d.name == req.definition_name) { def = d; break; }
                    }
                }
                if (!def) return R"({"status":"error","message":"Process definition not found"})";
                std::string run_id = rouen::hosts::process_host::instance().start(*def);
                auto snap = rouen::hosts::process_host::instance().snapshot(run_id);
                glz::json_t resp;
                resp["status"] = "success";
                resp["run_id"] = run_id;
                resp["definition_id"] = def->id;
                resp["definition_name"] = def->name;
                if (snap) {
                    resp["pid"] = snap->pid;
                    resp["state"] = (snap->state == rouen::hosts::process_run_state::running) ? "running" : "failed_to_start";
                    if (!snap->start_error.empty()) resp["start_error"] = snap->start_error;
                }
                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", start_process_def);

    function_definition const attach_process_def(
        "attach_process",
        "Attach Rouen process inspection tracking to an existing running OS process by PID.",
        R"mcp({"type":"object","properties":{"pid":{"type":"integer","description":"PID of process to attach"},"name":{"type":"string","description":"Optional descriptive name"}},"required":["pid"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_attach_process_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                if (req.pid <= 0) return R"({"status":"error","message":"Valid process PID is required"})";
                std::string run_id = rouen::hosts::process_host::instance().attach(req.pid, req.name);
                auto snap = rouen::hosts::process_host::instance().snapshot(run_id);
                if (!snap) return R"({"status":"error","message":"Failed to attach to process"})";
                glz::json_t resp;
                resp["status"] = "success";
                resp["run_id"] = snap->run_id;
                resp["definition_name"] = snap->definition_name;
                resp["state"] = (snap->state == rouen::hosts::process_run_state::running) ? "running" : "failed_to_start";
                resp["pid"] = snap->pid;
                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", attach_process_def);

    function_definition const kill_process_def(
        "kill_process",
        "Kill or terminate a running process by its run_id.",
        R"mcp({"type":"object","properties":{"run_id":{"type":"string","description":"Run ID of process to kill"}},"required":["run_id"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_kill_process_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                if (req.run_id.empty()) return R"({"status":"error","message":"run_id parameter is required"})";
                rouen::hosts::process_host::instance().kill(req.run_id);
                glz::json_t resp;
                resp["status"] = "success";
                resp["run_id"] = req.run_id;
                resp["message"] = std::format("Kill command sent for process run '{}'", req.run_id);
                std::string out;
                (void)glz::write_json(resp, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", kill_process_def);

    function_definition const get_process_ui_tree_def(
        "get_process_ui_tree",
        "Inspect accessibility UI element tree of a running process (by run_id, definition_id, or pid) up to max_depth, optionally scoped by window_title or hwnd.",
        R"mcp({"type":"object","properties":{"run_id":{"type":"string"},"definition_id":{"type":"integer"},"pid":{"type":"integer"},"hwnd":{"type":"integer","description":"Window HWND handle"},"window_title":{"type":"string","description":"Window title substring"},"window_class":{"type":"string","description":"Window class substring"},"max_depth":{"type":"integer"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_get_process_ui_tree_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                int64_t pid = mcp_resolve_process_pid(req.run_id, req.definition_id, req.pid, req.hwnd);
                if (pid <= 0 && req.hwnd == 0 && req.window_title.empty()) return R"({"status":"error","message":"Valid running process identifier (run_id, definition_id, or pid) is required"})";
                int max_depth = req.max_depth > 0 ? req.max_depth : 6;
                rouen::helpers::ui_window_scope scope{req.hwnd, req.window_title, req.window_class};
                auto res = rouen::helpers::ui_automation_explorer::inspect_process(pid, max_depth, 100, scope);
                glz::json_t root;
                root["status"] = res.success ? "success" : "error";
                root["pid"] = pid;
                root["total_node_count"] = res.total_node_count;
                if (res.permission_denied) root["permission_denied"] = true;
                if (!res.error_message.empty()) root["error_message"] = res.error_message;
                if (res.success) root["root"] = mcp_serialize_ui_node(res.root);
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", get_process_ui_tree_def);

    function_definition const get_process_ui_values_def(
        "get_process_ui_values",
        "Extract input and edit control values from the accessibility UI tree of a running process, optionally scoped by window_title or hwnd.",
        R"mcp({"type":"object","properties":{"run_id":{"type":"string"},"definition_id":{"type":"integer"},"pid":{"type":"integer"},"hwnd":{"type":"integer","description":"Window HWND handle"},"window_title":{"type":"string","description":"Window title substring"},"window_class":{"type":"string","description":"Window class substring"},"max_depth":{"type":"integer"},"edit_boxes_only":{"type":"boolean"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_get_process_ui_values_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                int64_t pid = mcp_resolve_process_pid(req.run_id, req.definition_id, req.pid, req.hwnd);
                if (pid <= 0 && req.hwnd == 0 && req.window_title.empty()) return R"({"status":"error","message":"Valid running process identifier required"})";
                rouen::helpers::ui_window_scope scope{req.hwnd, req.window_title, req.window_class};
                auto extracted = rouen::helpers::ui_automation_explorer::extract_process_values(pid, req.edit_boxes_only, req.max_depth > 0 ? req.max_depth : 8, scope);
                glz::json_t root;
                root["status"] = "success";
                root["pid"] = pid;
                std::vector<glz::json_t> vals_arr;
                for (const auto& item : extracted) {
                    glz::json_t val_obj;
                    val_obj["id"] = item.id;
                    val_obj["name"] = item.name;
                    val_obj["role"] = item.role;
                    val_obj["subrole"] = item.subrole;
                    val_obj["value"] = item.value;
                    val_obj["description"] = item.description;
                    val_obj["path"] = item.path;
                    vals_arr.push_back(std::move(val_obj));
                }
                root["values"] = std::move(vals_arr);
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", get_process_ui_values_def);

    function_definition const interact_process_ui_def(
        "interact_process_ui",
        "Perform UI interactions (click, set_value, focus, win32_click, win32_set_text, win32_command, or click at x/y coordinates) on accessibility controls of a running process, optionally scoped by window_title or hwnd.",
        R"mcp({"type":"object","properties":{"run_id":{"type":"string"},"definition_id":{"type":"integer"},"pid":{"type":"integer"},"hwnd":{"type":"integer","description":"Window HWND handle"},"window_title":{"type":"string","description":"Window title substring"},"window_class":{"type":"string","description":"Window class substring"},"target":{"type":"string","description":"Element ID or name target"},"action":{"type":"string","description":"Action verb: 'click', 'set_value', 'focus', 'win32_click', 'win32_set_text', 'win32_command'"},"value":{"type":"string","description":"Value to set for set_value / win32_set_text action"},"x":{"type":"number","description":"X coordinate for direct coordinate click"},"y":{"type":"number","description":"Y coordinate for direct coordinate click"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_interact_process_ui_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                int64_t pid = mcp_resolve_process_pid(req.run_id, req.definition_id, req.pid, req.hwnd);
                if (pid <= 0 && req.hwnd == 0 && req.window_title.empty()) return R"({"status":"error","message":"Valid running process identifier required"})";
                rouen::helpers::ui_window_scope scope{req.hwnd, req.window_title, req.window_class};
                rouen::helpers::ui_manipulation_result res;
                if (req.target.empty() && (req.x > 0.0f || req.y > 0.0f)) {
                    res = rouen::helpers::ui_automation_explorer::click_at_coordinates(pid, req.x, req.y);
                } else {
                    std::string action = req.action.empty() ? "click" : req.action;
                    res = rouen::helpers::ui_automation_explorer::perform_control_action(pid, req.target, action, req.value, scope);
                }
                glz::json_t root;
                root["status"] = res.success ? "success" : "error";
                root["pid"] = pid;
                if (res.permission_denied) root["permission_denied"] = true;
                if (!res.error_message.empty()) root["error_message"] = res.error_message;
                if (!res.matched_element_id.empty()) root["matched_element_id"] = res.matched_element_id;
                if (!res.matched_element_name.empty()) root["matched_element_name"] = res.matched_element_name;
                if (!res.matched_element_role.empty()) root["matched_element_role"] = res.matched_element_role;
                if (!res.action_performed.empty()) root["action_performed"] = res.action_performed;
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", interact_process_ui_def);

    function_definition const capture_process_ui_screenshot_def(
        "capture_process_ui_screenshot",
        "Capture a screenshot of an external orchestrated process window or specific window/HWND.",
        R"mcp({"type":"object","properties":{"run_id":{"type":"string"},"definition_id":{"type":"integer"},"pid":{"type":"integer"},"hwnd":{"type":"integer","description":"Window HWND handle"},"title_pattern":{"type":"string","description":"Window title substring to match"},"filename":{"type":"string","description":"Output image file path (.png or .bmp)"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_capture_process_ui_screenshot_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                int64_t pid = 0;
                if (req.pid > 0 || !req.run_id.empty() || req.definition_id > 0) {
                    pid = mcp_resolve_process_pid(req.run_id, req.definition_id, req.pid);
                    if (pid <= 0 && req.hwnd == 0 && req.title_pattern.empty()) {
                        return R"({"status":"error","message":"Valid running process identifier required"})";
                    }
                } else if (req.hwnd == 0 && req.title_pattern.empty()) {
                    return R"({"status":"error","message":"Valid process identifier or window identifier required"})";
                }
                if (req.filename.empty()) {
                    std::error_code ec;
                    auto temp_dir = std::filesystem::temp_directory_path(ec);
                    req.filename = (ec ? std::filesystem::path("process_window.png") : (temp_dir / "process_window.png")).string();
                }
                auto res = rouen::helpers::ui_automation_explorer::capture_window_screenshot(
                    pid, req.hwnd, req.title_pattern, req.filename
                );
                glz::json_t root;
                root["status"] = res.success ? "success" : "error";
                if (res.success) {
                    root["file"] = res.file;
                    root["hwnd"] = res.hwnd;
                    root["title"] = res.title;
                    root["width"] = res.width;
                    root["height"] = res.height;
                    root["message"] = res.message;
                } else {
                    root["message"] = res.error;
                }
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "process"
    );
    register_function("process", capture_process_ui_screenshot_def);

    // 11. AdLib & Cast Controls
    function_definition const get_adlib_status_def(
        "get_adlib_status",
        "Get status of the AdLib audio/video recording and presentation engine.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& engine = rouen::helpers::AdLibEngine::instance();
                auto stage = engine.get_stage();
                const char* stage_str = "Idle";
                if (stage == rouen::helpers::AdLibStage::Prepared) stage_str = "Prepared";
                else if (stage == rouen::helpers::AdLibStage::Intro) stage_str = "Intro";
                else if (stage == rouen::helpers::AdLibStage::Middle) stage_str = "Middle";
                else if (stage == rouen::helpers::AdLibStage::Outro) stage_str = "Outro";
                else if (stage == rouen::helpers::AdLibStage::Finished) stage_str = "Finished";
                return std::format(R"({{"status":"success","stage":"{}","is_active":{},"is_paused":{},"is_recording":{},"elapsed_seconds":{:.2f}}})",
                    stage_str, engine.is_active() ? "true" : "false", engine.is_paused() ? "true" : "false",
                    engine.is_recording() ? "true" : "false", engine.get_elapsed_seconds());
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "adlib"
    );
    register_function("adlib", get_adlib_status_def);

    function_definition const control_adlib_engine_def(
        "control_adlib_engine",
        "Control the AdLib video production engine (commands: 'prepare', 'start', 'next_stage', 'stop', 'run').",
        R"mcp({"type":"object","properties":{"command":{"type":"string","description":"Command verb: 'prepare', 'start', 'next_stage', 'stop', 'run'"},"intro_video_path":{"type":"string"},"background_path":{"type":"string"},"outro_video_path":{"type":"string"},"output_mp4_path":{"type":"string"},"mode":{"type":"string"},"mic_device_name":{"type":"string"},"duration_seconds":{"type":"integer"}},"required":["command"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_control_adlib_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                auto& engine = rouen::helpers::AdLibEngine::instance();
                if (req.command == "status") {
                    return std::format(R"({{"status":"success","stage":{}}})", static_cast<int>(engine.get_stage()));
                } else if (req.command == "prepare") {
                    rouen::helpers::AdLibConfig cfg;
                    cfg.intro_video_path = req.intro_video_path;
                    cfg.background_path = req.background_path;
                    cfg.outro_video_path = req.outro_video_path;
                    cfg.output_mp4_path = req.output_mp4_path;
                    cfg.mode = (req.mode == "live") ? rouen::helpers::AdLibMode::Live : rouen::helpers::AdLibMode::Recorded;
                    bool prepared = engine.prepare(cfg);
                    return std::format(R"({{"status":"success","prepared":{}}})", prepared ? "true" : "false");
                } else if (req.command == "start") {
                    bool started = engine.start();
                    return std::format(R"({{"status":"success","started":{}}})", started ? "true" : "false");
                } else if (req.command == "next_stage") {
                    engine.next_stage();
                    return R"({"status":"success","message":"Advanced to next stage"})";
                } else if (req.command == "stop") {
                    engine.stop();
                    return R"({"status":"success","message":"Stopped AdLib recording"})";
                } else if (req.command == "run") {
                    rouen::helpers::AdLibConfig cfg;
                    cfg.intro_video_path = req.intro_video_path;
                    cfg.background_path = req.background_path;
                    cfg.outro_video_path = req.outro_video_path;
                    cfg.output_mp4_path = req.output_mp4_path;
                    cfg.mode = (req.mode == "live") ? rouen::helpers::AdLibMode::Live : rouen::helpers::AdLibMode::Recorded;
                    if (!req.mic_device_name.empty()) {
                        cfg.mic_device_id = rouen::helpers::AudioCapture::find_device_id_by_name(req.mic_device_name);
                    } else if (req.mic_device_id > 0) {
                        cfg.mic_device_id = req.mic_device_id;
                    }
                    engine.prepare(cfg);
                    engine.set_auto_stop_seconds(req.duration_seconds > 0 ? static_cast<double>(req.duration_seconds) : 3.0);
                    engine.start();
                    return std::format(R"({{"status":"success","output_mp4_path":"{}","recording_started":true}})", req.output_mp4_path);
                }
                return R"({"status":"error","message":"Unknown command. Supported: prepare, start, next_stage, stop, run"})";
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "adlib"
    );
    register_function("adlib", control_adlib_engine_def);

    function_definition const get_cast_status_def(
        "get_cast_status",
        "Get media player & casting service status, playback position, duration, VU levels, and video stats.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto host = rouen::hosts::VideoFeedHost::get_host();
                bool is_casting = host ? host->is_running() : false;
                size_t audio_queued = host ? host->get_cast_queued_bytes() : 0;
                bool is_playing = false;
                double pos = 0.0, dur = 0.0;
                std::string media_url;
                bool has_video = false, texture_ready = false;
                float luminance = 0.0f, vu_l = 0.0f, vu_r = 0.0f;
                size_t video_q_size = 0;
                {
                    std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                    for (auto& [id, item_ptr] : media_player::items()) {
                        if (item_ptr && item_ptr->is_playing) {
                            is_playing = true;
                            pos = item_ptr->get_current_position();
                            dur = item_ptr->duration.load();
                            media_url = item_ptr->url;
                            has_video = item_ptr->has_video.load();
                            texture_ready = (item_ptr->video_texture != nullptr);
                            luminance = item_ptr->current_luminance.load();
                            vu_l = item_ptr->get_vu_level_l();
                            vu_r = item_ptr->get_vu_level_r();
                            {
                                std::lock_guard<std::mutex> q_lock(item_ptr->video_queue_mutex);
                                video_q_size = item_ptr->decoded_video_queue.size();
                            }
                            break;
                        }
                    }
                }
                return std::format(R"({{"status":"success","is_casting":{},"is_media_playing":{},"media_url":"{}","position":{:.3f},"duration":{:.3f},"audio_queued_bytes":{},"has_video":{},"texture_ready":{},"luminance":{:.4f},"vu_level_l":{:.4f},"vu_level_r":{:.4f},"video_queue_size":{}}})",
                    is_casting ? "true" : "false", is_playing ? "true" : "false", media_url, pos, dur, audio_queued,
                    has_video ? "true" : "false", texture_ready ? "true" : "false", luminance, vu_l, vu_r, video_q_size);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "cast"
    );
    register_function("cast", get_cast_status_def);

    function_definition const control_cast_playback_def(
        "control_cast_playback",
        "Start casting service or trigger media playback for a given URL/URI.",
        R"mcp({"type":"object","properties":{"command":{"type":"string","description":"'start_service' or 'play'"},"url":{"type":"string"},"uri":{"type":"string"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_control_cast_playback_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                auto host = rouen::hosts::VideoFeedHost::get_host();
                if (req.command == "start_service") {
                    if (host) { host->start(); return R"({"status":"success","message":"Video feed service started"})"; }
                    return R"({"status":"error","message":"VideoFeedHost unavailable"})";
                }
                if (host) host->start();
                std::string target_url = !req.url.empty() ? req.url : req.uri;
                if (target_url.empty()) return R"({"status":"error","message":"No media URL/URI provided"})";
                auto& item = media_player::get_item(target_url);
                item.url = target_url;
                item.playMedia();
                return std::format(R"({{"status":"success","message":"Media playback started","url":"{}"}})", target_url);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "cast"
    );
    register_function("cast", control_cast_playback_def);

    // 12. Metrics, Diagnostics, and Card Schemas
    function_definition const get_card_metrics_def(
        "get_card_metrics",
        "Get card rendering performance metrics and FPS data, or reset metrics.",
        R"mcp({"type":"object","properties":{"reset":{"type":"boolean","description":"Reset card render metrics if true"},"include_all":{"type":"boolean","description":"Include inactive cards if true"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_get_card_metrics_params req{};
                if (!params.empty()) (void)glz::read_json(req, params);
                if (req.reset) {
                    rouen::helpers::CardRenderMetrics::instance().reset();
                    return R"({"status":"success","message":"Card metrics reset"})";
                }
                auto metrics = rouen::helpers::CardRenderMetrics::instance().get_all_metrics(req.include_all);
                std::vector<glz::json_t> arr;
                for (const auto& m : metrics) {
                    glz::json_t item;
                    item["title"] = m.title;
                    item["uri"] = m.uri;
                    item["last_render_ms"] = m.last_render_ms;
                    item["avg_render_ms"] = m.avg_render_ms;
                    item["max_render_ms"] = m.max_render_ms;
                    item["min_render_ms"] = m.min_render_ms;
                    item["render_count"] = m.render_count;
                    item["slow_render_count"] = m.slow_render_count;
                    item["very_slow_render_count"] = m.very_slow_render_count;
                    item["requested_fps"] = m.requested_fps;
                    arr.push_back(item);
                }
                std::string out;
                (void)glz::write_json(arr, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "metrics"
    );
    register_function("metrics", get_card_metrics_def);

    function_definition const get_rss_diagnostics_def(
        "get_rss_diagnostics",
        "Get RSS feed diagnostics, feed item counts, and rendering performance metrics.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto rss_host = rouen::cards::rss::getHost();
                if (!rss_host) return R"({"status":"error","message":"RSS Host not available"})";
                auto diag = rss_host->get_rss_diagnostics();
                glz::json_t root;
                root["status"] = "success";
                glz::json_t data;
                data["total_feeds"] = diag.total_feeds;
                data["total_items"] = diag.total_items;
                data["slowest_feed_title"] = diag.slowest_feed_title;
                data["slowest_feed_uri"] = diag.slowest_feed_uri;
                data["slowest_feed_render_ms"] = diag.slowest_feed_render_ms;
                std::vector<glz::json_t> feeds_arr;
                for (const auto& f : diag.feeds) {
                    glz::json_t item;
                    item["feed_id"] = f.id;
                    item["title"] = f.title;
                    item["url"] = f.url;
                    item["language"] = f.language;
                    item["item_count"] = f.item_count;
                    item["tag_count"] = f.tag_count;
                    item["last_render_ms"] = f.last_render_ms;
                    item["avg_render_ms"] = f.avg_render_ms;
                    item["max_render_ms"] = f.max_render_ms;
                    item["min_render_ms"] = f.min_render_ms;
                    item["render_count"] = f.render_count;
                    item["slow_render_count"] = f.slow_render_count;
                    item["is_slow"] = f.is_slow;
                    feeds_arr.push_back(item);
                }
                data["feeds"] = feeds_arr;
                root["diagnostics"] = data;
                std::string out;
                (void)glz::write_json(root, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "rss"
    );
    register_function("rss", get_rss_diagnostics_def);

    function_definition const list_card_schemas_def(
        "list_card_schemas",
        "List all available card schemas and registered card URIs.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                std::vector<std::string> schemas;
                const auto& dict = rouen::cards::factory::dictionary();
                schemas.reserve(dict.size());
                for (const auto& pair : dict) {
                    schemas.push_back(pair.first);
                }
                std::sort(schemas.begin(), schemas.end());
                return glz::write_json(schemas).value_or("[]");
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "deck"
    );
    function_definition const notify_operator_telegram_def(
        "notify_operator_telegram",
        "Sends an urgent push notification directly to the operator's phone via the Mesh Telegram gateway.",
        R"mcp({"type":"object","properties":{"message":{"type":"string","description":"Markdown formatted message"},"urgent":{"type":"boolean","description":"Whether notification is urgent","default":true}},"required":["message"]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_notify_operator_telegram_params p{};
                if (glz::read_json(p, params) != glz::error_code::none || p.message.empty()) {
                    return R"({"status":"error","message":"Invalid parameters. 'message' string is required."})";
                }
                rouen::services::notification_options opts{
                    .channel = "telegram",
                    .urgent = p.urgent,
                    .spoken = true
                };
                auto [success, target] = rouen::services::presence_service::instance().route_notification(p.message, "", opts);
                if (success) {
                    return std::format(R"({{"status":"success","routed_target":"{}","message":"Notification dispatched to operator via Telegram"}})", target);
                } else {
                    return std::format(R"({{"status":"error","routed_target":"{}","message":"Failed to dispatch notification via Telegram gateway"}})", target);
                }
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "system"
    );
    register_function("system", notify_operator_telegram_def);
    register_function("telegram", notify_operator_telegram_def);

    function_definition mesh_list_nodes_def(
        "mesh_list_nodes",
        "Lists computers, nodes, and devices currently connected or known on the Rouen mesh network, including client IDs, hostnames, and live connection status. Note: If the user requires specific system or platform information for a remote node, query the target node's Rouen REST API via an open mesh virtual route.",
        R"({"type":"object","properties":{}})",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& mesh = hosts::rouen_mesh_host::instance();
                auto cfg = mesh.get_config();
                std::string local_cid = cfg.client_id.empty() ? hosts::rouen_mesh_host::generate_default_client_id() : cfg.client_id;

                auto connected = mesh.get_connected_clients();
                auto presence_entries = mesh.get_registry_entries("presence/");

                struct mesh_node_view {
                    std::string client_id;
                    std::string hostname;
                    std::string platform;
                    std::string user;
                    std::string status;
                    std::string last_active;
                    std::string ip_address;
                    bool is_self{false};
                };

                std::map<std::string, mesh_node_view> nodes;

                // 1. Add local node
                auto local_pres = rouen::services::presence_service::instance().get_local_presence();
                nodes[local_cid] = mesh_node_view{
                    .client_id = local_cid,
                    .hostname = local_pres.hostname,
                    .platform = local_pres.platform,
                    .user = local_pres.user,
                    .status = mesh.is_connected() ? local_pres.status : "offline",
                    .last_active = local_pres.last_active_iso,
                    .ip_address = "127.0.0.1",
                    .is_self = true
                };

                // 2. Add presence records from registry
                for (const auto& [k, entry] : presence_entries) {
                    if (k == "presence/last_active") continue;
                    if (entry.value.empty()) continue;
                    rouen::services::presence_record rec{};
                    if (glz::read_json(rec, entry.value) == glz::error_code::none && !rec.client_id.empty()) {
                        auto& node = nodes[rec.client_id];
                        node.client_id = rec.client_id;
                        // Avoid inheriting false "mac" platforms and hostnames from legacy presence bugs on remote nodes
                        if (rec.client_id != local_cid && rec.platform == "mac" && rec.hostname == "mac") {
                            node.platform = "unknown";
                            node.hostname = rec.client_id;
                        } else {
                            node.hostname = rec.hostname;
                            node.platform = rec.platform;
                        }
                        node.user = rec.user;
                        node.status = rec.status;
                        node.last_active = rec.last_active_iso;
                        node.is_self = (rec.client_id == local_cid);
                    }
                }

                // 3. Update with active connected client metadata
                for (const auto& c : connected) {
                    if (c.client_id.empty()) continue;
                    auto& node = nodes[c.client_id];
                    node.client_id = c.client_id;
                    if (!c.ip_address.empty()) node.ip_address = c.ip_address;
                    node.status = "connected";
                    node.is_self = (c.client_id == local_cid);
                }

                std::vector<glz::json_t> nodes_json;
                for (const auto& [cid, n] : nodes) {
                    glz::json_t item;
                    item["client_id"] = n.client_id;
                    item["hostname"] = n.hostname.empty() ? cid : n.hostname;
                    if (n.is_self) {
                        item["platform"] = n.platform;
                    } else if (!n.platform.empty() && n.platform != "unknown" && n.platform != "mac") {
                        item["platform"] = n.platform;
                    }
                    item["user"] = n.user;
                    item["status"] = n.status.empty() ? "connected" : n.status;
                    item["last_active"] = n.last_active;
                    if (!n.ip_address.empty()) item["ip_address"] = n.ip_address;
                    item["is_self"] = n.is_self;
                    nodes_json.push_back(std::move(item));
                }

                glz::json_t result;
                result["mesh_connected"] = mesh.is_connected();
                result["total_nodes"] = static_cast<double>(nodes_json.size());
                result["nodes"] = nodes_json;

                std::string out;
                (void)glz::write_json(result, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"error":"{}"}})", e.what());
            }
        },
        "mesh"
    );
    register_function("mesh", mesh_list_nodes_def);
    register_function("system", mesh_list_nodes_def);
    register_function("deck", mesh_list_nodes_def);

    function_definition mesh_get_status_def(
        "mesh_get_status",
        "Returns the current connection status of this computer to the Rouen cloud relay and mesh network, including pairing state, client ID, ping latency, and active virtual routes.",
        R"({"type":"object","properties":{}})",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& mesh = hosts::rouen_mesh_host::instance();
                auto cfg = mesh.get_config();
                auto routes = mesh.get_active_routes();

                glz::json_t result;
                result["connected"] = mesh.is_connected();
                result["paired"] = mesh.is_paired();
                result["client_id"] = cfg.client_id;
                result["server_url"] = cfg.server_url;
                result["auth_mode"] = hosts::rouen_mesh_host::auth_mode_to_string(cfg.auth_mode);
                result["ping_ms"] = static_cast<double>(mesh.get_ping_ms());
                result["active_routes_count"] = static_cast<double>(routes.size());
                result["status_message"] = mesh.get_status_message();

                std::string out;
                (void)glz::write_json(result, out);
                return out;
            } catch (const std::exception& e) {
                return std::format(R"({{"error":"{}"}})", e.what());
            }
        },
        "mesh"
    );
    register_function("mesh", mesh_get_status_def);
    register_function("system", mesh_get_status_def);
    register_function("deck", mesh_get_status_def);

    function_definition mesh_open_route_def(
        "mesh_open_route",
        "Opens a virtual route (transparent TCP tunnel) to a target Rouen mesh client ID and port (e.g. target_port 8081 for Rouen REST API), allowing local requests to reach the target machine.",
        R"json({"type":"object","properties":{"target_client_id":{"type":"string","description":"Target mesh client ID (e.g. 'rouen-md-d-ws-ir-01')"},"target_port":{"type":"integer","description":"Target port on remote host (e.g. 8081 for Rouen API)"},"local_port":{"type":"integer","description":"Optional local listening port (0 for automatic allocation)"}},"required":["target_client_id","target_port"]})json",
        [](const std::string& params) -> std::string {
            try {
                glz::json_t p;
                auto err = glz::read_json(p, params);
                if (err || !p.is_object()) return R"({"error":"Invalid JSON parameters"})";
                std::string target_client_id = p.contains("target_client_id") ? p["target_client_id"].get<std::string>() : "";
                int target_port = p.contains("target_port") ? static_cast<int>(p["target_port"].get<double>()) : 8081;
                int local_port = p.contains("local_port") ? static_cast<int>(p["local_port"].get<double>()) : 0;
                if (target_client_id.empty() || target_port <= 0) return R"({"error":"target_client_id and target_port are required"})";

                auto& mesh = hosts::rouen_mesh_host::instance();
                std::string out_err;
                bool ok = mesh.open_virtual_route(target_client_id, static_cast<uint16_t>(target_port), out_err, static_cast<uint16_t>(local_port));
                if (!ok) {
                    return std::format(R"({{"success":false,"error":"{}"}})", out_err);
                }
                uint16_t assigned_port = static_cast<uint16_t>(local_port);
                for (const auto& r : mesh.get_active_routes()) {
                    if (r.target_client_id == target_client_id && r.target_port == target_port) {
                        assigned_port = r.local_port;
                        break;
                    }
                }
                return std::format(R"({{"success":true,"target_client_id":"{}","target_port":{},"local_port":{},"local_url":"http://127.0.0.1:{}"}})",
                    target_client_id, target_port, assigned_port, assigned_port);
            } catch (const std::exception& e) {
                return std::format(R"({{"error":"{}"}})", e.what());
            }
        },
        "mesh"
    );
    register_function("mesh", mesh_open_route_def);
    register_function("system", mesh_open_route_def);
    register_function("deck", mesh_open_route_def);

    function_definition mesh_query_remote_api_def(
        "mesh_query_remote_api",
        "Queries the Rouen REST API on a remote target mesh client via a virtual route tunnel. Automatically ensures a tunnel to port 8081 is open and executes the HTTP request, returning the remote node's verified live system data.",
        R"json({"type":"object","properties":{"target_client_id":{"type":"string","description":"Target client ID (e.g. 'rouen-md-d-ws-ir-01')"},"endpoint":{"type":"string","description":"API endpoint to query (e.g. '/api/presence', '/api/window', '/api/openapi.json', '/api/health')"}},"required":["target_client_id","endpoint"]})json",
        [](const std::string& params) -> std::string {
            try {
                glz::json_t p;
                auto err = glz::read_json(p, params);
                if (err || !p.is_object()) return R"({"error":"Invalid JSON parameters"})";
                std::string target_client_id = p.contains("target_client_id") ? p["target_client_id"].get<std::string>() : "";
                std::string endpoint = p.contains("endpoint") ? p["endpoint"].get<std::string>() : "/api/health";
                if (target_client_id.empty()) return R"({"error":"target_client_id is required"})";

                auto& mesh = hosts::rouen_mesh_host::instance();
                uint16_t local_port = 0;
                for (const auto& r : mesh.get_active_routes()) {
                    if (r.target_client_id == target_client_id && r.target_port == 8081) {
                        local_port = r.local_port;
                        break;
                    }
                }
                if (local_port == 0) {
                    std::string out_err;
                    if (!mesh.open_virtual_route(target_client_id, 8081, out_err, 0)) {
                        return std::format(R"({{"error":"Failed to open virtual route to {}:8081: {}"}})", target_client_id, out_err);
                    }
                    for (const auto& r : mesh.get_active_routes()) {
                        if (r.target_client_id == target_client_id && r.target_port == 8081) {
                            local_port = r.local_port;
                            break;
                        }
                    }
                }
                if (local_port == 0) {
                    return R"({"error":"Tunnel opened but local listening port was not found"})";
                }

                std::string norm_endpoint = endpoint.starts_with('/') ? endpoint : ("/" + endpoint);
                std::string url = std::format("http://127.0.0.1:{}{}", local_port, norm_endpoint);
                http::fetch fetcher(5);
                std::string response_body = fetcher(url);
                return response_body.empty() ? R"({"status":"ok","empty_response":true})" : response_body;
            } catch (const std::exception& e) {
                return std::format(R"({{"error":"{}"}})", e.what());
            }
        },
        "mesh"
    );
    register_function("mesh", mesh_query_remote_api_def);
    register_function("system", mesh_query_remote_api_def);
    register_function("deck", mesh_query_remote_api_def);

    register_function("deck", list_card_schemas_def);
}


void mcp_host::register_function(const std::string& card_type, const function_definition& func) {
    std::lock_guard<std::mutex> const lock(mutex_);
    
    // Create a copy of the function with the card_type set
    function_definition func_copy(func.name, func.description, func.schema, func.handler, card_type);
    
    // Register in main function map
    functions_.emplace(func.name, std::move(func_copy));
    
    // Add to card function registry
    card_functions_[card_type].push_back(func.name);
    
    DEBUG_TRACE("MCP: Registered function '" + func.name + "' from card '" + card_type + "'");
}

void mcp_host::unregister_card_functions(const std::string& card_type) {
    std::lock_guard<std::mutex> const lock(mutex_);
    
    auto card_it = card_functions_.find(card_type);
    if (card_it == card_functions_.end()) {
        return;
    }
    
    // Remove all functions for this card type
    for (const auto& func_name : card_it->second) {
        functions_.erase(func_name);
        DEBUG_TRACE(std::format("MCP: Unregistered function '{}' from card '{}'", func_name, card_type));
    }
    
    // Remove card from registry
    card_functions_.erase(card_it);
}

std::vector<mcp_host::function_definition> mcp_host::get_available_functions() const {
    std::lock_guard<std::mutex> const lock(mutex_);
    
    std::vector<function_definition> result;
    result.reserve(functions_.size());
    
    for (const auto& [name, func] : functions_) {
        result.push_back(func);
    }
    
    return result;
}

std::vector<mcp_host::function_definition> mcp_host::get_functions_for_card(const std::string& card_type) const {
    std::lock_guard<std::mutex> const lock(mutex_);
    
    std::vector<function_definition> result;
    
    auto card_it = card_functions_.find(card_type);
    if (card_it == card_functions_.end()) {
        return result;
    }
    
    result.reserve(card_it->second.size());
    for (const auto& func_name : card_it->second) {
        auto func_it = functions_.find(func_name);
        if (func_it != functions_.end()) {
            result.push_back(func_it->second);
        }
    }
    
    return result;
}

std::vector<std::string> mcp_host::get_registered_categories() const {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::vector<std::string> categories;
    categories.reserve(card_functions_.size());
    for (const auto& [card_type, funcs] : card_functions_) {
        if (!card_type.empty() && std::find(categories.begin(), categories.end(), card_type) == categories.end()) {
            categories.push_back(card_type);
        }
    }
    return categories;
}

mcp_host::execution_result mcp_host::execute_function(const std::string& name, const std::string& params) {
    function_definition func;
    {
        std::lock_guard<std::mutex> const lock(mutex_);
        auto func_it = functions_.find(name);
        if (func_it == functions_.end()) {
            return {false, "", "Function '" + name + "' not found"};
        }
        func = func_it->second; // Safe copy under lock
    }
    
    // Validate parameters if schema is provided
    if (!func.schema.empty() && !validate_parameters(params, func.schema)) {
        return {false, "", "Invalid parameters for function '" + name + "'"};
    }
    
    try {
        DEBUG_TRACE("MCP: Executing function '" + name + "' with params: " + params);
        std::string result = func.handler(params);
        DEBUG_TRACE("MCP: Function '" + name + "' completed successfully");
        return {true, std::move(result)};
    } catch (const std::exception& e) {
        std::string error = "Error executing function '" + name + "': " + e.what();
        DEBUG_ERROR(error);
        return {false, "", std::move(error)};
    } catch (...) {
        std::string error = "Unknown error executing function '" + name + "'";
        DEBUG_ERROR(error);
        return {false, "", std::move(error)};
    }
}

bool mcp_host::has_function(const std::string& name) const {
    std::lock_guard<std::mutex> const lock(mutex_);
    return functions_.find(name) != functions_.end();
}

std::string mcp_host::get_function_schema(const std::string& name) const {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto func_it = functions_.find(name);
    if (func_it != functions_.end()) {
        return func_it->second.schema;
    }
    return "";
}

std::string mcp_host::get_functions_description() const {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (functions_.empty()) {
        return "No functions available.";
    }
    
    std::ostringstream ss;
    ss << "Available functions:\n";
    
    for (const auto& [name, func] : functions_) {
        ss << "- " << func.name << " (" << func.card_type << "): " << func.description << "\n";
    }
    
    return ss.str();
}

bool mcp_host::validate_parameters(const std::string& params, const std::string& schema) {
    // Basic validation - just check if params is valid JSON
    // In a full implementation, we'd validate against the JSON schema
    (void)schema; // Suppress unused parameter warning - schema validation not yet implemented
    
    try {
        if (params.empty()) {
            return true; // Empty params are valid for functions that don't require them
        }
        
        // Try to parse as JSON
        auto json_obj = glz::read_json<glz::json_t>(params);
        if (!json_obj) {
            DEBUG_WARN("MCP: Failed to parse parameters as JSON: " + params);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        DEBUG_WARN("MCP: Parameter validation error: " + std::string(e.what()));
        return false;
    }
}

void mcp_host::detect_system_info() {
    std::string os_type;
    std::string os_version;
    
#if defined(__APPLE__)
    os_type = "macOS";
    std::string sw_vers_out = ProcessHelper::executeCommand("sw_vers -productVersion");
    if (!sw_vers_out.empty()) {
        sw_vers_out.erase(sw_vers_out.find_last_not_of(" \t\r\n") + 1);
        os_version = sw_vers_out;
    } else {
        os_version = "unknown";
    }
#elif defined(__linux__)
    os_type = "Linux";
    std::ifstream release_file("/etc/os-release");
    if (release_file.is_open()) {
        std::string line;
        while (std::getline(release_file, line)) {
            if (line.starts_with("PRETTY_NAME=")) {
                std::string pretty = line.substr(12);
                if (pretty.size() >= 2 && pretty.front() == '"' && pretty.back() == '"') {
                    pretty = pretty.substr(1, pretty.size() - 2);
                }
                os_version = pretty;
                break;
            }
        }
    }
    if (os_version.empty()) {
        std::string uname_out = ProcessHelper::executeCommand("uname -r");
        if (!uname_out.empty()) {
            uname_out.erase(uname_out.find_last_not_of(" \t\r\n") + 1);
            os_version = uname_out;
        } else {
            os_version = "unknown";
        }
    }
#elif defined(_WIN32)
    os_type = "Windows";
    std::string ver_out = ProcessHelper::executeCommand("ver");
    if (!ver_out.empty()) {
        ver_out.erase(0, ver_out.find_first_not_of(" \t\r\n"));
        ver_out.erase(ver_out.find_last_not_of(" \t\r\n") + 1);
        os_version = ver_out;
    } else {
        os_version = "unknown";
    }
#else
    os_type = "Unknown OS";
    os_version = "unknown";
#endif

    std::vector<std::string> found;
#if defined(__APPLE__) || defined(__linux__)
    std::string const cmd = "for cmd in brew nix apt dnf pacman yum zypper apk port; do command -v $cmd >/dev/null 2>&1 && echo $cmd; done";
    std::string const output = ProcessHelper::executeCommand(cmd);
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            found.push_back(line);
        }
    }
#elif defined(_WIN32)
    std::string cmd = "for %i in (winget choco scoop) do @where %i >nul 2>&1 && echo %i";
    std::string output = ProcessHelper::executeCommand(cmd);
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            found.push_back(line);
        }
    }
#endif

    std::string pkgs;
    if (found.empty()) {
        pkgs = "none detected";
    } else {
        for (size_t i = 0; i < found.size(); ++i) {
            if (i > 0) pkgs += ", ";
            pkgs += found[i];
        }
    }

    cached_system_info_ = "OS: " + os_type + " (" + os_version + "), Installed Package Managers: " + pkgs;
    DEBUG_TRACE("MCP: Cached system info: " + cached_system_info_);
}

} // namespace rouen::helpers
