#pragma once

#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

struct mg_mgr;
struct mg_connection;
struct mg_http_message;

namespace rouen::hosts {

/**
 * Embedded HTTP REST API Server Host
 * 
 * Runs an embedded Mongoose HTTP server context listening on port 8081 for
 * card lifecycle, camera layout, and AI schema requests.
 */
class api_server_host {
public:
    api_server_host();
    ~api_server_host();

    bool initialize();
    bool start(const std::string& address);
    void stop();

private:
    void server_loop();
    static void event_handler(struct mg_connection* c, int ev, void* ev_data);
    static void handle_request(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_card_creation(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_ai_request(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_schemas_request(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_cast_status(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_cast_start(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_cast_play(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_camera_status(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_camera_snapshot(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_camera_layout_get(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_camera_layout_set(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_card_focus(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_deck_scroll(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_window_get(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_window_set(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_screenshot(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_status(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_prepare(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_start(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_next_stage(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_stop(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_run(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_test_audio(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_test_video(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_adlib_test_mux(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_swagger_ui(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_openapi_spec(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_rss_diagnostics(struct mg_connection* c, struct mg_http_message* hm);

    std::unique_ptr<struct mg_mgr> mgr_;
    struct mg_connection* conn_;
    bool initialized_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> server_thread_;
};

} // namespace rouen::hosts

namespace rouen::helpers {
    using api_server = ::rouen::hosts::api_server_host;
}
