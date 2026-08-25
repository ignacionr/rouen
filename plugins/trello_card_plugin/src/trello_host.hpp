#pragma once

#include <memory>
#include <string>
#include <future>
#include <vector>

#include "trello_model.hpp"

namespace rouen::hosts {

class trello_host {
public:
    trello_host();
    ~trello_host() = default;

    bool connect_from_environment();
    bool connect_with_credentials(const std::string& api_key, const std::string& token, const std::string& profile_name = "Manual");
    bool is_connected() const;
    void disconnect();
    
    std::vector<models::trello::trello_connection_profile> get_saved_profiles() const;
    void save_profile(const models::trello::trello_connection_profile& profile);
    void delete_profile(const std::string& profile_name);
    std::string get_current_profile_name() const;
    
    std::future<std::vector<models::trello::trello_board>> get_user_boards();
    std::future<models::trello::trello_board> get_board_details(const std::string& board_id);
    std::future<bool> create_board(const std::string& name, const std::string& description = "");
    std::future<bool> update_board(const std::string& board_id, const std::string& name, const std::string& description = "");
    std::future<bool> delete_board(const std::string& board_id);
    
    std::future<std::vector<models::trello::trello_list>> get_board_lists(const std::string& board_id);
    std::future<models::trello::trello_list> create_list(const std::string& board_id, const std::string& name);
    std::future<bool> update_list(const std::string& list_id, const std::string& name);
    std::future<bool> archive_list(const std::string& list_id);
    
    std::future<std::vector<models::trello::trello_card>> get_board_cards(const std::string& board_id);
    std::future<std::vector<models::trello::trello_card>> get_list_cards(const std::string& list_id);
    std::future<models::trello::trello_card> get_card_details(const std::string& card_id);
    std::future<models::trello::trello_card> create_card(const std::string& list_id, const std::string& name, const std::string& description = "");
    std::future<bool> update_card(const std::string& card_id, const std::string& name, const std::string& description = "");
    std::future<bool> move_card(const std::string& card_id, const std::string& target_list_id);
    std::future<bool> delete_card(const std::string& card_id);
    
    std::future<std::vector<models::trello::trello_card>> search_cards(const std::string& query, const std::string& board_id = "");
    
    static std::string get_board_url(const std::string& board_id);
    static std::string get_card_url(const std::string& card_id);
    
    std::string get_last_error() const { return last_error_; }
    void clear_last_error() { last_error_.clear(); }

private:
    std::shared_ptr<models::trello::trello_model> model_;
    std::string last_error_;
    
    void set_error(const std::string& error);
    void clear_error();
};

std::shared_ptr<trello_host> get_trello_host();

} // namespace rouen::hosts
