#include "trello_card.hpp"

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <chrono>
#include <format>

// 2. Libraries used in the project, in alphabetic order
// None

// 3. All other includes
#include "../../helpers/api_keys.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

void trello_card::check_async_operations() {
    // Check boards fetch
    if (boards_future_.has_value() && 
        boards_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        boards_ = boards_future_->get();
        boards_future_.reset();
        loading_boards_ = false;
        
        // If we have a selected_board_id_ but no name yet (from initial_board_id_), find and set the name
        if (!selected_board_id_.empty() && selected_board_name_.empty()) {
            auto it = std::find_if(boards_.begin(), boards_.end(), 
                                   [this](const auto& board) { return board.id == selected_board_id_; });
            if (it != boards_.end()) {
                selected_board_name_ = it->name;
            }
        }
    }
    
    // Check board details fetch
    if (board_details_future_.has_value() && 
        board_details_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto board_result = board_details_future_->get();
        board_details_future_.reset();
        loading_board_details_ = false;
        
        // Handle different contexts
        if (context_ == card_context::card_specific) {
            // For card context, store as parent_board_ for move functionality
            parent_board_ = std::move(board_result);
            
            // Find the current list name
            for (const auto& list : parent_board_.lists) {
                if (list.id == current_card_.idList) {
                    parent_list_ = list;
                    colors[0] = get_label_color(list.color.value_or("blue"));
                    break;
                }
            }
            
            // Debug: Log successful board loading
            connection_error_ = std::format("Board loaded: {} lists", parent_board_.lists.size());
        } else {
            // For general board context, store as current_board_
            current_board_ = std::move(board_result);
            
            // If this was a direct board fetch (like for a specific board card), set the board name
            if (!current_board_.id.empty() && selected_board_name_.empty()) {
                selected_board_name_ = current_board_.name;
            }
            name(std::format("Trello - Board: {}", selected_board_name_));
            colors[0] = ImVec4(0.5f, 0.5f, 1.0f, 1.0f); // Update primary color
        }
    }
    
    // Check card creation
    if (create_card_future_.has_value() && 
        create_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto new_card = create_card_future_->get();
        create_card_future_.reset();
        creating_card_ = false;
        
        if (!new_card.id.empty()) {
            // Clear form and refresh board
            memset(new_card_name_, 0, sizeof(new_card_name_));
            memset(new_card_desc_, 0, sizeof(new_card_desc_));
            fetch_board_details(); // Refresh to show new card
        }
    }
    
    // Check search
    if (search_future_.has_value() && 
        search_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        search_results_ = search_future_->get();
        search_future_.reset();
        searching_ = false;
    }
    
    // Check card details future (for card context)
    if (card_details_future_.has_value() &&
        card_details_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = card_details_future_->get();
            current_card_ = std::move(result);
            loading_card_details_ = false;
            
            // Update window title with card name instead of ID
            if (!current_card_.name.empty()) {
                name(std::format("Trello - Card: {}", current_card_.name));
            }
            // Always fetch board details to get all lists for the move functionality
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
    
    // Check update card future
    if (update_card_future_.has_value() &&
        update_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = update_card_future_->get();
            if (result) {
                // Refresh card details after successful update
                fetch_card_details();
                updating_card_ = false;
                editing_card_ = false;  // Exit edit mode
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
    
    // Check move card future
    if (move_card_future_.has_value() &&
        move_card_future_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = move_card_future_->get();
            if (result) {
                // Refresh card details after successful move
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
    
    // Reset card-specific state
    current_card_ = {};
    parent_board_ = {};
    parent_list_ = {};
    editing_card_ = false;
    loading_card_details_ = false;
    updating_card_ = false;
    moving_card_ = false;
    target_list_id_.clear();
    
    // Reset async operations
    boards_future_.reset();
    board_details_future_.reset();
    create_card_future_.reset();
    search_future_.reset();
    card_details_future_.reset();
    update_card_future_.reset();
    move_card_future_.reset();
    
    // Reset loading states
    loading_boards_ = false;
    loading_board_details_ = false;
    creating_card_ = false;
    searching_ = false;
}

} // namespace rouen::cards
