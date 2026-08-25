#include "trello_card.hpp"

#include <cstring>
#include <format>
#include <string>
#include <thread>

#include "helpers/api_keys.hpp"
#include "helpers/platform_utils.hpp"
#include "helpers/fetch.hpp"
#include "registrar.hpp"
#include "trello_host.hpp"

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

trello_card::trello_card() : trello_host_(hosts::get_trello_host()) {
    auto api_key = rouen::platform::get_env("TRELLO_API_KEY");
    auto token = rouen::platform::get_env("TRELLO_TOKEN");
    
    if (!api_key.empty() && api_key.length() < sizeof(api_key_buffer_)) {
        std::strncpy(api_key_buffer_, api_key.c_str(), sizeof(api_key_buffer_) - 1);
        api_key_buffer_[sizeof(api_key_buffer_) - 1] = '\0';
    }
    if (!token.empty() && token.length() < sizeof(token_buffer_)) {
        std::strncpy(token_buffer_, token.c_str(), sizeof(token_buffer_) - 1);
        token_buffer_[sizeof(token_buffer_) - 1] = '\0';
    }
}

trello_card::trello_card(const std::string& board_id) : trello_card() {
    context_ = card_context::board_specific;
    initial_board_id_ = board_id;
    selected_board_id_ = board_id;

    colors[0] = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 0.3f);
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
}

trello_card::trello_card(const std::string& entity_id, card_context context) 
    : trello_host_(hosts::get_trello_host()), context_(context) {
    auto api_key = rouen::platform::get_env("TRELLO_API_KEY");
    auto token = rouen::platform::get_env("TRELLO_TOKEN");
    
    if (!api_key.empty() && api_key.length() < sizeof(api_key_buffer_)) {
        std::strncpy(api_key_buffer_, api_key.c_str(), sizeof(api_key_buffer_) - 1);
        api_key_buffer_[sizeof(api_key_buffer_) - 1] = '\0';
    }
    if (!token.empty() && token.length() < sizeof(token_buffer_)) {
        std::strncpy(token_buffer_, token.c_str(), sizeof(token_buffer_) - 1);
        token_buffer_[sizeof(token_buffer_) - 1] = '\0';
    }
    
    switch (context) {
        case card_context::general:
            break;
        case card_context::board_specific:
            initial_board_id_ = entity_id;
            selected_board_id_ = entity_id;
            break;
        case card_context::card_specific:
            initial_card_id_ = entity_id;
            break;
    }
    colors[0] = ImVec4(0.0f, 0.7f, 1.0f, 1.0f);
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 0.3f);
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
}

void trello_card::draw() {
    if (!trello_host_->is_connected()) {
        render_connection_screen();
    } else if (context_ == card_context::card_specific) {
        render_card_interface();
    } else {
        render_main_interface();
    }
    
    check_async_operations();
}

std::string trello_card::title() const {
    switch (context_) {
        case card_context::general:
            return "Trello";
        case card_context::board_specific:
            return "Trello - Board";
        case card_context::card_specific:
            return "Trello - Card";
    }
    return "Trello";
}

std::string trello_card::uri() const {
    switch (context_) {
        case card_context::general:
            return "trello";
        case card_context::board_specific:
            return "trello-board:" + initial_board_id_;
        case card_context::card_specific:
            return "trello-card:" + initial_card_id_;
    }
    return "trello";
}

void trello_card::handle_uri(std::string_view locator) {
    std::string loc(locator);
    if (loc.starts_with("trello-board:")) {
        context_ = card_context::board_specific;
        initial_board_id_ = loc.substr(13);
        selected_board_id_ = initial_board_id_;
    } else if (loc.starts_with("trello-card:")) {
        context_ = card_context::card_specific;
        initial_card_id_ = loc.substr(12);
    } else if (!loc.empty() && loc != "trello") {
        context_ = card_context::board_specific;
        initial_board_id_ = loc;
        selected_board_id_ = loc;
    }
}

ImVec4 trello_card::get_label_color(const std::string& color_name) {
    if (color_name == "green") return {0.0f, 0.7f, 0.2f, 1.0f};
    if (color_name == "yellow") return {0.9f, 0.8f, 0.0f, 1.0f};
    if (color_name == "orange") return {1.0f, 0.5f, 0.0f, 1.0f};
    if (color_name == "red") return {0.9f, 0.2f, 0.2f, 1.0f};
    if (color_name == "purple") return {0.6f, 0.2f, 0.8f, 1.0f};
    if (color_name == "blue") return {0.2f, 0.5f, 0.9f, 1.0f};
    if (color_name == "sky") return {0.5f, 0.8f, 1.0f, 1.0f};
    if (color_name == "lime") return {0.5f, 1.0f, 0.0f, 1.0f};
    if (color_name == "pink") return {1.0f, 0.4f, 0.7f, 1.0f};
    if (color_name == "black") return {0.2f, 0.2f, 0.2f, 1.0f};
    return {0.5f, 0.5f, 0.5f, 1.0f};
}

std::string trello_card::format_due_date(const std::string& due_date) {
    return due_date.substr(0, 10);
}

void trello_card::open_in_browser(const std::string& url) {
    rouen::platform::open_url(url);
}

void trello_card::show_error(const std::string& error) {
    connection_error_ = error;
}

void trello_card::clear_error() {
    connection_error_.clear();
}

void trello_card::create_card_tab(const std::string& card_id) {
    if (card_id.empty()) return;
    create_card_uri("trello-card:" + card_id);
}

} // namespace rouen::cards
