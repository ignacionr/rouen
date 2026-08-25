#include "IconsMaterialDesign.h"
#include "trello_card.hpp"

#include <cstring>
#include <format>
#include "helpers/imgui_include.hpp"

namespace rouen::cards {

void trello_card::render_main_interface() {
    if (context_ == card_context::card_specific) {
        render_card_interface();
        return;
    }
    
    if (context_ == card_context::board_specific) {
        if (!initialized_) {
            fetch_boards();
            if (!initial_board_id_.empty()) {
                fetch_board_details();
            }
            initialized_ = true;
        }
        render_cards_tab();
        return;
    }
    
    bool const is_board_selected = !selected_board_id_.empty();
    if (!initialized_) {
        fetch_boards();
        if (is_board_selected) {
            fetch_board_details();
        } else if (context_ == card_context::card_specific && !initial_card_id_.empty()) {
            fetch_card_details();
        }
        initialized_ = true;
    }
    
    if (ImGui::BeginTabBar("TrelloTabs")) {
        if ((!is_board_selected) && ImGui::BeginTabItem(ICON_MD_DASHBOARD " Boards")) {
            active_tab_ = 0;
            render_boards_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_ASSIGNMENT " Cards")) {
            active_tab_ = 1;
            render_cards_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_ADD " Create")) {
            active_tab_ = 2;
            render_create_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_SETTINGS " Settings")) {
            active_tab_ = 3;
            render_settings_tab();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::Separator();
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 100);
    if (ImGui::Button("Logout", ImVec2(100, 0))) {
        trello_host_->disconnect();
        reset_ui_state();
    }
}

void trello_card::render_boards_tab() {
    if (ImGui::Button("Refresh Boards", ImVec2(120, 0))) {
        fetch_boards();
    }
    
    if (loading_boards_) {
        ImGui::SameLine();
        ImGui::TextColored(colors[1], "Loading...");
    }
    
    ImGui::Separator();
    render_boards_list();
}

void trello_card::render_cards_tab() {
    if (context_ != card_context::board_specific) {
        render_board_selector();
    }
    
    if (!selected_board_id_.empty()) {
        if (context_ != card_context::board_specific) {
            ImGui::Separator();
        }
        
        if (loading_board_details_) {
            ImGui::TextColored(colors[1], "Loading board details...");
        } else if (!current_board_.id.empty()) {
            render_board_overview();
            ImGui::Separator();
            render_lists_and_cards();
        } else {
            ImGui::TextColored(colors[3], "Failed to load board details");
            if (ImGui::Button("Retry")) {
                fetch_board_details();
            }
        }
    } else {
        if (context_ == card_context::board_specific) {
            ImGui::TextColored(colors[2], "Error: No board ID specified for board-specific card");
        } else {
            ImGui::TextColored(colors[3], "Select a board to view cards");
        }
    }
}

void trello_card::render_create_tab() {
    ImGui::TextColored(colors[0], "Create New Card");
    ImGui::Separator();
    
    render_board_selector();
    
    if (!selected_board_id_.empty() && !current_board_.lists.empty()) {
        ImGui::Separator();
        
        ImGui::Text("Select List:");
        if (ImGui::BeginCombo("##list_selector", selected_list_id_.empty() ? "Choose a list..." : "Selected")) {
            for (const auto& list : current_board_.lists) {
                if (!list.closed) {
                    bool const is_selected = (selected_list_id_ == list.id);
                    if (ImGui::Selectable(list.name.c_str(), is_selected)) {
                        selected_list_id_ = list.id;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Separator();
        ImGui::Text("Card Name:");
        ImGui::InputText("##card_name", new_card_name_, sizeof(new_card_name_));
        
        ImGui::Text("Description (optional):");
        ImGui::InputTextMultiline("##card_desc", new_card_desc_, sizeof(new_card_desc_), ImVec2(-1, 100));
        
        ImGui::Separator();
        bool const can_create = !selected_list_id_.empty() && strlen(new_card_name_) > 0 && !creating_card_;
        
        if (!can_create) {
            ImGui::BeginDisabled();
        }
        
        if (ImGui::Button("Create Card", ImVec2(120, 0))) {
            create_new_card();
        }
        
        if (!can_create) {
            ImGui::EndDisabled();
        }
        
        if (creating_card_) {
            ImGui::SameLine();
            ImGui::TextColored(colors[1], "Creating...");
        }
        
    } else {
        ImGui::TextColored(colors[3], "Select a board to create cards");
    }
}

void trello_card::render_settings_tab() {
    ImGui::TextColored(colors[0], "Trello Settings");
    ImGui::Separator();
    
    ImGui::Text("Current Profile: %s", trello_host_->get_current_profile_name().c_str());
    
    ImGui::Separator();
    render_search_section();
    
    ImGui::Separator();
    
    if (ImGui::Button("Manage Profiles", ImVec2(150, 0))) {
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Test Connection", ImVec2(150, 0))) {
        fetch_boards();
    }
}

void trello_card::render_search_section() {
    ImGui::TextColored(colors[0], "Search Cards");
    
    ImGui::InputText("##search", search_query_, sizeof(search_query_));
    ImGui::SameLine();
    
    bool const can_search = strlen(search_query_) > 0 && !searching_;
    if (!can_search) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Search", ImVec2(80, 0))) {
        perform_search();
    }
    
    if (!can_search) {
        ImGui::EndDisabled();
    }
    
    if (searching_) {
        ImGui::SameLine();
        ImGui::TextColored(colors[1], "Searching...");
    }
    
    render_search_results();
}

void trello_card::render_search_results() {
    if (search_results_.empty()) {
        return;
    }
    
    ImGui::TextColored(colors[0], "Search Results (%zu cards)", search_results_.size());
    
    if (ImGui::BeginTable("SearchResults", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Card", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Board", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        
        for (const auto& search_result : search_results_) {
            ImGui::TableNextRow();
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", search_result.name.c_str());
            if (!search_result.desc.empty()) {
                ImGui::TextColored(colors[5], "%.50s%s", search_result.desc.c_str(), 
                                 search_result.desc.length() > 50 ? "..." : "");
            }
            
            ImGui::TableNextColumn();
            ImGui::TextColored(colors[3], "ID: %s", search_result.idBoard.c_str());
            
            ImGui::TableNextColumn();
            ImGui::PushID(search_result.id.c_str());
            if (ImGui::Button("Open", ImVec2(70, 0))) {
                create_card_tab(search_result.id);
            }
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void trello_card::perform_search() {
    if (strlen(search_query_) == 0) return;
    
    searching_ = true;
    search_future_ = trello_host_->search_cards(search_query_, selected_board_id_);
}

} // namespace rouen::cards
