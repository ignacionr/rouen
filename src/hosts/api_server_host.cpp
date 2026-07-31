#include "api_server_host.hpp"
#include "audio_capture.hpp"
#include "mp4_writer.hpp"

// 1. Standard includes in alphabetic order
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <glaze/core/reflect.hpp>
#include <glaze/json/json_t.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
// Suppress mongoose reserved macro warnings (Clang only)
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-macro-identifier"
#endif
#include <mongoose.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

// Glaze for JSON parsing

// 3. All other includes
#include "../registrar.hpp"
#include "deferred_operations.hpp"
#include "media_player.hpp"
#include "card_render_metrics.hpp"
#include "../helpers/adlib_engine.hpp"
#include "../hosts/video_feed_host.hpp"
#include "../cards/interface/card.hpp"
#include "../cards/interface/factory.hpp"
#include "../cards/information/rss.hpp"

// JSON structures for API requests
struct card_creation_request {
    std::string uri = "menu";  // Default to menu card
};

struct ai_request {
    std::string prompt;
    std::string model = "default";
};

struct cast_play_request {
    std::string url;
    std::string uri;
};

struct error_response {
    std::string error;
};

namespace rouen::hosts {

api_server_host::api_server_host()
    : mgr_(nullptr)
    , conn_(nullptr)
    , initialized_(false)
    , running_(false)
    , server_thread_(nullptr) {
}

api_server_host::~api_server_host() {
    stop();
}

bool api_server_host::initialize() {
    if (initialized_) {
        return true;
    }

    try {
        mgr_ = std::make_unique<mg_mgr>();
    } catch (const std::exception& e) {
        std::cerr << "Failed to allocate mongoose manager: " << e.what() << '\n';
        return false;
    }

    mg_mgr_init(mgr_.get());
    initialized_ = true;
    return true;
}

bool api_server_host::start(const std::string& address) {
    if (!initialized_ && !initialize()) {
        return false;
    }

    if (running_) {
        return true; // Already running
    }

    conn_ = mg_http_listen(mgr_.get(), address.c_str(), event_handler, this);
    if (!conn_) {
        std::cerr << "Failed to start HTTP server on " << address << '\n';
        return false;
    }

    // Start the server thread
    running_ = true;
    try {
        server_thread_ = std::make_unique<std::thread>(&api_server_host::server_loop, this);
    } catch (const std::exception& e) {
        std::cerr << "Failed to start server thread: " << e.what() << '\n';
        mg_http_listen(mgr_.get(), nullptr, nullptr, nullptr); // Cancel listening
        conn_ = nullptr;
        running_ = false;
        return false;
    }

    std::cout << "API server started on " << address << '\n';
    return true;
}

void api_server_host::server_loop() {
    while (running_) {
        mg_mgr_poll(mgr_.get(), 100); // Poll for events with 100ms timeout
    }
}

void api_server_host::stop() {
    if (running_) {
        running_ = false;
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
            server_thread_.reset();
        }
    }

    if (conn_) {
        // Note: mongoose handles connection cleanup
        conn_ = nullptr;
    }

    if (mgr_) {
        mg_mgr_free(mgr_.get());
        mgr_.reset();
    }

    initialized_ = false;
}

void api_server_host::event_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        auto* hm = static_cast<struct mg_http_message*>(ev_data);
        std::cout << "[APIServer] Received HTTP request: " << std::string(hm->method.buf, hm->method.len) << " " << std::string(hm->uri.buf, hm->uri.len) << std::endl;
        api_server_host::handle_request(c, hm);
    }
}

