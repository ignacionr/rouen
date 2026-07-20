#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <string>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
#include "../models/trello_model.hpp"

namespace rouen::hosts {

/**
 * Trello API Host Controller
 * 
 * This class provides a high-level interface for interacting with Trello API
 * through the trello_model. It serves as an abstraction layer between UI components
 * and the underlying model, allowing other parts of the program to easily interact
 * with Trello boards and cards.
 */
class trello_host {
public:
    trello_host();
    ~trello_host() = default;

    // Connection management
    bool connect_from_environment();
    bool connect_with_credentials(const std::string& api_key, const std::string& token, const std::string& profile_name = "Manual");
    bool is_connected() const;
    void disconnect();
    
    // Profile management
    std::vector<models::trello::trello_connection_profile> get_saved_profiles() const;
    void save_profile(const models::trello::trello_connection_profile& profile);
    void delete_profile(const std::string& profile_name);
    std::string get_current_profile_name() const;
    
    // Board operations
    std::future<std::vector<models::trello::trello_board>> get_user_boards();
    std::future<models::trello::trello_board> get_board_details(const std::string& board_id);
    std::future<bool> create_board(const std::string& name, const std::string& description = "");
    std::future<bool> update_board(const std::string& board_id, const std::string& name, const std::string& description = "");
    std::future<bool> delete_board(const std::string& board_id);
    
    // List operations
    std::future<std::vector<models::trello::trello_list>> get_board_lists(const std::string& board_id);
    std::future<models::trello::trello_list> create_list(const std::string& board_id, const std::string& name);
    std::future<bool> update_list(const std::string& list_id, const std::string& name);
    std::future<bool> archive_list(const std::string& list_id);
    
    // Card operations
    std::future<std::vector<models::trello::trello_card>> get_board_cards(const std::string& board_id);
    std::future<std::vector<models::trello::trello_card>> get_list_cards(const std::string& list_id);
    std::future<models::trello::trello_card> get_card_details(const std::string& card_id);
    std::future<models::trello::trello_card> create_card(const std::string& list_id, const std::string& name, const std::string& description = "");
    std::future<bool> update_card(const std::string& card_id, const std::string& name, const std::string& description = "");
    std::future<bool> move_card(const std::string& card_id, const std::string& target_list_id);
    std::future<bool> delete_card(const std::string& card_id);
    
    // Search and query operations
    std::future<std::vector<models::trello::trello_card>> search_cards(const std::string& query, const std::string& board_id = "");
    
    // Utility methods for other parts of the program
    static std::string get_board_url(const std::string& board_id) ;
    static std::string get_card_url(const std::string& card_id) ;
    
    // Error handling
    std::string get_last_error() const { return last_error_; }
    void clear_last_error() { last_error_.clear(); }

private:
    std::shared_ptr<models::trello::trello_model> model_;
    std::string last_error_;
    
    // Helper methods
    void set_error(const std::string& error);
    void clear_error();
};

// Global instance accessor for easy access from other parts of the program
std::shared_ptr<trello_host> get_trello_host();

} // namespace rouen::hosts
