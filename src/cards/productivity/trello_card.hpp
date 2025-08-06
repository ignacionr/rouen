#pragma once

// 1. Standard includes in alphabetic order
#include <array>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"

// 3. All other includes
#include "../interface/card.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../../hosts/trello_host.hpp"
#include "../../models/trello_model.hpp"

namespace rouen::cards {

class trello_card : public card {
public:
    trello_card();
    explicit trello_card(const std::string& board_id);  // Constructor for opening specific board
    ~trello_card() override = default;

    bool render() override;
    std::string get_uri() const override { 
        return initial_board_id_.empty() ? "trello" : "trello-board:" + initial_board_id_; 
    }

private:
    // Core components
    std::shared_ptr<hosts::trello_host> trello_host_;
    
    // UI state
    bool initialized_ = false;
    int active_tab_ = 0;  // 0=Boards, 1=Cards, 2=Create, 3=Settings
    std::string initial_board_id_;  // Board ID to open automatically if specified
    
    // Connection state
    char api_key_buffer_[256] = {0};
    char token_buffer_[512] = {0};
    char profile_name_buffer_[128] = {0};
    std::string connection_error_;
    
    // Board data
    std::optional<std::future<std::vector<models::trello::trello_board>>> boards_future_;
    std::vector<models::trello::trello_board> boards_;
    bool loading_boards_ = false;
    
    // Selected board state
    std::string selected_board_id_;
    std::string selected_board_name_;
    std::optional<std::future<models::trello::trello_board>> board_details_future_;
    models::trello::trello_board current_board_;
    bool loading_board_details_ = false;
    
    // Card operations
    char new_card_name_[256] = {0};
    char new_card_desc_[1024] = {0};
    std::string selected_list_id_;
    bool creating_card_ = false;
    std::optional<std::future<models::trello::trello_card>> create_card_future_;
    
    // Search functionality
    char search_query_[256] = {0};
    std::vector<models::trello::trello_card> search_results_;
    bool searching_ = false;
    std::optional<std::future<std::vector<models::trello::trello_card>>> search_future_;
    
    // Rendering methods
    void render_connection_screen();
    void render_main_interface();
    void render_boards_tab();
    void render_cards_tab();
    void render_create_tab();
    void render_settings_tab();
    
    // Board management
    void render_boards_list();
    void render_board_selector();
    void fetch_boards();
    void select_board(const std::string& board_id, const std::string& board_name);
    void fetch_board_details();
    
    // Card management
    void render_board_overview();
    void render_lists_and_cards();
    void render_card_item(const models::trello::trello_card& card, const models::trello::trello_list* list);
    void create_new_card();
    
    // Search functionality
    void render_search_section();
    void perform_search();
    void render_search_results();
    
    // Connection management
    void render_connection_form();
    void render_saved_profiles();
    void attempt_connection();
    void try_environment_connection();
    bool validate_connection_form();
    
    // Utility methods
    void check_async_operations();
    void reset_ui_state();
    ImVec4 get_label_color(const std::string& color_name) const;
    std::string format_due_date(const std::string& due_date) const;
    void open_in_browser(const std::string& url) const;
    
    // Error handling
    void show_error(const std::string& error);
    void clear_error();
};

} // namespace rouen::cards
