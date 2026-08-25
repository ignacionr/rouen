#include "trello_host.hpp"

#include <exception>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include "helpers/debug.hpp"
#include "trello_model.hpp"

#define TRELLO_HOST_ERROR(message) LOG_COMPONENT("TRELLO_HOST", LOG_LEVEL_ERROR, message)
#define TRELLO_HOST_INFO(message) LOG_COMPONENT("TRELLO_HOST", LOG_LEVEL_INFO, message)
#define TRELLO_HOST_DEBUG(message) LOG_COMPONENT("TRELLO_HOST", LOG_LEVEL_DEBUG, message)

#define TRELLO_HOST_ERROR_FMT(fmt, ...) TRELLO_HOST_ERROR(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

std::shared_ptr<trello_host> get_trello_host() {
    static std::mutex g_trello_host_mutex;
    static std::shared_ptr<trello_host> g_trello_host;

    std::lock_guard<std::mutex> const lock(g_trello_host_mutex);
    if (!g_trello_host) {
        g_trello_host = std::make_shared<trello_host>();
    }
    return g_trello_host;
}

trello_host::trello_host() : model_(models::trello::get_trello_model()) {
    TRELLO_HOST_INFO("Trello host initialized");
    
    if (connect_from_environment()) {
        TRELLO_HOST_INFO("Successfully connected using environment variables");
    } else {
        TRELLO_HOST_DEBUG("No environment variables found or connection failed");
    }
}

bool trello_host::connect_from_environment() {
    clear_error();
    try {
        bool const result = model_->connect_from_environment();
        if (!result) {
            set_error("Failed to connect using environment variables");
        }
        return result;
    } catch (const std::exception& e) {
        set_error(std::format("Environment connection error: {}", e.what()));
        return false;
    }
}

bool trello_host::connect_with_credentials(const std::string& api_key, const std::string& token, const std::string& profile_name) {
    clear_error();
    try {
        models::trello::trello_connection_profile profile;
        profile.name = profile_name;
        profile.api_key = api_key;
        profile.token = token;
        profile.is_environment = false;
        
        bool const result = model_->connect(profile);
        if (!result) {
            set_error("Failed to connect with provided credentials");
        }
        return result;
    } catch (const std::exception& e) {
        set_error(std::format("Credential connection error: {}", e.what()));
        return false;
    }
}

bool trello_host::is_connected() const {
    return model_->is_connected();
}

void trello_host::disconnect() {
    clear_error();
    model_->disconnect();
    TRELLO_HOST_INFO("Disconnected from Trello");
}

std::vector<models::trello::trello_connection_profile> trello_host::get_saved_profiles() const {
    return model_->get_saved_profiles();
}

void trello_host::save_profile(const models::trello::trello_connection_profile& profile) {
    clear_error();
    try {
        model_->save_profile(profile);
    } catch (const std::exception& e) {
        set_error(std::format("Failed to save profile: {}", e.what()));
    }
}

void trello_host::delete_profile(const std::string& profile_name) {
    clear_error();
    try {
        model_->delete_profile(profile_name);
    } catch (const std::exception& e) {
        set_error(std::format("Failed to delete profile: {}", e.what()));
    }
}

std::string trello_host::get_current_profile_name() const {
    if (is_connected()) {
        return model_->get_current_profile().name;
    }
    return "";
}

std::future<std::vector<models::trello::trello_board>> trello_host::get_user_boards() {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return std::vector<models::trello::trello_board>{}; });
    }
    
    return model_->get_user_boards();
}

std::future<models::trello::trello_board> trello_host::get_board_details(const std::string& board_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return models::trello::trello_board{}; });
    }
    
    return model_->get_board(board_id, true, true);
}

std::future<bool> trello_host::create_board(const std::string& name, const std::string& description) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->create_board(name, description);
}

std::future<bool> trello_host::update_board(const std::string& board_id, const std::string& name, const std::string& description) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->update_board(board_id, name, description);
}

std::future<bool> trello_host::delete_board(const std::string& board_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->delete_board(board_id);
}

std::future<std::vector<models::trello::trello_list>> trello_host::get_board_lists(const std::string& board_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return std::vector<models::trello::trello_list>{}; });
    }
    
    return model_->get_board_lists(board_id);
}

std::future<models::trello::trello_list> trello_host::create_list(const std::string& board_id, const std::string& name) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return models::trello::trello_list{}; });
    }
    
    return model_->create_list(board_id, name);
}

std::future<bool> trello_host::update_list(const std::string& list_id, const std::string& name) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->update_list(list_id, name);
}

std::future<bool> trello_host::archive_list(const std::string& list_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->archive_list(list_id);
}

std::future<std::vector<models::trello::trello_card>> trello_host::get_board_cards(const std::string& board_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return std::vector<models::trello::trello_card>{}; });
    }
    
    return model_->get_board_cards(board_id);
}

std::future<std::vector<models::trello::trello_card>> trello_host::get_list_cards(const std::string& list_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return std::vector<models::trello::trello_card>{}; });
    }
    
    return model_->get_list_cards(list_id);
}

std::future<models::trello::trello_card> trello_host::get_card_details(const std::string& card_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return models::trello::trello_card{}; });
    }
    
    return model_->get_card(card_id);
}

std::future<models::trello::trello_card> trello_host::create_card(const std::string& list_id, const std::string& name, const std::string& description) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return models::trello::trello_card{}; });
    }
    
    return model_->create_card(list_id, name, description);
}

std::future<bool> trello_host::update_card(const std::string& card_id, const std::string& name, const std::string& description) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->update_card(card_id, name, description);
}

std::future<bool> trello_host::move_card(const std::string& card_id, const std::string& target_list_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->move_card(card_id, target_list_id);
}

std::future<bool> trello_host::delete_card(const std::string& card_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return false; });
    }
    
    return model_->delete_card(card_id);
}

std::future<std::vector<models::trello::trello_card>> trello_host::search_cards(const std::string& query, const std::string& board_id) {
    clear_error();
    if (!is_connected()) {
        set_error("Not connected to Trello");
        return std::async(std::launch::async, []() { return std::vector<models::trello::trello_card>{}; });
    }
    
    return model_->search_cards(query, board_id);
}

std::string trello_host::get_board_url(const std::string& board_id) {
    return std::format("https://trello.com/b/{}", board_id);
}

std::string trello_host::get_card_url(const std::string& card_id) {
    return std::format("https://trello.com/c/{}", card_id);
}

void trello_host::set_error(const std::string& error) {
    last_error_ = error;
    TRELLO_HOST_ERROR_FMT("Error: {}", error);
}

void trello_host::clear_error() {
    last_error_.clear();
}

} // namespace rouen::hosts