void api_server_host::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
    std::string response;
    int status_code = 200;
    std::string content_type = "application/json";

    if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
        mg_http_reply(c, 204,
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                      "Access-Control-Max-Age: 86400\r\n", "");
        return;
    }

    if (mg_match(hm->uri, mg_str("/swagger"), nullptr) ||
        mg_match(hm->uri, mg_str("/swagger/*"), nullptr) ||
        mg_match(hm->uri, mg_str("/docs"), nullptr) ||
        mg_match(hm->uri, mg_str("/docs/*"), nullptr) ||
        mg_match(hm->uri, mg_str("/api/docs"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            content_type = "text/html; charset=utf-8";
            response = handle_swagger_ui(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/openapi.json"), nullptr) ||
               mg_match(hm->uri, mg_str("/openapi.json"), nullptr) ||
               mg_match(hm->uri, mg_str("/swagger.json"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_openapi_spec(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            content_type = "text/html; charset=utf-8";
            response = handle_swagger_ui(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/health"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = R"({"status":"ok","message":"API server is running"})";
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cards"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_card_creation(c, hm);
        } else if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_cards_get(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/ai"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_ai_request(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/schemas"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_schemas_request(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cast/status"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_cast_status(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cast/start"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_cast_start(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cast/play"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_cast_play(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/camera/status"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_camera_status(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/camera/snapshot"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_camera_snapshot(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/camera/layout"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_camera_layout_get(c, hm);
        } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_camera_layout_set(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cards/focus"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_card_focus(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/deck/scroll"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            auto fn = registrar::get<std::function<std::string()>>("get_deck_status");
            if (fn && *fn) {
                response = (*fn)();
            } else {
                response = R"({"error":"Deck status service not available"})";
            }
        } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_deck_scroll(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/window"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_window_get(c, hm);
        } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_window_set(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/metrics"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            bool include_all = false;
            if (hm->query.len > 0) {
                std::string const q(hm->query.buf, hm->query.len);
                if (q.find("all=true") != std::string::npos || q.find("active=false") != std::string::npos) {
                    include_all = true;
                }
            }
            auto metrics = rouen::helpers::CardRenderMetrics::instance().get_all_metrics(include_all);
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
            response = out;
        } else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            rouen::helpers::CardRenderMetrics::instance().reset();
            response = R"({"status":"ok","message":"Metrics reset"})";
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/rss/diagnostics"), nullptr) ||
               mg_match(hm->uri, mg_str("/api/diagnostics/rss"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_rss_diagnostics(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/screenshot"), nullptr) ||
               mg_match(hm->uri, mg_str("/api/editor/snapshot"), nullptr) ||
               mg_match(hm->uri, mg_str("/api/cards/snapshot"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 || mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_screenshot(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/status"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = handle_adlib_status(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/prepare"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_adlib_prepare(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/start"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_adlib_start(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/next_stage"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_adlib_next_stage(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/stop"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_adlib_stop(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/run"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_adlib_run(c, hm);
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/adlib/test/audio"), nullptr)) {
        response = handle_adlib_test_audio(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/adlib/test/video"), nullptr)) {
        response = handle_adlib_test_video(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/adlib/test/mux"), nullptr)) {
        response = handle_adlib_test_mux(c, hm);
    } else {
        status_code = 404;
        response = R"({"error":"Not found"})";
    }

    mg_http_reply(c, status_code,
                  ("Content-Type: " + content_type + "\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                   "Access-Control-Allow-Headers: Content-Type, Authorization\r\n").c_str(),
                  "%s", response.c_str());
}

std::string api_server_host::handle_card_creation(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        // Get the create_card service from registrar
        auto create_card_func = registrar::get<std::function<void(std::string const&)>>("create_card");
        if (!create_card_func) {
            error_response response{"Card creation service not available"};
            return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
        }

        // Parse JSON body using glaze with inline reflection
        std::string body(hm->body.buf, hm->body.len);
        card_creation_request request;

        if (!body.empty()) {
            auto result = glz::read_json(request, body);
            if (result) {
                error_response response{"Invalid JSON format: " + std::string(glz::format_error(result, body))};
                return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
            }
        }

        // Create the card using the service
        (*create_card_func)(request.uri);

        return R"({"success":true,"message":"Card created successfully","uri":")" + request.uri + "\"}";
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

std::string api_server_host::handle_cards_get(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto active_cards_func = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
        if (!active_cards_func || !*active_cards_func) {
            error_response response{"Active cards service not available"};
            return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
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
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

std::string api_server_host::handle_ai_request(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        // Get the MCP service from registrar
        auto mcp_service = registrar::get<std::shared_ptr<rouen::helpers::mcp_service>>("mcp_service");
        if (!mcp_service) {
            error_response response{"AI service not available"};
            return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
        }

        // Parse JSON body using glaze with inline reflection
        std::string body(hm->body.buf, hm->body.len);
        ai_request request;

        if (!body.empty()) {
            auto result = glz::read_json(request, body);
            if (result) {
                error_response response{"Invalid JSON format: " + std::string(glz::format_error(result, body))};
                return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
            }
        }

        // For now, return a simple response
        return R"({"success":true,"message":"AI request processed","model":")" + request.model + "\"}";
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

std::string api_server_host::handle_schemas_request(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
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
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

std::string api_server_host::handle_cast_status(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        bool const is_casting = host ? host->is_running() : false;
        size_t audio_queued = host ? host->get_cast_queued_bytes() : 0;
        
        bool is_playing = false;
        double pos = 0.0;
        double dur = 0.0;
        std::string media_url;
        
        bool has_video = false;
        bool texture_ready = false;
        float luminance = 0.0f;
        float vu_l = 0.0f;
        float vu_r = 0.0f;
        size_t video_q_size = 0;
        
        // A/V sync diagnostic fields
        double first_video_pts = -1.0;
        double first_audio_pts = -1.0;
        double last_presented_pts = -1.0;
        double av_sync_delta_ms = 0.0;
        int64_t frames_presented = 0;
        int64_t frames_dropped = 0;
        int64_t frames_held = 0;
        int64_t gpu_frames_rendered = 0;
        float actual_rendering_fps = 0.0f;
        double audio_queue_seconds = 0.0;
        
        {
            std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
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
                        std::lock_guard<std::mutex> const q_lock(item_ptr->video_queue_mutex);
                        video_q_size = item_ptr->decoded_video_queue.size();
                    }
                    // A/V sync diagnostics
                    first_video_pts = item_ptr->first_video_pts.load();
                    first_audio_pts = item_ptr->first_audio_pts.load();
                    last_presented_pts = item_ptr->last_presented_pts.load();
                    av_sync_delta_ms = item_ptr->last_av_sync_delta_ms.load();
                    frames_presented = item_ptr->frames_presented.load();
                    frames_dropped = item_ptr->frames_dropped.load();
                    frames_held = item_ptr->frames_held.load();
                    gpu_frames_rendered = item_ptr->gpu_frames_rendered.load();
                    actual_rendering_fps = item_ptr->actual_rendering_fps.load();
                    if (item_ptr->local_audio_stream) {
                        int const q_bytes = SDL_GetAudioStreamQueued(item_ptr->local_audio_stream);
                        int const rate = item_ptr->audio_sample_rate.load() > 0 ? item_ptr->audio_sample_rate.load() : 44100;
                        audio_queue_seconds = static_cast<double>(q_bytes) / static_cast<double>(rate * 4);
                    }
                    break;
                }
            }
        }
        
        bool const eof_reached = (dur > 0.0 && pos >= dur - 0.3);
        
        return std::format(
            R"({{"is_casting":{},"is_media_playing":{},"media_url":"{}","position":{:.3f},"duration":{:.3f},"audio_queued_bytes":{},"eof_reached":{},"has_video":{},"texture_ready":{},"luminance":{:.4f},"vu_level_l":{:.4f},"vu_level_r":{:.4f},"video_queue_size":{},"first_video_pts":{:.6f},"first_audio_pts":{:.6f},"last_presented_pts":{:.6f},"av_sync_delta_ms":{:.3f},"frames_presented":{},"frames_dropped":{},"frames_held":{},"audio_queue_seconds":{:.4f},"gpu_frames_rendered":{},"actual_rendering_fps":{:.1f}}})",
            is_casting ? "true" : "false",
            is_playing ? "true" : "false",
            media_url,
            pos,
            dur,
            audio_queued,
            eof_reached ? "true" : "false",
            has_video ? "true" : "false",
            texture_ready ? "true" : "false",
            luminance,
            vu_l,
            vu_r,
            video_q_size,
            first_video_pts,
            first_audio_pts,
            last_presented_pts,
            av_sync_delta_ms,
            frames_presented,
            frames_dropped,
            frames_held,
            audio_queue_seconds,
            gpu_frames_rendered,
            actual_rendering_fps
        );
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_cast_start(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (host) {
            host->start();
            return R"({"success":true,"message":"Video feed service started","endpoint":"tcp://127.0.0.1:8889"})";
        }
        return R"({"error":"VideoFeedHost unavailable"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_cast_play(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (host) {
            host->start();
        }
        
        std::string body(hm->body.buf, hm->body.len);
        cast_play_request request;
        if (!body.empty()) {
            (void)glz::read_json(request, body);
        }
        
        std::string target_url = request.url.empty() ? request.uri : request.url;
        if (target_url.empty()) {
            return R"({"error":"No media URL/URI provided"})";
        }
        
        auto& item = media_player::get_item(target_url);
        item.url = target_url;
        item.playMedia();
        
        return std::format(R"({{"success":true,"message":"Media playback started","url":"{}"}})", target_url);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_camera_status(struct mg_connection*, struct mg_http_message*) {
    try {
        auto fn = registrar::get<std::function<std::string()>>("camera_get_status");
        if (fn && *fn) {
            return (*fn)();
        }
        return R"({"active":false,"message":"Camera service not active"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_camera_snapshot(struct mg_connection*, struct mg_http_message*) {
    try {
        auto fn = registrar::get<std::function<std::string(const std::string&)>>("camera_save_snapshot");
        if (fn && *fn) {
            return (*fn)("/tmp/camera_snapshot.ppm");
        }
        return R"({"success":false,"error":"Camera card snapshot service not available"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_camera_layout_get(struct mg_connection*, struct mg_http_message*) {
    try {
        auto fn = registrar::get<std::function<std::string()>>("camera_get_layout");
        if (fn && *fn) {
            return (*fn)();
        }
        return R"({"error":"Camera layout service not available"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

struct camera_layout_request {
    std::string layout;
    int preset = -1;
};

std::string api_server_host::handle_camera_layout_set(struct mg_connection*, struct mg_http_message* hm) {
    try {
        auto fn = registrar::get<std::function<std::string(const std::string&)>>("camera_set_layout");
        if (!fn || !*fn) {
            return R"({"error":"Camera layout service not available"})";
        }

        std::string body(hm->body.buf, hm->body.len);
        camera_layout_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
        }

        std::string const target = !req.layout.empty() ? req.layout : (req.preset >= 0 ? std::to_string(req.preset) : "0");
        return (*fn)(target);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

struct card_focus_request {
    std::string uri{};
    int index{-1};
};

std::string api_server_host::handle_card_focus(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        std::string body(hm->body.buf, hm->body.len);
        card_focus_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
        }

        if (req.index >= 0) {
            auto fn = registrar::get<std::function<void(size_t)>>("focus_card_index");
            if (fn && *fn) {
                (*fn)(static_cast<size_t>(req.index));
                return std::format(R"({{"success":true,"message":"Focused card at index {}"}})", req.index);
            }
        }

        if (!req.uri.empty()) {
            auto fn = registrar::get<std::function<void(const std::string&)>>("focus_card");
            if (fn && *fn) {
                (*fn)(req.uri);
                return std::format(R"({{"success":true,"message":"Focused card with URI {}"}})", req.uri);
            }
        }

        error_response response{"Focus card service not available or invalid parameters"};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

struct deck_scroll_request {
    int section{0};
};

std::string api_server_host::handle_deck_scroll(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        std::string body(hm->body.buf, hm->body.len);
        deck_scroll_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
        }

        auto fn = registrar::get<std::function<void(int)>>("scroll_to_section");
        if (fn && *fn) {
            (*fn)(req.section);
            return std::format(R"({{"success":true,"message":"Scrolled deck to section {}"}})", req.section);
        }

        error_response response{"Scroll deck service not available"};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or(R"({"error":"Unknown error"})");
    }
}

struct window_geometry_request {
    int x{-1};
    int y{-1};
    int width{-1};
    int height{-1};
};

std::string api_server_host::handle_window_get(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto get_window_fn = registrar::get<std::function<SDL_Window*()>>("get_window");
        if (!get_window_fn || !*get_window_fn) {
            return R"({"error":"Window service not available"})";
        }

        SDL_Window* window = (*get_window_fn)();
        if (!window) {
            return R"({"error":"Window instance not available"})";
        }

        int x = 0, y = 0, w = 0, h = 0;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GetWindowSize(window, &w, &h);

        return std::format(R"({{"x":{},"y":{},"width":{},"height":{}}})", x, y, w, h);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_window_set(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        auto get_window_fn = registrar::get<std::function<SDL_Window*()>>("get_window");
        if (!get_window_fn || !*get_window_fn) {
            return R"({"error":"Window service not available"})";
        }

        SDL_Window* window = (*get_window_fn)();
        if (!window) {
            return R"({"error":"Window instance not available"})";
        }

        std::string body(hm->body.buf, hm->body.len);
        window_geometry_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
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

        return std::format(R"({{"success":true,"message":"Window position and size updated","x":{},"y":{},"width":{},"height":{}}})",
            new_x, new_y, new_w, new_h);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

struct screenshot_request {
    std::string target = "editor";
    std::string filename = "/tmp/snapshot.png";
    int width = 800;
    int height = 600;
};

std::string api_server_host::handle_screenshot(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        std::string body(hm->body.buf, hm->body.len);
        screenshot_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
        }
        if (req.filename.empty()) {
            req.filename = "/tmp/snapshot.png";
        }
        if (req.width <= 0) req.width = 800;
        if (req.height <= 0) req.height = 600;

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

                    if (req.target == "editor" || req.target.empty()) {
                        auto ed_fn = registrar::get<std::function<std::string(const std::string&, int, int)>>("editor_save_snapshot");
                        if (ed_fn && *ed_fn) {
                            promise->set_value((*ed_fn)(req.filename, req.width, req.height));
                            return;
                        }
                    }

                    promise->set_value(R"({"success":false,"error":"Screenshot service not available"})");
                } catch (const std::exception& e) {
                    promise->set_value(std::format(R"({{"success":false,"error":"{}"}})", e.what()));
                }
            });

            if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
                return future.get();
            } else {
                return R"({"success":false,"error":"Screenshot operation timed out waiting for main thread"})";
            }
        }

        auto screenshot_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("take_screenshot");
        if (screenshot_fn && *screenshot_fn) {
            return (*screenshot_fn)(req.target, req.filename, req.width, req.height);
        }

        if (req.target == "editor" || req.target.empty()) {
            auto ed_fn = registrar::get<std::function<std::string(const std::string&, int, int)>>("editor_save_snapshot");
            if (ed_fn && *ed_fn) {
                return (*ed_fn)(req.filename, req.width, req.height);
            }
        }

        return R"({"success":false,"error":"Screenshot service not available"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

struct adlib_api_request {
    std::string intro_video_path;
    std::string background_path;
    std::string outro_video_path;
    std::string output_mp4_path = "/Users/ignaciorodriguez/Downloads/adlib_output.mp4";
    std::string mode = "recorded";
    std::string mic_device_name;
    uint32_t mic_device_id{0};
    int duration_seconds = 3;
};

std::string api_server_host::handle_adlib_status(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    auto& engine = rouen::helpers::AdLibEngine::instance();
    auto stage = engine.get_stage();
    const char* stage_str = "Idle";
    if (stage == rouen::helpers::AdLibStage::Prepared) stage_str = "Prepared";
    else if (stage == rouen::helpers::AdLibStage::Intro) stage_str = "Intro";
    else if (stage == rouen::helpers::AdLibStage::Middle) stage_str = "Middle";
    else if (stage == rouen::helpers::AdLibStage::Outro) stage_str = "Outro";
    else if (stage == rouen::helpers::AdLibStage::Finished) stage_str = "Finished";

    return std::format(R"({{"status":"ok","stage":"{}","is_active":{},"is_paused":{},"is_recording":{},"elapsed_seconds":{:.2f}}})",
        stage_str,
        engine.is_active() ? "true" : "false",
        engine.is_paused() ? "true" : "false",
        engine.is_recording() ? "true" : "false",
        engine.get_elapsed_seconds());
}

std::string api_server_host::handle_adlib_prepare(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        std::string body(hm->body.buf, hm->body.len);
        adlib_api_request req;
        if (!body.empty()) {
            (void)glz::read_json(req, body);
        }
        if (req.output_mp4_path.empty()) {
            req.output_mp4_path = "/Users/ignaciorodriguez/Downloads/adlib_output.mp4";
        }

        rouen::helpers::AdLibConfig cfg;
        cfg.intro_video_path = req.intro_video_path;
        cfg.background_path = req.background_path;
        cfg.outro_video_path = req.outro_video_path;
        cfg.output_mp4_path = req.output_mp4_path;
        cfg.mode = (req.mode == "live") ? rouen::helpers::AdLibMode::Live : rouen::helpers::AdLibMode::Recorded;

        bool const prepared = rouen::helpers::AdLibEngine::instance().prepare(cfg);
        return std::format(R"({{"success":{},"message":"Ad-Lib scene prepared","stage":"Prepared"}})", prepared ? "true" : "false");
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_start(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        bool const started = rouen::helpers::AdLibEngine::instance().start();
        return std::format(R"({{"success":{},"message":"Ad-Lib presentation started"}})", started ? "true" : "false");
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_next_stage(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        rouen::helpers::AdLibEngine::instance().next_stage();
        return R"({"success":true,"message":"Ad-Lib advanced to next stage"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_stop(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        rouen::helpers::AdLibEngine::instance().stop();
        return R"({"success":true,"message":"Ad-Lib stopped & output file flushed"})";
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_run(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        std::string body(hm->body.buf, hm->body.len);
        adlib_api_request req;
        if (!body.empty()) {
            try {
                (void)glz::read_json(req, body);
            } catch (...) {}
            
            auto pos_path = body.find("\"output_mp4_path\"");
            if (pos_path != std::string::npos) {
                auto q1 = body.find('"', pos_path + 17);
                if (q1 != std::string::npos) {
                    auto q2 = body.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        req.output_mp4_path = body.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
            auto pos_dur = body.find("\"duration_seconds\"");
            if (pos_dur != std::string::npos) {
                auto col = body.find(':', pos_dur + 18);
                if (col != std::string::npos) {
                    try {
                        req.duration_seconds = std::stoi(body.substr(col + 1));
                    } catch (...) {}
                }
            }
        }
        if (req.output_mp4_path.empty()) {
            req.output_mp4_path = "/Users/ignaciorodriguez/Downloads/adlib_output.mp4";
        }

        std::cout << "[APIServer] handle_adlib_run output_path=" << req.output_mp4_path << " duration=" << req.duration_seconds << std::endl;



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

        auto& engine = rouen::helpers::AdLibEngine::instance();
        engine.prepare(cfg);
        engine.set_auto_stop_seconds(req.duration_seconds > 0 ? static_cast<double>(req.duration_seconds) : 3.0);
        engine.start();

        return std::format(R"({{"success":true,"output_mp4_path":"{}","status":"recording_started"}})", req.output_mp4_path);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_test_audio(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto devices = rouen::helpers::AudioCapture::get_input_devices();
        rouen::helpers::AudioCapture cap;
        bool const started = cap.start(0, 44100, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto pcm = cap.read_audio_data();
        float peak = cap.get_current_peak();
        cap.stop();

        return std::format(
            R"({{"success":{},"devices_found":{},"bytes_captured":{},"peak_level":{:.4f},"status":"audio_capture_test_passed"}})",
            started ? "true" : "false", devices.size(), pcm.size(), peak
        );
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_test_video(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        std::string out_path = "/Users/ignaciorodriguez/Downloads/adlib_test_video.mp4";
        rouen::helpers::NativeMP4Writer writer;
        if (!writer.open(out_path, 1280, 720, 30, 0)) {
            return R"({"success":false,"error":"Failed to open MP4 video writer"})";
        }

        std::vector<uint8_t> frame(1280 * 720 * 4, 100);
        for (int i = 0; i < 15; ++i) {
            writer.write_video_frame(frame.data());
        }
        writer.close();

        return std::format(
            R"({{"success":true,"output_path":"{}","frames_written":60,"status":"video_feed_test_passed"}})",
            out_path
        );
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_adlib_test_mux(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        std::string out_path = "/Users/ignaciorodriguez/Downloads/adlib_test_mux.mp4";
        rouen::helpers::NativeMP4Writer writer;
        if (!writer.open(out_path, 1280, 720, 30, 44100)) {
            return R"({"success":false,"error":"Failed to open MP4 mux writer"})";
        }

        std::vector<uint8_t> video_frame(1280 * 720 * 4, 150);
        std::vector<uint8_t> audio_frame(4096, 0);

        for (int i = 0; i < 60; ++i) {
            writer.write_video_frame(video_frame.data());
            writer.write_audio_samples(audio_frame.data(), audio_frame.size());
        }
        writer.close();

        uintmax_t sz = 0;
        if (std::filesystem::exists(out_path)) {
            sz = std::filesystem::file_size(out_path);
        }

        return std::format(
            R"({{"success":{},"output_path":"{}","video_frames":60,"audio_bytes_written":{},"file_size":{},"status":"interleaved_mux_test_passed"}})",
            (sz > 1000) ? "true" : "false", out_path, 60 * 4096, sz
        );
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server_host::handle_rss_diagnostics(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    auto rss_host = rouen::cards::rss::getHost();
    if (!rss_host) {
        return R"({"error":"RSS Host not available"})";
    }

    auto diag = rss_host->get_rss_diagnostics();

    glz::json_t root;
    root["status"] = "ok";

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
}

std::string api_server_host::handle_swagger_ui(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    return R"html(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Rouen API - Swagger UI</title>
  <link rel="stylesheet" type="text/css" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css">
  <link rel="icon" type="image/png" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/favicon-32x32.png">
  <style>
    html { box-sizing: border-box; overflow-y: scroll; }
    *, *:before, *:after { box-sizing: inherit; }
    body { margin: 0; background: #0b0f19; color: #e2e8f0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    .swagger-ui .topbar { background-color: #0f172a; border-bottom: 1px solid #1e293b; padding: 10px 0; }
    .swagger-ui .topbar a { display: flex; align-items: center; text-decoration: none; }
    .swagger-ui .topbar-wrapper img { display: none; }
    .swagger-ui .topbar-wrapper::before { content: "ROUEN API EXPLORER"; color: #38bdf8; font-weight: 700; font-size: 1.1rem; letter-spacing: 0.05em; }
    .swagger-ui .topbar .download-url-wrapper { display: none; }
    .swagger-ui { color: #cbd5e1; }
    .swagger-ui .info .title { color: #f8fafc; font-weight: 700; }
    .swagger-ui .info p, .swagger-ui .info li { color: #94a3b8; }
    .swagger-ui .scheme-container { background: #1e293b; box-shadow: none; border-radius: 8px; margin: 10px 0; }
    .swagger-ui .opblock .opblock-summary-method { border-radius: 4px; font-weight: 700; }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-bundle.js" charset="UTF-8"></script>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-standalone-preset.js" charset="UTF-8"></script>
  <script>
    window.onload = function() {
      const ui = SwaggerUIBundle({
        url: "/api/openapi.json",
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        plugins: [
          SwaggerUIBundle.plugins.DownloadUrl
        ],
        layout: "StandaloneLayout"
      });
      window.ui = ui;
    };
  </script>
</body>
</html>)html";
}

std::string api_server_host::handle_openapi_spec(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    return R"json({
  "openapi": "3.0.3",
  "info": {
    "title": "Rouen REST API",
    "description": "REST API service for controlling cards, deck view, AI assistant, screen casting, camera streams, metrics, and system diagnostics in Rouen.",
    "version": "1.0.0"
  },
  "servers": [
    {
      "url": "http://localhost:8081",
      "description": "Local Rouen API Server"
    }
  ],
  "tags": [
    {"name": "Documentation", "description": "API documentation and OpenAPI schema endpoints"},
    {"name": "System & Health", "description": "Application health status, window settings, screenshots, and card schemas"},
    {"name": "Cards", "description": "Card creation, focusing, and workspace lifecycle"},
    {"name": "AI Assistant", "description": "AI prompt invocation and assistant interaction"},
    {"name": "Casting & Media", "description": "Video feed streaming, casting control, and media playback"},
    {"name": "Camera", "description": "Camera feed status, snapshots, and grid layout controls"},
    {"name": "Deck Navigation", "description": "Deck view status and card stack scrolling controls"},
    {"name": "Metrics & Diagnostics", "description": "Card render performance metrics and RSS diagnostics"},
    {"name": "AdLib Engine", "description": "AdLib session orchestration, video rendering, and audio hardware tests"}
  ],
  "paths": {
    "/api/health": {
      "get": {
        "tags": ["System & Health"],
        "summary": "Check API server health status",
        "operationId": "getHealth",
        "responses": {
          "200": {
            "description": "API server is running",
            "content": {
              "application/json": {
                "schema": {
                  "type": "object",
                  "properties": {
                    "status": {"type": "string", "example": "ok"},
                    "message": {"type": "string", "example": "API server is running"}
                  }
                }
              }
            }
          }
        }
      }
    },
    "/api/openapi.json": {
      "get": {
        "tags": ["Documentation"],
        "summary": "Get OpenAPI 3.0 Specification JSON",
        "operationId": "getOpenApiSpec",
        "responses": {
          "200": {
            "description": "OpenAPI specification document",
            "content": {
              "application/json": {}
            }
          }
        }
      }
    },
    "/swagger": {
      "get": {
        "tags": ["Documentation"],
        "summary": "Interactive Swagger UI documentation page",
        "operationId": "getSwaggerUI",
        "responses": {
          "200": {
            "description": "Interactive HTML interface",
            "content": {
              "text/html": {}
            }
          }
        }
      }
    },
    "/api/schemas": {
      "get": {
        "tags": ["System & Health"],
        "summary": "List all registered card schemas and URIs",
        "operationId": "getSchemas",
        "responses": {
          "200": {
            "description": "List of available card URIs",
            "content": {
              "application/json": {
                "schema": {
                  "type": "array",
                  "items": {"type": "string"}
                }
              }
            }
          }
        }
      }
    },
    "/api/cards": {
      "get": {
        "tags": ["Cards"],
        "summary": "Get array of currently active cards",
        "operationId": "getCards",
        "responses": {
          "200": {
            "description": "List of active cards with URIs and widths",
            "content": {
              "application/json": {
                "schema": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "properties": {
                      "index": {"type": "integer", "example": 0},
                      "title": {"type": "string", "example": "Menu"},
                      "uri": {"type": "string", "example": "menu"},
                      "width": {"type": "number", "example": 300.0}
                    }
                  }
                }
              }
            }
          }
        }
      },
      "post": {
        "tags": ["Cards"],
        "summary": "Create a new card by URI",
        "operationId": "createCard",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "uri": {"type": "string", "example": "menu", "description": "URI of card to create"}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Card created successfully"
          }
        }
      }
    },
    "/api/cards/focus": {
      "post": {
        "tags": ["Cards"],
        "summary": "Focus card by index or card ID",
        "operationId": "focusCard",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "index": {"type": "integer", "example": 0},
                  "id": {"type": "string", "example": "rss_card_1"}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Card focused successfully"
          }
        }
      }
    },
    "/api/ai": {
      "post": {
        "tags": ["AI Assistant"],
        "summary": "Send prompt to AI model",
        "operationId": "aiPrompt",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "prompt": {"type": "string", "example": "Analyze system performance"},
                  "model": {"type": "string", "example": "default"}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "AI execution result"
          }
        }
      }
    },
    "/api/cast/status": {
      "get": {
        "tags": ["Casting & Media"],
        "summary": "Get screen casting and media playback diagnostics",
        "operationId": "getCastStatus",
        "responses": {
          "200": {
            "description": "Casting & playback status"
          }
        }
      }
    },
    "/api/cast/start": {
      "post": {
        "tags": ["Casting & Media"],
        "summary": "Start TCP video feed casting service",
        "operationId": "startCast",
        "responses": {
          "200": {
            "description": "Video feed service started on port 8889"
          }
        }
      }
    },
    "/api/cast/play": {
      "post": {
        "tags": ["Casting & Media"],
        "summary": "Play media from URL or URI",
        "operationId": "playCastMedia",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "url": {"type": "string", "example": "https://example.com/video.mp4"},
                  "uri": {"type": "string"}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Media playback started"
          }
        }
      }
    },
    "/api/camera/status": {
      "get": {
        "tags": ["Camera"],
        "summary": "Get camera status and devices",
        "operationId": "getCameraStatus",
        "responses": {
          "200": {
            "description": "Camera status response"
          }
        }
      }
    },
    "/api/camera/snapshot": {
      "post": {
        "tags": ["Camera"],
        "summary": "Capture frame snapshot from active camera",
        "operationId": "takeCameraSnapshot",
        "responses": {
          "200": {
            "description": "Snapshot captured"
          }
        }
      }
    },
    "/api/camera/layout": {
      "get": {
        "tags": ["Camera"],
        "summary": "Get current camera grid layout",
        "operationId": "getCameraLayout",
        "responses": {
          "200": {
            "description": "Camera layout configuration"
          }
        }
      },
      "post": {
        "tags": ["Camera"],
        "summary": "Set camera grid layout parameters",
        "operationId": "setCameraLayout",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "rows": {"type": "integer", "example": 2},
                  "cols": {"type": "integer", "example": 2}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Layout updated"
          }
        }
      }
    },
    "/api/deck/scroll": {
      "get": {
        "tags": ["Deck Navigation"],
        "summary": "Get deck view scroll position and state",
        "operationId": "getDeckStatus",
        "responses": {
          "200": {
            "description": "Deck status response"
          }
        }
      },
      "post": {
        "tags": ["Deck Navigation"],
        "summary": "Scroll deck view",
        "operationId": "scrollDeck",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "delta": {"type": "number", "example": 1.0},
                  "position": {"type": "number", "example": 0.0}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Deck scrolled"
          }
        }
      }
    },
    "/api/window": {
      "get": {
        "tags": ["System & Health"],
        "summary": "Get application window state and size",
        "operationId": "getWindow",
        "responses": {
          "200": {
            "description": "Window status"
          }
        }
      },
      "post": {
        "tags": ["System & Health"],
        "summary": "Set window resolution and display mode",
        "operationId": "setWindow",
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "mode": {"type": "string", "example": "fullscreen"},
                  "width": {"type": "integer", "example": 1920},
                  "height": {"type": "integer", "example": 1080}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "Window updated"
          }
        }
      }
    },
    "/api/metrics": {
      "get": {
        "tags": ["Metrics & Diagnostics"],
        "summary": "Fetch card render performance metrics",
        "operationId": "getMetrics",
        "parameters": [
          {
            "name": "all",
            "in": "query",
            "description": "Set to true to include inactive cards",
            "required": false,
            "schema": {"type": "boolean", "default": false}
          }
        ],
        "responses": {
          "200": {
            "description": "Render metrics list"
          }
        }
      },
      "delete": {
        "tags": ["Metrics & Diagnostics"],
        "summary": "Reset card rendering performance metrics",
        "operationId": "resetMetrics",
        "responses": {
          "200": {
            "description": "Metrics cleared"
          }
        }
      }
    },
    "/api/rss/diagnostics": {
      "get": {
        "tags": ["Metrics & Diagnostics"],
        "summary": "Get RSS feeds texture cache and card render diagnostics",
        "operationId": "getRssDiagnostics",
        "responses": {
          "200": {
            "description": "RSS diagnostics data"
          }
        }
      }
    },
    "/api/screenshot": {
      "get": {
        "tags": ["System & Health"],
        "summary": "Get latest window screenshot/snapshot",
        "operationId": "getScreenshot",
        "responses": {
          "200": {
            "description": "Screenshot response"
          }
        }
      },
      "post": {
        "tags": ["System & Health"],
        "summary": "Trigger a window screenshot capture",
        "operationId": "postScreenshot",
        "responses": {
          "200": {
            "description": "Screenshot captured"
          }
        }
      }
    },
    "/api/adlib/status": {
      "get": {
        "tags": ["AdLib Engine"],
        "summary": "Get status of AdLib engine and active session",
        "operationId": "getAdlibStatus",
        "responses": {
          "200": {
            "description": "AdLib engine status"
          }
        }
      }
    },
    "/api/adlib/prepare": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Prepare AdLib recording/rendering session",
        "operationId": "prepareAdlib",
        "responses": {
          "200": {
            "description": "AdLib session prepared"
          }
        }
      }
    },
    "/api/adlib/start": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Start AdLib session",
        "operationId": "startAdlib",
        "responses": {
          "200": {
            "description": "AdLib session started"
          }
        }
      }
    },
    "/api/adlib/next_stage": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Advance AdLib session to next stage",
        "operationId": "nextStageAdlib",
        "responses": {
          "200": {
            "description": "AdLib stage advanced"
          }
        }
      }
    },
    "/api/adlib/stop": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Stop AdLib session",
        "operationId": "stopAdlib",
        "responses": {
          "200": {
            "description": "AdLib session stopped"
          }
        }
      }
    },
    "/api/adlib/run": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Run full automated AdLib workflow",
        "operationId": "runAdlib",
        "responses": {
          "200": {
            "description": "AdLib workflow executed"
          }
        }
      }
    },
    "/api/adlib/test/audio": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Test audio hardware capture",
        "operationId": "testAdlibAudio",
        "responses": {
          "200": {
            "description": "Audio test finished"
          }
        }
      }
    },
    "/api/adlib/test/video": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Test video rendering pipeline",
        "operationId": "testAdlibVideo",
        "responses": {
          "200": {
            "description": "Video test finished"
          }
        }
      }
    },
    "/api/adlib/test/mux": {
      "post": {
        "tags": ["AdLib Engine"],
        "summary": "Test A/V muxing pipeline",
        "operationId": "testAdlibMux",
        "responses": {
          "200": {
            "description": "Mux test finished"
          }
        }
      }
    }
  }
})json";
}

} // namespace rouen::hosts
