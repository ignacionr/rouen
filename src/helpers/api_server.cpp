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

    mgr_ = new mg_mgr();
    if (!mgr_) {
        std::cerr << "Failed to allocate mongoose manager" << std::endl;
        return false;
    }

    mg_mgr_init(mgr_);
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

    conn_ = mg_http_listen(mgr_, address.c_str(), event_handler, this);
    if (!conn_) {
        std::cerr << "Failed to start HTTP server on " << address << std::endl;
        return false;
    }

    // Start the server thread
    running_ = true;
    server_thread_ = new std::thread(&api_server::server_loop, this);

    std::cout << "API server started on " << address << std::endl;
    return true;
}

void api_server::server_loop() {
    while (running_) {
        mg_mgr_poll(mgr_, 100); // Poll for events with 100ms timeout
    }
}

void api_server::stop() {
    if (running_) {
        running_ = false;
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
            delete server_thread_;
            server_thread_ = nullptr;
        }
    }

    if (conn_) {
        // Note: mongoose handles connection cleanup
        conn_ = nullptr;
    }

    if (mgr_) {
        mg_mgr_free(mgr_);
        delete mgr_;
        mgr_ = nullptr;
    }

    initialized_ = false;
}

void api_server::event_handler(struct mg_connection* c, int ev, void* ev_data) {
    api_server* server = static_cast<api_server*>(c->fn_data);
    
    if (ev == MG_EV_HTTP_MSG) {
        auto* hm = static_cast<struct mg_http_message*>(ev_data);
        server->handle_request(c, hm);
    }
}

void api_server::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
    std::string response;
    int status_code = 200;
    std::string content_type = "application/json";

    if (mg_match(hm->uri, mg_str("/api/health"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            response = "{\"status\":\"ok\",\"message\":\"API server is running\"}";
        } else {
            status_code = 405;
            response = "{\"error\":\"Method not allowed\"}";
        }
    } else if (mg_match(hm->uri, mg_str("/api/cards"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_card_creation(c, hm);
        } else {
            status_code = 405;
            response = "{\"error\":\"Method not allowed\"}";
        }
    } else if (mg_match(hm->uri, mg_str("/api/ai"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            response = handle_ai_request(c, hm);
        } else {
            status_code = 405;
            response = "{\"error\":\"Method not allowed\"}";
        }
    } else {
        status_code = 404;
        response = "{\"error\":\"Not found\"}";
    }

    mg_http_reply(c, status_code, ("Content-Type: " + content_type + "\r\n").c_str(), "%s", response.c_str());
}

std::string api_server::handle_card_creation(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        // Get the create_card service from registrar
        auto create_card_func = registrar::get<std::function<void(std::string const&)>>("create_card");
        if (!create_card_func) {
            error_response response{"Card creation service not available"};
            return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
        }

        // Parse JSON body using glaze with inline reflection
        std::string body(hm->body.buf, hm->body.len);
        card_creation_request request;

        if (!body.empty()) {
            auto result = glz::read_json(request, body);
            if (!result) {
                error_response response{"Invalid JSON format"};
                return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
            }
        }

        // Create the card using the service
        (*create_card_func)(request.uri);

        return "{\"success\":true,\"message\":\"Card created successfully\",\"uri\":\"" + request.uri + "\"}";
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
    }
}

std::string api_server::handle_ai_request(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    try {
        // Get the MCP service from registrar
        auto mcp_service = registrar::get<std::shared_ptr<rouen::helpers::mcp_service>>("mcp_service");
        if (!mcp_service) {
            error_response response{"AI service not available"};
            return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
        }

        // Parse JSON body using glaze with inline reflection
        std::string body(hm->body.buf, hm->body.len);
        ai_request request;

        if (!body.empty()) {
            auto result = glz::read_json(request, body);
            if (!result) {
                error_response response{"Invalid JSON format"};
                return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
            }
        }

        // For now, return a simple response
        return "{\"success\":true,\"message\":\"AI request processed\",\"model\":\"" + request.model + "\"}";
    } catch (const std::exception& e) {
        error_response response{std::string(e.what())};
        return glz::write_json(response).value_or("{\"error\":\"Unknown error\"}");
    }
}

} // namespace rouen::helpers
