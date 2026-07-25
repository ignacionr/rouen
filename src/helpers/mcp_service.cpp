// 1. Standard includes in alphabetic order
#include <algorithm>
#include <fstream>
#include <sstream>

// 2. Libraries used in the project, in alphabetic order
#include "glaze_include.hpp"

// 3. All other includes
#include "debug.hpp"
#include "mcp_service.hpp"
#include "platform_utils.hpp"
#include "media_player.hpp"
#include "process_helper.hpp"
#include "string_helper.hpp"
#include "fetch.hpp"
#include "../registrar.hpp"
#include "../models/notes/notes_repository.hpp"
#include "../models/series/series_repository.hpp"
#include "../models/adaptive_cards/adaptive_cards_repository.hpp"
#include "../models/contacts/contacts_repository.hpp"
#include "universal_sync_service.hpp"
#include "persona_manager.hpp"

namespace rouen::helpers {

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

mcp_service::mcp_service() {
    detect_system_info();

    // Register default run_local_command function associated with terminal
    function_definition run_cmd_def(
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

            const std::string command_with_stderr = request.command + " 2>&1";
            std::string output;
            if (!request.working_directory.empty()) {
                output = ProcessHelper::executeCommandInDirectory(request.working_directory, command_with_stderr);
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
    function_definition create_card_def(
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
    function_definition create_time_series_def(
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
                std::string card_uri = std::format("number-series:{}", series_rec.name);
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
    function_definition create_adaptive_card_def(
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
    function_definition list_adaptive_cards_def(
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
    function_definition get_adaptive_card_def(
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
    function_definition create_alarm_def(
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
                std::string card_uri = "alarm:" + request.datetime;
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
    function_definition edit_file_def(
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

    // Register YouTube search videos function
    function_definition youtube_search_def(
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
            
            std::string escaped_query;
            for (char c : request.query) {
                if (c == '"' || c == '\\' || c == '$' || c == '`') {
                    escaped_query += '\\';
                }
                escaped_query += c;
            }
            
#ifdef __APPLE__
            std::string cmd = std::format("export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH && yt-dlp --flat-playlist --dump-json \"ytsearch10:{}\"", escaped_query);
#else
            std::string cmd = std::format("yt-dlp --flat-playlist --dump-json \"ytsearch10:{}\"", escaped_query);
#endif
            std::string output = ProcessHelper::executeCommand(cmd);
            
            std::stringstream ss(output);
            std::string line;
            std::vector<mcp_youtube_video> results;
            
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                try {
                    glz::json_t resp;
                    auto ec = glz::read_json(resp, line);
                    if (!ec) {
                        mcp_youtube_video video;
                        if (resp.contains("id") && resp["id"].is_string()) {
                            video.id = resp["id"].get<std::string>();
                        }
                        if (resp.contains("title") && resp["title"].is_string()) {
                            video.title = resp["title"].get<std::string>();
                        }
                        if (resp.contains("url") && resp["url"].is_string()) {
                            video.url = resp["url"].get<std::string>();
                        } else if (!video.id.empty()) {
                            video.url = "https://www.youtube.com/watch?v=" + video.id;
                        }
                        if (resp.contains("duration_string") && resp["duration_string"].is_string()) {
                            video.duration = resp["duration_string"].get<std::string>();
                        }
                        if (resp.contains("channel") && resp["channel"].is_string()) {
                            video.channel = resp["channel"].get<std::string>();
                        } else if (resp.contains("uploader") && resp["uploader"].is_string()) {
                            video.channel = resp["uploader"].get<std::string>();
                        }
                        results.push_back(std::move(video));
                    }
                } catch (...) {
                    (void)0;
                }
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
    function_definition youtube_play_def(
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
                    std::string card_uri = "youtube:play:" + ::helpers::StringHelper::url_encode(request.url) + "|" + ::helpers::StringHelper::url_encode(request.title);
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
    function_definition youtube_create_card_def(
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
                    std::string card_uri = "youtube:" + ::helpers::StringHelper::url_encode(request.query);
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
    function_definition wikipedia_search_def(
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
                std::string encoded_query = ::helpers::StringHelper::url_encode(request.query);
                std::string url = "https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=" + encoded_query + "&format=json&utf8=";
                
                std::vector<std::string> headers = {
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
    function_definition wikipedia_get_article_def(
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
                std::string encoded_title = ::helpers::StringHelper::url_encode(request.title);
                std::string url = "https://en.wikipedia.org/w/api.php?action=query&prop=extracts&explaintext=1&titles=" + encoded_title + "&format=json";
                
                std::vector<std::string> headers = {
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
    function_definition wikipedia_create_card_def(
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
    function_definition notes_list_def(
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
    function_definition notes_get_def(
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
    function_definition notes_save_def(
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
                int note_id = repo.save_note(request.title, request.content, request.tags);
                
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
    function_definition notes_append_def(
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

                int note_id = repo.save_note(note->title, new_content, note->tags);
                
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
    function_definition notes_delete_def(
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

                bool deleted = repo.delete_note(note->id);
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
    function_definition contacts_list_def(
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

    function_definition contacts_get_def(
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

    function_definition contacts_save_def(
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
                UniversalSyncService::instance().sync_out("MCP saved contact: " + c.get_full_name());

                return std::format(R"({{"status":"success","message":"Contact saved","id":{}}})", saved_id);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_save_def);
    register_function("contacts", contacts_save_def);

    function_definition contacts_delete_def(
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
                UniversalSyncService::instance().sync_out("MCP deleted contact ID: " + std::to_string(req.id));

                return std::format(R"({{"status":"success","message":"Contact deleted","id":{}}})", req.id);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_delete_def);
    register_function("contacts", contacts_delete_def);

    function_definition contacts_import_macos_def(
        "contacts_import_macos",
        "Import contacts from local macOS Contacts app into the directory.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                models::contacts::contacts_repository repo;
                int count = repo.import_macos_contacts();
                UniversalSyncService::instance().sync_out("Imported macOS contacts via MCP");
                return std::format(R"({{"status":"success","imported_count":{}}})", count);
            } catch (const std::exception& e) {
                return std::format(R"({{"status":"error","message":"{}"}})", e.what());
            }
        },
        "directory"
    );
    register_function("directory", contacts_import_macos_def);
    register_function("contacts", contacts_import_macos_def);

    // Register Persona MCP functions
    function_definition list_personas_def(
        "list_personas",
        "List all available AI personas in Rouen, including their descriptions, system prompts, allowed MCP tools, and current active selection status.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& pm = PersonaManager::instance();
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

    function_definition enable_persona_def(
        "enable_persona",
        "Enable or switch the active AI persona in Rouen by name or 0-based index (e.g. 'Data Cruncher', 'Terminal Hack', 'Rouen Assistant').",
        R"mcp({"type":"object","properties":{"name":{"type":"string","description":"Name of the persona to enable (e.g. 'Data Cruncher', 'Terminal Hack', 'Rouen Assistant')"},"index":{"type":"integer","description":"0-based index of the persona in the persona list"}},"required":[]})mcp",
        [](const std::string& params) -> std::string {
            try {
                mcp_enable_persona_params req{};
                if (!params.empty()) {
                    static_cast<void>(glz::read_json(req, params));
                }

                auto& pm = PersonaManager::instance();
                const auto& personas = pm.get_personas();

                int target_idx = -1;
                if (!req.name.empty()) {
                    std::string target_name = ::helpers::StringHelper::to_lower(req.name);
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

    function_definition get_active_persona_def(
        "get_active_persona",
        "Get information about the currently active AI persona in Rouen.",
        R"mcp({"type":"object","properties":{}})mcp",
        [](const std::string& /*params*/) -> std::string {
            try {
                auto& pm = PersonaManager::instance();
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
}

void mcp_service::register_function(const std::string& card_type, const function_definition& func) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a copy of the function with the card_type set
    function_definition func_copy(func.name, func.description, func.schema, func.handler, card_type);
    
    // Register in main function map
    functions_.emplace(func.name, std::move(func_copy));
    
    // Add to card function registry
    card_functions_[card_type].push_back(func.name);
    
    DEBUG_TRACE("MCP: Registered function '" + func.name + "' from card '" + card_type + "'");
}

void mcp_service::unregister_card_functions(const std::string& card_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
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

std::vector<mcp_service::function_definition> mcp_service::get_available_functions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<function_definition> result;
    result.reserve(functions_.size());
    
    for (const auto& [name, func] : functions_) {
        result.push_back(func);
    }
    
    return result;
}

std::vector<mcp_service::function_definition> mcp_service::get_functions_for_card(const std::string& card_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
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

mcp_service::execution_result mcp_service::execute_function(const std::string& name, const std::string& params) {
    function_definition func;
    {
        std::lock_guard<std::mutex> lock(mutex_);
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

bool mcp_service::has_function(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return functions_.find(name) != functions_.end();
}

std::string mcp_service::get_function_schema(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto func_it = functions_.find(name);
    if (func_it != functions_.end()) {
        return func_it->second.schema;
    }
    return "";
}

std::string mcp_service::get_functions_description() const {
    std::lock_guard<std::mutex> lock(mutex_);
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

bool mcp_service::validate_parameters(const std::string& params, const std::string& schema) {
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

void mcp_service::detect_system_info() {
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
    std::string cmd = "for cmd in brew nix apt dnf pacman yum zypper apk port; do command -v $cmd >/dev/null 2>&1 && echo $cmd; done";
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
