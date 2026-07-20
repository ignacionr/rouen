#pragma once

#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

// Forward declarations for mongoose
struct mg_mgr;
struct mg_connection;
struct mg_http_message;

namespace rouen::helpers {

class api_server {
public:
    api_server();
    ~api_server();

    bool initialize();
    bool start(const std::string& address);
    void stop();

private:
    void server_loop();
    static void event_handler(struct mg_connection* c, int ev, void* ev_data);
    void handle_request(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_card_creation(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_ai_request(struct mg_connection* c, struct mg_http_message* hm);
    static std::string handle_schemas_request(struct mg_connection* c, struct mg_http_message* hm);

    struct mg_mgr* mgr_;
    struct mg_connection* conn_;
    bool initialized_;
    std::atomic<bool> running_;
    std::thread* server_thread_;
};

} // namespace rouen::helpers
