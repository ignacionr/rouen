#include "trello_card.hpp"

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>

// 2. Libraries used in the project, in alphabetic order
// None

// 3. All other includes
#include "../../helpers/api_keys.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

trello_card::trello_card() : trello_host_(hosts::get_trello_host()) {
    name("Trello");
    
    // Try to load existing credentials into form
    auto api_key = helpers::ApiKeys::get_trello_api_key();
    auto token = helpers::ApiKeys::get_trello_token();
    
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
    selected_board_id_ = board_id;  // Pre-select the board
    name("Trello - Board");

    colors[0] = ImVec4(0.0f, 0.5f, 1.0f, 1.0f); // Primary color
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 0.3f); // Secondary color
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Error color
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Success color
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Warning color
}

trello_card::trello_card(const std::string& entity_id, card_context context) 
    :  trello_host_(hosts::get_trello_host()), context_(context) {
    name("Trello");
    
    // Try to load existing credentials into form
    auto api_key = helpers::ApiKeys::get_trello_api_key();
    auto token = helpers::ApiKeys::get_trello_token();
    
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
            // Standard trello: behavior
            break;
        case card_context::board_specific:
            initial_board_id_ = entity_id;
            selected_board_id_ = entity_id;  // Pre-select the board
            name("Trello - Board");
            break;
        case card_context::card_specific:
            initial_card_id_ = entity_id;
            name(std::format("Trello - Card: {}", entity_id));
            break;
    }
    colors[0] = ImVec4(0.0f, 0.7f, 1.0f, 1.0f); // Blue primary color
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 0.3f); // Secondary color
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Error color
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Success color
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Warning color
}

std::string trello_card::get_uri() const {
    switch (context_) {
        case card_context::general:
            return "trello";
        case card_context::board_specific:
            return "trello-board:" + initial_board_id_;
        case card_context::card_specific:
            return "trello-card:" + initial_card_id_;
    }
    // This should never be reached, but required for some compilers
    return "trello";
}

bool trello_card::render() {
    return render_window([this]() {
        if (!trello_host_->is_connected()) {
            render_connection_screen();
        } else {
            render_main_interface();
        }
        
        // Check for completed async operations
        check_async_operations();
    });
}

// Utility methods
ImVec4 trello_card::get_label_color(const std::string& color_name) {
    // Trello label colors
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
    return {0.5f, 0.5f, 0.5f, 1.0f}; // Default gray
}

std::string trello_card::format_due_date(const std::string& due_date) {
    // Simple date formatting - could be enhanced with proper date parsing
    return due_date.substr(0, 10); // Just return YYYY-MM-DD part
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
    
    // Create a new Trello card for this specific card
    "create_card"_sfn("trello-card:" + card_id);
}

} // namespace rouen::cards
