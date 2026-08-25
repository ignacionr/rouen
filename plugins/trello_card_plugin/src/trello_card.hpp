#pragma once

#include <array>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

#include <rouen_plugin_api.hpp>
#include "helpers/imgui_include.hpp"
#include "IconsMaterialDesign.h"
#include "trello_host.hpp"
#include "trello_model.hpp"

namespace rouen::cards {

class trello_card : public rouen::plugin::plugin_card {
public:
    enum class card_context { general, board_specific, card_specific };
    
    trello_card();
    explicit trello_card(const std::string& board_id);
    explicit trello_card(const std::string& entity_id, card_context context);
    ~trello_card() override = default;

    void draw() override;
    [[nodiscard]] std::string title() const override;
    [[nodiscard]] std::string uri() const override;
    void handle_uri(std::string_view locator) override;

    using color_array = std::array<ImVec4, 16>;
    color_array colors;

private:
    std::shared_ptr<hosts::trello_host> trello_host_;
    
    bool initialized_ = false;
    int active_tab_ = 0;
    card_context context_ = card_context::general;
    std::string initial_board_id_;
    std::string initial_card_id_;
    
    char api_key_buffer_[256] = {0};
    char token_buffer_[512] = {0};
    char profile_name_buffer_[128] = {0};
    std::string connection_error_;
    
    std::optional<std::future<std::vector<models::trello::trello_board>>> boards_future_;
    std::vector<models::trello::trello_board> boards_;
    bool loading_boards_ = false;
    
    std::string selected_board_id_;
    std::string selected_board_name_;
    std::optional<std::future<models::trello::trello_board>> board_details_future_;
    models::trello::trello_board current_board_;
    bool loading_board_details_ = false;
    
    char new_card_name_[256] = {0};
    char new_card_desc_[1024] = {0};
    std::string selected_list_id_;
    bool creating_card_ = false;
    std::optional<std::future<models::trello::trello_card>> create_card_future_;
    
    char search_query_[256] = {0};
    std::vector<models::trello::trello_card> search_results_;
    bool searching_ = false;
    std::optional<std::future<std::vector<models::trello::trello_card>>> search_future_;
    
    models::trello::trello_card current_card_;
    models::trello::trello_board parent_board_;
    models::trello::trello_list parent_list_;
    bool loading_card_details_ = false;
    std::optional<std::future<models::trello::trello_card>> card_details_future_;
    
    char edit_card_name_[256] = {0};
    char edit_card_desc_[2048] = {0};
    bool editing_card_ = false;
    bool updating_card_ = false;
    std::optional<std::future<bool>> update_card_future_;
    std::string target_list_id_;
    bool moving_card_ = false;
    std::optional<std::future<bool>> move_card_future_;
    
    void render_connection_screen();
    void render_main_interface();
    void render_boards_tab();
    void render_cards_tab();
    void render_create_tab();
    void render_settings_tab();
    
    void render_card_interface();
    void render_card_details_tab();
    void render_card_edit_tab();
    void render_card_activity_tab();
    void render_card_overview();
    void render_card_edit_form();
    void render_card_actions();
    void render_card_metadata() const;
    
    void render_boards_list();
    void render_board_selector();
    void fetch_boards();
    void select_board(const std::string& board_id, const std::string& board_name);
    void fetch_board_details();
    
    void render_board_overview();
    void render_lists_and_cards();
    void render_card_item(const models::trello::trello_card& trello_card_item, const models::trello::trello_list* list);
    void create_new_card();
    
    void fetch_card_details();
    void update_card();
    void move_card_to_list();
    static void archive_card();
    static void delete_card();
    void populate_edit_form();
    void reset_edit_form();
    
    void render_search_section();
    void perform_search();
    void render_search_results();
    
    void render_connection_form();
    void render_saved_profiles();
    
    void check_async_operations();
    void reset_ui_state();
    
    static ImVec4 get_label_color(const std::string& color_name);
    static std::string format_due_date(const std::string& due_date);
    static void open_in_browser(const std::string& url);
    void show_error(const std::string& error);
    void clear_error();
    static void create_card_tab(const std::string& card_id);
};

} // namespace rouen::cards
