#include "IconsMaterialDesign.h"
#include "models/trello_model.hpp"
#include "trello_card.hpp"

// 1. Standard includes in alphabetic order
#include <format>
#include <imgui.h>

// 2. Libraries used in the project, in alphabetic order
// None

// 3. All other includes
#include "../../registrar.hpp"

namespace rouen::cards {

void trello_card::render_boards_list() {
    if (boards_.empty()) {
        if (!loading_boards_) {
            ImGui::TextColored(colors[3], "No boards found. Click 'Refresh Boards' to try again.");
        }
        return;
    }
    
    if (ImGui::BeginTable("BoardsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();
        
        for (const auto& board : boards_) {
            // Skip closed boards
            if (board.closed) continue;
            
            ImGui::TableNextRow();
            
            // Name column
            ImGui::TableNextColumn();
            
            // Make board name clickable to open in new card
            ImGui::PushID(board.id.c_str());
            if (ImGui::Selectable(std::format("{} ({} - {})", board.name, board.lists.size(), board.cards.size()).c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                // Create new Trello card with this board ID
                "create_card"_sfn("trello-board:" + board.id);
            }
            ImGui::PopID();
            
            if (!board.desc.empty()) {
                ImGui::TextColored(colors[5], "%s", board.desc.c_str());
            }
                        
            // Actions
            ImGui::TableNextColumn();
            ImGui::PushID(board.id.c_str());
            if (ImGui::Button("View", ImVec2(45, 0))) {
                select_board(board.id, board.name);
                active_tab_ = 1; // Switch to Cards tab
            }
            ImGui::SameLine();
            if (ImGui::Button("Open", ImVec2(45, 0))) {
                open_in_browser(board.url);
            }
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void trello_card::render_board_selector() {
    ImGui::Text("Board:");
    if (ImGui::BeginCombo("##board_selector", selected_board_name_.empty() ? "Select a board..." : selected_board_name_.c_str())) {
        for (const auto& board : boards_) {
            // Skip closed boards
            if (board.closed) continue;
            
            bool is_selected = (selected_board_id_ == board.id);
            if (ImGui::Selectable(board.name.c_str(), is_selected)) {
                select_board(board.id, board.name);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void trello_card::render_board_overview() {
    ImGui::TextColored(colors[0], "%s", current_board_.name.c_str());
    if (!current_board_.desc.empty()) {
        ImGui::TextWrapped("%s", current_board_.desc.c_str());
    }
    
    // Quick stats
    ImGui::Text("Lists: %zu | Cards: %zu | Members: %zu", 
                current_board_.lists.size(), 
                current_board_.cards.size(),
                current_board_.members.size());
    
    if (ImGui::Button("Open in Browser", ImVec2(150, 0))) {
        open_in_browser(current_board_.url);
    }
}

void trello_card::render_lists_and_cards() {
    for (const auto& list : current_board_.lists) {
        if (list.closed) continue;
        
        auto list_color = get_label_color(list.color.value_or("blue"));
        ImGui::PushStyleColor(ImGuiCol_Header, list_color);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, list_color);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, list_color);
        if (ImGui::CollapsingHeader(list.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            auto cards_in_list = current_board_.get_cards_for_list(list.id);
            
            if (cards_in_list.empty()) {
                ImGui::TextColored(colors[3], "  No cards in this list");
            } else {
                for (const auto& trello_card_item : cards_in_list) {
                    render_card_item(trello_card_item, &list);
                }
            }
        }
        ImGui::PopStyleColor(3);  // Pop colors for header
    }
}

void trello_card::render_card_item(const models::trello::trello_card& trello_card_item, const models::trello::trello_list* /* list */) {
    ImGui::PushID(trello_card_item.id.c_str());
    
    // Card name with clickable link
    if (ImGui::Selectable(trello_card_item.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) {
            open_in_browser(trello_card_item.url);
        } else {
            // Single click - open card in new UI card
            create_card_tab(trello_card_item.id);
        }
    }
    
    // Show card details on hover
    if (ImGui::IsItemHovered() && !trello_card_item.desc.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s", trello_card_item.desc.c_str());
        ImGui::EndTooltip();
    }
    
    // Card badges and info
    if (trello_card_item.badges_comments() > 0 || trello_card_item.badges_attachments() > 0 || (trello_card_item.due.has_value() && !trello_card_item.due->empty())) {
        ImGui::SameLine();
        ImGui::Text("(");
        
        bool need_separator = false;
        
        if (trello_card_item.badges_comments() > 0) {
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "%d" ICON_MD_COMMENT, trello_card_item.badges_comments());
            need_separator = true;
        }
        
        if (trello_card_item.badges_attachments() > 0) {
            if (need_separator) { ImGui::SameLine(); ImGui::Text("|"); }
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "%d" ICON_MD_ATTACHMENT, trello_card_item.badges_attachments());
            need_separator = true;
        }
        
        if (trello_card_item.due.has_value() && !trello_card_item.due->empty()) {
            if (need_separator) { ImGui::SameLine(); ImGui::Text("|"); }
            ImGui::SameLine();
            ImGui::TextColored(trello_card_item.dueComplete ? colors[6] : colors[2], ICON_MD_SCHEDULE);
        }
        
        ImGui::SameLine();
        ImGui::Text(")");
    }
    
    ImGui::PopID();
}

// Board management implementation methods
void trello_card::fetch_boards() {
    loading_boards_ = true;
    boards_future_ = trello_host_->get_user_boards();
}

void trello_card::select_board(const std::string& board_id, const std::string& board_name) {
    selected_board_id_ = board_id;
    selected_board_name_ = board_name;
    fetch_board_details();
}

void trello_card::fetch_board_details() {
    if (selected_board_id_.empty()) return;
    
    loading_board_details_ = true;
    board_details_future_ = trello_host_->get_board_details(selected_board_id_);
}

} // namespace rouen::cards
