#include "IconsMaterialDesign.h"
#include "trello_model.hpp"
#include "trello_card.hpp"

#include <format>
#include <thread>
#include "helpers/imgui_include.hpp"
#include "helpers/fetch.hpp"
#include "registrar.hpp"

namespace rouen::cards {

inline void create_card_uri_local(const std::string& uri) {
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
            if (board.closed) continue;
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            
            ImGui::PushID(board.id.c_str());
            if (ImGui::Selectable(std::format("{} ({} - {})", board.name, board.lists.size(), board.cards.size()).c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                create_card_uri_local("trello-board:" + board.id);
            }
            ImGui::PopID();
            
            if (!board.desc.empty()) {
                ImGui::TextColored(colors[5], "%s", board.desc.c_str());
            }
                        
            ImGui::TableNextColumn();
            ImGui::PushID(board.id.c_str());
            if (ImGui::Button("View", ImVec2(45, 0))) {
                select_board(board.id, board.name);
                active_tab_ = 1;
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
            if (board.closed) continue;
            
            bool const is_selected = (selected_board_id_ == board.id);
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
        ImGui::PopStyleColor(3);
    }
}

void trello_card::render_card_item(const models::trello::trello_card& trello_card_item, const models::trello::trello_list* /* list */) {
    ImGui::PushID(trello_card_item.id.c_str());
    
    if (ImGui::Selectable(trello_card_item.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) {
            open_in_browser(trello_card_item.url);
        } else {
            create_card_tab(trello_card_item.id);
        }
    }
    
    if (ImGui::IsItemHovered() && !trello_card_item.desc.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s", trello_card_item.desc.c_str());
        ImGui::EndTooltip();
    }
    
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
