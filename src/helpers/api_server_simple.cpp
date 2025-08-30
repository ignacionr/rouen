#include "api_server.hpp"
#include "registrar.hpp"
#include "mcp_service.hpp"
#include <iostream>
#include <sstream>

namespace rouen::helpers {

api_server::api_server()
    : mgr_()
    , connection_(nullptr)
{
}

api_server::~api_server() {
    stop();
}

bool api_server::initialize(int port) {
    return true;
}

void api_server::start() {
    std::cout << "API server started" << std::endl;
}

void api_server::stop() {
}

void api_server::event_handler(struct mg_connection* c, int ev, void* ev_data) {
}

void api_server::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
}

void api_server::handle_card_creation(struct mg_connection* c, struct mg_http_message* hm) {
}

void api_server::handle_ai_request(struct mg_connection* c, struct mg_http_message* hm) {
}

}
