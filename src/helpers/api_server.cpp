#include "api_server.hpp"

// 1. Standard includes in alphabetic order
#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

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
#include <glaze/glaze.hpp>

// 3. All other includes
#include "../registrar.hpp"
#include "mcp_service.hpp"
#include "media_player.hpp"
#include "../hosts/video_feed_host.hpp"
#include "../cards/interface/card.hpp"
#include "../cards/interface/factory.hpp"

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

namespace rouen::helpers {

api_server::api_server()
    : mgr_(nullptr)
    , conn_(nullptr)
    , initialized_(false)
    , running_(false)
    , server_thread_(nullptr) {
}

api_server::~api_server() {
    stop();
}

bool api_server::initialize() {
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

bool api_server::start(const std::string& address) {
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
        server_thread_ = std::make_unique<std::thread>(&api_server::server_loop, this);
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

void api_server::server_loop() {
    while (running_) {
        mg_mgr_poll(mgr_.get(), 100); // Poll for events with 100ms timeout
    }
}

void api_server::stop() {
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

void api_server::event_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        auto* hm = static_cast<struct mg_http_message*>(ev_data);
        api_server::handle_request(c, hm);
    }
}

void api_server::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
    std::string response;
    int status_code = 200;
    std::string content_type = "application/json";

    if (mg_match(hm->uri, mg_str("/api/health"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = R"({"status":"ok","message":"API server is running"})";
        } else {
            status_code = 405;
            response = R"({"error":"Method not allowed"})";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cards"), nullptr)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_card_creation(c, hm);
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
    } else {
        status_code = 404;
        response = R"({"error":"Not found"})";
    }

    mg_http_reply(c, status_code, ("Content-Type: " + content_type + "\r\n").c_str(), "%s", response.c_str());
}

std::string api_server::handle_card_creation(struct mg_connection* /*c*/, struct mg_http_message* hm) {
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

std::string api_server::handle_ai_request(struct mg_connection* /*c*/, struct mg_http_message* hm) {
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

std::string api_server::handle_schemas_request(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
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

std::string api_server::handle_cast_status(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
    try {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        bool is_casting = host ? host->is_running() : false;
        size_t audio_queued = host ? host->get_cast_queued_bytes() : 0;
        
        bool is_playing = false;
        double pos = 0.0;
        double dur = 0.0;
        std::string media_url;
        
        {
            std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && item_ptr->is_playing) {
                    is_playing = true;
                    pos = item_ptr->position.load();
                    dur = item_ptr->duration.load();
                    media_url = item_ptr->url;
                    break;
                }
            }
        }
        
        bool eof_reached = (dur > 0.0 && pos >= dur - 0.3);
        
        return std::format(
            R"({{"is_casting":{},"is_media_playing":{},"media_url":"{}","position":{:.2f},"duration":{:.2f},"audio_queued_bytes":{},"eof_reached":{}}})",
            is_casting ? "true" : "false",
            is_playing ? "true" : "false",
            media_url,
            pos,
            dur,
            audio_queued,
            eof_reached ? "true" : "false"
        );
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server::handle_cast_start(struct mg_connection* /*c*/, struct mg_http_message* /*hm*/) {
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

std::string api_server::handle_cast_play(struct mg_connection* /*c*/, struct mg_http_message* hm) {
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
        
        unsigned int id = static_cast<unsigned int>(std::hash<std::string>{}(target_url));
        auto& item = media_player::get_item(id);
        item.url = target_url;
        item.playMedia();
        
        return std::format(R"({{"success":true,"message":"Media playback started","url":"{}"}})", target_url);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

std::string api_server::handle_camera_status(struct mg_connection*, struct mg_http_message*) {
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

std::string api_server::handle_camera_snapshot(struct mg_connection*, struct mg_http_message*) {
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

std::string api_server::handle_camera_layout_get(struct mg_connection*, struct mg_http_message*) {
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

std::string api_server::handle_camera_layout_set(struct mg_connection*, struct mg_http_message* hm) {
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

        std::string target = !req.layout.empty() ? req.layout : (req.preset >= 0 ? std::to_string(req.preset) : "0");
        return (*fn)(target);
    } catch (const std::exception& e) {
        return std::format(R"({{"error":"{}"}})", e.what());
    }
}

} // namespace rouen::helpers
