#include "trello_card.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <future>
#include <utility>

namespace rouen::cards {

void trello_card::check_async_operations() {
    if (boards_future_.has_value() && 
        boards_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        boards_ = boards_future_->get();
        boards_future_.reset();
        loading_boards_ = false;
        
        if (!selected_board_id_.empty() && selected_board_name_.empty()) {
            auto it = std::find_if(boards_.begin(), boards_.end(), 
                                   [this](const auto& board) { return board.id == selected_board_id_; });
            if (it != boards_.end()) {
                selected_board_name_ = it->name;
            }
        }
    }
    
    if (board_details_future_.has_value() && 
        board_details_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto board_result = board_details_future_->get();
        board_details_future_.reset();
        loading_board_details_ = false;
        
        if (context_ == card_context::card_specific) {
            parent_board_ = std::move(board_result);
            
            for (const auto& list : parent_board_.lists) {
                if (list.id == current_card_.idList) {
                    parent_list_ = list;
                    colors[0] = get_label_color(list.color.value_or("blue"));
                    break;
                }
            }
            
            connection_error_ = std::format("Board loaded: {} lists", parent_board_.lists.size());
        } else {
            current_board_ = std::move(board_result);
            
            if (!current_board_.id.empty() && selected_board_name_.empty()) {
                selected_board_name_ = current_board_.name;
            }
            colors[0] = ImVec4(0.5f, 0.5f, 1.0f, 1.0f);
        }
    }
    
    if (create_card_future_.has_value() && 
        create_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto new_card = create_card_future_->get();
        create_card_future_.reset();
        creating_card_ = false;
        
        if (!new_card.id.empty()) {
            memset(new_card_name_, 0, sizeof(new_card_name_));
            memset(new_card_desc_, 0, sizeof(new_card_desc_));
            fetch_board_details();
        }
    }
    
    if (search_future_.has_value() && 
        search_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        search_results_ = search_future_->get();
        search_future_.reset();
        searching_ = false;
    }
    
    if (card_details_future_.has_value() &&
        card_details_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = card_details_future_->get();
            current_card_ = std::move(result);
            loading_card_details_ = false;
            
            if (!current_card_.idBoard.empty()) {
                loading_board_details_ = true;
                board_details_future_ = trello_host_->get_board_details(current_card_.idBoard);
            }
        } catch (const std::exception& e) {
            loading_card_details_ = false;
            connection_error_ = "Error loading card: " + std::string(e.what());
        }
        card_details_future_.reset();
    }
    
    if (update_card_future_.has_value() &&
        update_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = update_card_future_->get();
            if (result) {
                fetch_card_details();
                updating_card_ = false;
                editing_card_ = false;
            } else {
                updating_card_ = false;
                connection_error_ = "Failed to update card";
            }
        } catch (const std::exception& e) {
            updating_card_ = false;
            connection_error_ = "Error updating card: " + std::string(e.what());
        }
        update_card_future_.reset();
    }
    
    if (move_card_future_.has_value() &&
        move_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = move_card_future_->get();
            if (result) {
                fetch_card_details();
                moving_card_ = false;
                target_list_id_.clear();
            } else {
                moving_card_ = false;
                connection_error_ = "Failed to move card";
            }
        } catch (const std::exception& e) {
            moving_card_ = false;
            connection_error_ = "Error moving card: " + std::string(e.what());
        }
        move_card_future_.reset();
    }
}

void trello_card::reset_ui_state() {
    initialized_ = false;
    active_tab_ = 0;
    boards_.clear();
    selected_board_id_.clear();
    selected_board_name_.clear();
    current_board_ = {};
    search_results_.clear();
    
    current_card_ = {};
    parent_board_ = {};
    parent_list_ = {};
    editing_card_ = false;
    loading_card_details_ = false;
    updating_card_ = false;
    moving_card_ = false;
    target_list_id_.clear();
    
    boards_future_.reset();
    board_details_future_.reset();
    create_card_future_.reset();
    search_future_.reset();
    card_details_future_.reset();
    update_card_future_.reset();
    move_card_future_.reset();
    
    loading_boards_ = false;
    loading_board_details_ = false;
    creating_card_ = false;
    searching_ = false;
}

} // namespace rouen::cards
