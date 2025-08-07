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

trello_card::trello_card() : trello_host_(hosts::get_trello_host()) {
    name("Trello");
    
    // Try to load existing credentials into form
    auto api_key = helpers::ApiKeys::get_trello_api_key();
    auto token = helpers::ApiKeys::get_trello_token();
    
    if (!api_key.empty() && api_key.length() < sizeof(api_key_buffer_)) {
        std::strcpy(api_key_buffer_, api_key.c_str());
    }
    if (!token.empty() && token.length() < sizeof(token_buffer_)) {
        std::strcpy(token_buffer_, token.c_str());
    }
}

trello_card::trello_card(const std::string& board_id) : trello_card() {
    context_ = card_context::board_specific;
    initial_board_id_ = board_id;
    selected_board_id_ = board_id;  // Pre-select the board
    name("Trello - Board");

    colors[0] = ImVec4(0.0f, 0.5f, 1.0f, 1.0f); // Primary color
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Secondary color
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Error color
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Success color
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Warning color
}

trello_card::trello_card(const std::string& entity_id, card_context context) 
    : card(), trello_host_(hosts::get_trello_host()), context_(context) {
    name("Trello");
    
    // Try to load existing credentials into form
    auto api_key = helpers::ApiKeys::get_trello_api_key();
    auto token = helpers::ApiKeys::get_trello_token();
    
    if (!api_key.empty() && api_key.length() < sizeof(api_key_buffer_)) {
        std::strcpy(api_key_buffer_, api_key.c_str());
    }
    if (!token.empty() && token.length() < sizeof(token_buffer_)) {
        std::strcpy(token_buffer_, token.c_str());
    }
    
    switch (context) {
        case card_context::general:
            // Standard trello: behavior
            break;
        case card_context::board_specific:
            initial_board_id_ = entity_id;
            selected_board_id_ = entity_id;  // Pre-select the board
            name("Trello - Board");
            break;
        case card_context::card_specific:
            initial_card_id_ = entity_id;
            name(std::format("Trello - Card: {}", entity_id));
            break;
    }
    colors[0] = ImVec4(0.0f, 0.7f, 1.0f, 1.0f); // Blue primary color
    colors[1] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Secondary color
    colors[2] = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Error color
    colors[3] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Success color
    colors[4] = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Warning color
}

std::string trello_card::get_uri() const {
    switch (context_) {
        case card_context::general:
            return "trello";
        case card_context::board_specific:
            return "trello-board:" + initial_board_id_;
        case card_context::card_specific:
            return "trello-card:" + initial_card_id_;
    }
    // This should never be reached, but required for some compilers
    return "trello";
}

bool trello_card::render() {
    return render_window([this]() {
        if (!trello_host_->is_connected()) {
            render_connection_screen();
        } else {
            render_main_interface();
        }
        
        // Check for completed async operations
        check_async_operations();
    });
}

void trello_card::render_connection_screen() {
    ImGui::TextColored(colors[0], ICON_MD_DASHBOARD " Connect to Trello");
    ImGui::Separator();
    
    if (!connection_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[2]); // Error color
        ImGui::TextWrapped("%s", connection_error_.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    
    // Try environment connection first
    if (ImGui::Button("Connect from Environment", ImVec2(200, 0))) {
        try_environment_connection();
    }
    ImGui::SameLine();
    ImGui::TextColored(colors[5], "Uses TRELLO_API_KEY and TRELLO_TOKEN");
    
    ImGui::Separator();
    
    render_saved_profiles();
    ImGui::Separator();
    render_connection_form();
}

void trello_card::render_main_interface() {
    // Handle different contexts
    if (context_ == card_context::card_specific) {
        render_card_interface();
        return;
    }
    
    // Board specific context - show cards for this board
    if (context_ == card_context::board_specific) {
        // Initialize board data if needed
        if (!initialized_) {
            fetch_boards();  // Need this for board names
            if (!initial_board_id_.empty()) {
                fetch_board_details();
            }
            initialized_ = true;
        }
        render_cards_tab();
        return;
    }
    
    // General context - show full interface with tabs
    bool is_board_selected = !selected_board_id_.empty();
    if (!initialized_) {
        fetch_boards();  // Need this for the board selector to work
        if (is_board_selected) {
            // If we have a specific board ID, fetch both boards list and board details
            fetch_board_details();
            // use the board as a name
            name(std::format("Trello - Board: {}", current_board_.name));
        } else if (context_ == card_context::card_specific && !initial_card_id_.empty()) {
            // If we have a specific card ID, fetch card details
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
    
    // Logout button
    ImGui::Separator();
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 100);
    if (ImGui::Button("Logout", ImVec2(100, 0))) {
        trello_host_->disconnect();
        reset_ui_state();
    }
}

void trello_card::render_boards_tab() {
    // Refresh button
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
    // For board-specific context, skip the board selector
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
        
        // List selection
        ImGui::Text("Select List:");
        if (ImGui::BeginCombo("##list_selector", selected_list_id_.empty() ? "Choose a list..." : "Selected")) {
            for (const auto& list : current_board_.lists) {
                if (!list.closed) {
                    bool is_selected = (selected_list_id_ == list.id);
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
        
        // Card details
        ImGui::Separator();
        ImGui::Text("Card Name:");
        ImGui::InputText("##card_name", new_card_name_, sizeof(new_card_name_));
        
        ImGui::Text("Description (optional):");
        ImGui::InputTextMultiline("##card_desc", new_card_desc_, sizeof(new_card_desc_), ImVec2(-1, 100));
        
        // Create button
        ImGui::Separator();
        bool can_create = !selected_list_id_.empty() && strlen(new_card_name_) > 0 && !creating_card_;
        
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
    
    // Current connection info
    ImGui::Text("Current Profile: %s", trello_host_->get_current_profile_name().c_str());
    
    ImGui::Separator();
    
    // Search functionality
    render_search_section();
    
    ImGui::Separator();
    
    // Connection management
    if (ImGui::Button("Manage Profiles", ImVec2(150, 0))) {
        // Could open a profile management dialog in the future
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Test Connection", ImVec2(150, 0))) {
        fetch_boards(); // Simple connection test
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
                for (const auto& card : cards_in_list) {
                    render_card_item(card, &list);
                }
            }
        }
        ImGui::PopStyleColor(3);  // Pop colors for header
    }
}

void trello_card::render_card_item(const models::trello::trello_card& card, const models::trello::trello_list* /* list */) {
    ImGui::PushID(card.id.c_str());
    
    // Card name with clickable link
    if (ImGui::Selectable(card.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) {
            open_in_browser(card.url);
        } else {
            // Single click - open card in new UI card
            create_card_tab(card.id);
        }
    }
    
    // Show card details on hover
    if (ImGui::IsItemHovered() && !card.desc.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s", card.desc.c_str());
        ImGui::EndTooltip();
    }
    
    // Card badges and info
    if (card.badges_comments() > 0 || card.badges_attachments() > 0 || (card.due.has_value() && !card.due->empty())) {
        ImGui::SameLine();
        ImGui::Text("(");
        
        bool need_separator = false;
        
        if (card.badges_comments() > 0) {
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "%d" ICON_MD_COMMENT, card.badges_comments());
            need_separator = true;
        }
        
        if (card.badges_attachments() > 0) {
            if (need_separator) { ImGui::SameLine(); ImGui::Text("|"); }
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "%d" ICON_MD_ATTACHMENT, card.badges_attachments());
            need_separator = true;
        }
        
        if (card.due.has_value() && !card.due->empty()) {
            if (need_separator) { ImGui::SameLine(); ImGui::Text("|"); }
            ImGui::SameLine();
            ImGui::TextColored(card.dueComplete ? colors[6] : colors[2], ICON_MD_SCHEDULE);
        }
        
        ImGui::SameLine();
        ImGui::Text(")");
    }
    
    ImGui::PopID();
}

void trello_card::render_search_section() {
    ImGui::TextColored(colors[0], "Search Cards");
    
    ImGui::InputText("##search", search_query_, sizeof(search_query_));
    ImGui::SameLine();
    
    bool can_search = strlen(search_query_) > 0 && !searching_;
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
        
        for (const auto& card : search_results_) {
            ImGui::TableNextRow();
            
            // Card name
            ImGui::TableNextColumn();
            ImGui::Text("%s", card.name.c_str());
            if (!card.desc.empty()) {
                ImGui::TextColored(colors[5], "%.50s%s", card.desc.c_str(), 
                                 card.desc.length() > 50 ? "..." : "");
            }
            
            // Board (we'd need to resolve this from board_id)
            ImGui::TableNextColumn();
            ImGui::TextColored(colors[3], "ID: %s", card.idBoard.c_str());
            
            // Actions
            ImGui::TableNextColumn();
            ImGui::PushID(card.id.c_str());
            if (ImGui::Button("Open", ImVec2(70, 0))) {
                create_card_tab(card.id);
            }
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void trello_card::render_connection_form() {
    ImGui::Text("Manual Connection:");
    
    ImGui::Text("Profile Name:");
    ImGui::InputText("##profile_name", profile_name_buffer_, sizeof(profile_name_buffer_));
    
    ImGui::Text("API Key:");
    ImGui::InputText("##api_key", api_key_buffer_, sizeof(api_key_buffer_));
    
    ImGui::Text("Token:");
    ImGui::InputText("##token", token_buffer_, sizeof(token_buffer_), ImGuiInputTextFlags_Password);
    
    ImGui::Separator();
    
    if (ImGui::Button("Connect", ImVec2(100, 0))) {
        attempt_connection();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save Profile", ImVec2(100, 0))) {
        if (validate_connection_form()) {
            models::trello::trello_connection_profile profile;
            profile.name = strlen(profile_name_buffer_) > 0 ? profile_name_buffer_ : "Manual Connection";
            profile.api_key = api_key_buffer_;
            profile.token = token_buffer_;
            profile.is_environment = false;
            
            trello_host_->save_profile(profile);
            clear_error();
        }
    }
}

void trello_card::render_saved_profiles() {
    auto profiles = trello_host_->get_saved_profiles();
    if (profiles.empty()) {
        return;
    }
    
    ImGui::Text("Saved Profiles:");
    
    for (const auto& profile : profiles) {
        ImGui::PushID(profile.name.c_str());
        
        if (ImGui::Button(profile.name.c_str(), ImVec2(150, 0))) {
            try {
                trello_host_->connect_with_credentials(profile.api_key, profile.token, profile.name);
                clear_error();
                reset_ui_state();
            } catch (const std::exception& e) {
                show_error(std::format("Failed to connect with profile '{}': {}", profile.name, e.what()));
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(60, 0))) {
            trello_host_->delete_profile(profile.name);
        }
        
        ImGui::PopID();
    }
}

// Implementation of helper methods

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

void trello_card::create_new_card() {
    if (selected_list_id_.empty() || strlen(new_card_name_) == 0) return;
    
    creating_card_ = true;
    create_card_future_ = trello_host_->create_card(selected_list_id_, new_card_name_, new_card_desc_);
}

void trello_card::perform_search() {
    if (strlen(search_query_) == 0) return;
    
    searching_ = true;
    search_future_ = trello_host_->search_cards(search_query_, selected_board_id_);
}

void trello_card::attempt_connection() {
    if (!validate_connection_form()) return;
    
    try {
        std::string profile_name = strlen(profile_name_buffer_) > 0 ? profile_name_buffer_ : "Manual Connection";
        bool success = trello_host_->connect_with_credentials(api_key_buffer_, token_buffer_, profile_name);
        
        if (success) {
            clear_error();
            reset_ui_state();
        } else {
            show_error("Failed to connect with provided credentials");
        }
    } catch (const std::exception& e) {
        show_error(std::format("Connection error: {}", e.what()));
    }
}

void trello_card::try_environment_connection() {
    try {
        bool success = trello_host_->connect_from_environment();
        if (success) {
            clear_error();
            reset_ui_state();
        } else {
            show_error("Environment variables not found or invalid");
        }
    } catch (const std::exception& e) {
        show_error(std::format("Environment connection error: {}", e.what()));
    }
}

bool trello_card::validate_connection_form() {
    if (strlen(api_key_buffer_) == 0) {
        show_error("API Key is required");
        return false;
    }
    if (strlen(token_buffer_) == 0) {
        show_error("Token is required");
        return false;
    }
    return true;
}

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
        current_board_ = board_details_future_->get();
        board_details_future_.reset();
        loading_board_details_ = false;
        
        // If this was a direct board fetch (like for a specific board card), set the board name
        if (!current_board_.id.empty() && selected_board_name_.empty()) {
            selected_board_name_ = current_board_.name;
        }
        name(std::format("Trello - Board: {}", selected_board_name_));
        colors[0] = ImVec4(0.5f, 0.5f, 1.0f, 1.0f); // Update primary color
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
            // Note: We would need separate API calls to get parent board and list details
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

ImVec4 trello_card::get_label_color(const std::string& color_name) const {
    // Trello label colors
    if (color_name == "green") return ImVec4(0.0f, 0.7f, 0.2f, 1.0f);
    if (color_name == "yellow") return ImVec4(0.9f, 0.8f, 0.0f, 1.0f);
    if (color_name == "orange") return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    if (color_name == "red") return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    if (color_name == "purple") return ImVec4(0.6f, 0.2f, 0.8f, 1.0f);
    if (color_name == "blue") return ImVec4(0.2f, 0.5f, 0.9f, 1.0f);
    if (color_name == "sky") return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
    if (color_name == "lime") return ImVec4(0.5f, 1.0f, 0.0f, 1.0f);
    if (color_name == "pink") return ImVec4(1.0f, 0.4f, 0.7f, 1.0f);
    if (color_name == "black") return ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    return ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Default gray
}

std::string trello_card::format_due_date(const std::string& due_date) const {
    // Simple date formatting - could be enhanced with proper date parsing
    return due_date.substr(0, 10); // Just return YYYY-MM-DD part
}

void trello_card::open_in_browser(const std::string& url) const {
    rouen::platform::open_url(url);
}

void trello_card::show_error(const std::string& error) {
    connection_error_ = error;
}

void trello_card::clear_error() {
    connection_error_.clear();
}

// Card-specific interface methods (for trello-card: context)

void trello_card::render_card_interface() {
    if (!initialized_) {
        if (!initial_card_id_.empty()) {
            fetch_card_details();
        }
        initialized_ = true;
    }
    
    if (ImGui::BeginTabBar("TrelloCardTabs")) {
        if (ImGui::BeginTabItem(ICON_MD_INFO " Details")) {
            active_tab_ = 0;
            render_card_details_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_EDIT " Edit")) {
            active_tab_ = 1;
            render_card_edit_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_HISTORY " Activity")) {
            active_tab_ = 2;
            render_card_activity_tab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_MD_SETTINGS " Settings")) {
            active_tab_ = 3;
            render_settings_tab();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
}

void trello_card::render_card_details_tab() {
    if (loading_card_details_) {
        ImGui::TextColored(colors[1], "Loading card details...");
        return;
    }
    
    if (current_card_.id.empty()) {
        ImGui::TextColored(colors[2], "Card not found or failed to load");
        if (ImGui::Button("Retry")) {
            fetch_card_details();
        }
        return;
    }
    
    render_card_overview();
    ImGui::Separator();
    render_card_metadata();
    ImGui::Separator();
    render_card_actions();
}

void trello_card::render_card_edit_tab() {
    if (current_card_.id.empty()) {
        ImGui::TextColored(colors[3], "Load card details first");
        return;
    }
    
    ImGui::TextColored(colors[0], "Edit Card");
    ImGui::Separator();
    
    render_card_edit_form();
}

void trello_card::render_card_activity_tab() {
    ImGui::TextColored(colors[0], "Card Activity");
    ImGui::Separator();
    
    // Placeholder for activity/comments
    ImGui::TextColored(colors[5], "Activity history will be displayed here");
    ImGui::Text("• Comments");
    ImGui::Text("• Card movements");
    ImGui::Text("• Label changes");
    ImGui::Text("• Member assignments");
}

void trello_card::render_card_overview() {
    // Card name
    ImGui::TextColored(colors[0], "%s", current_card_.name.c_str());
    
    // Breadcrumb navigation
    if (!parent_board_.name.empty() && !parent_list_.name.empty()) {
        ImGui::TextColored(colors[5], "Board: %s > List: %s", 
                          parent_board_.name.c_str(), parent_list_.name.c_str());
    }
    
    // Description
    if (!current_card_.desc.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", current_card_.desc.c_str());
    }
    
    // Due date
    if (current_card_.due.has_value() && !current_card_.due->empty()) {
        ImGui::Spacing();
        ImVec4 due_color = current_card_.dueComplete ? colors[3] : colors[2];
        ImGui::TextColored(due_color, "%s Due: %s", 
                          ICON_MD_SCHEDULE, format_due_date(*current_card_.due).c_str());
        if (current_card_.dueComplete) {
            ImGui::SameLine();
            ImGui::TextColored(colors[3], "(Completed)");
        }
    }
    
    // Badges
    if (current_card_.badges_comments() > 0 || current_card_.badges_attachments() > 0) {
        ImGui::Spacing();
        if (current_card_.badges_comments() > 0) {
            ImGui::TextColored(colors[4], "%s %d comments", ICON_MD_COMMENT, current_card_.badges_comments());
        }
        if (current_card_.badges_attachments() > 0) {
            if (current_card_.badges_comments() > 0) ImGui::SameLine();
            ImGui::TextColored(colors[4], "%s %d attachments", ICON_MD_ATTACHMENT, current_card_.badges_attachments());
        }
    }
}

void trello_card::render_card_edit_form() {
    if (!editing_card_) {
        populate_edit_form();
        editing_card_ = true;
    }
    
    // Card name
    ImGui::Text("Card Name:");
    ImGui::InputText("##edit_card_name", edit_card_name_, sizeof(edit_card_name_));
    
    // Card description
    ImGui::Text("Description:");
    ImGui::InputTextMultiline("##edit_card_desc", edit_card_desc_, sizeof(edit_card_desc_), ImVec2(-1, 150));
    
    // Move to different list
    if (!parent_board_.lists.empty()) {
        ImGui::Text("Move to List:");
        if (ImGui::BeginCombo("##target_list", target_list_id_.empty() ? "Select list..." : "List selected")) {
            for (const auto& list : parent_board_.lists) {
                if (!list.closed) {
                    bool is_selected = (target_list_id_ == list.id);
                    if (ImGui::Selectable(list.name.c_str(), is_selected)) {
                        target_list_id_ = list.id;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    
    ImGui::Separator();
    
    // Action buttons
    bool can_update = strlen(edit_card_name_) > 0 && !updating_card_;
    
    if (!can_update) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Update Card", ImVec2(120, 0))) {
        update_card();
    }
    
    if (!can_update) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    if (!target_list_id_.empty() && target_list_id_ != current_card_.idList && !moving_card_) {
        if (ImGui::Button("Move Card", ImVec2(120, 0))) {
            move_card_to_list();
        }
        ImGui::SameLine();
    }
    
    if (ImGui::Button("Reset", ImVec2(80, 0))) {
        reset_edit_form();
    }
    
    if (updating_card_) {
        ImGui::SameLine();
        ImGui::TextColored(colors[1], "Updating...");
    }
    
    if (moving_card_) {
        ImGui::SameLine();
        ImGui::TextColored(colors[1], "Moving...");
    }
}

void trello_card::render_card_actions() {
    ImGui::TextColored(colors[0], "Quick Actions");
    
    if (ImGui::Button("Open in Trello", ImVec2(150, 0))) {
        open_in_browser(current_card_.url);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Copy Link", ImVec2(100, 0))) {
        ImGui::SetClipboardText(current_card_.url.c_str());
    }
    
    if (ImGui::Button("Archive Card", ImVec2(120, 0))) {
        archive_card();
    }
    
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Card", ImVec2(120, 0))) {
        delete_card();
    }
    ImGui::PopStyleColor();
}

void trello_card::render_card_metadata() {
    ImGui::TextColored(colors[0], "Card Information");
    
    ImGui::Text("Card ID: %s", current_card_.id.c_str());
    ImGui::Text("Position: %.1f", static_cast<double>(current_card_.pos));
    
    if (!current_card_.idLabels.empty()) {
        ImGui::Text("Labels: %zu", current_card_.idLabels.size());
    }
    
    if (!current_card_.idMembers.empty()) {
        ImGui::Text("Members: %zu", current_card_.idMembers.size());
    }
    
    if (!current_card_.idChecklists.empty()) {
        ImGui::Text("Checklists: %zu", current_card_.idChecklists.size());
    }
}

// Card-specific management methods

void trello_card::fetch_card_details() {
    if (initial_card_id_.empty()) return;
    
    loading_card_details_ = true;
    card_details_future_ = trello_host_->get_card_details(initial_card_id_);
}

void trello_card::update_card() {
    if (current_card_.id.empty() || strlen(edit_card_name_) == 0) return;
    
    updating_card_ = true;
    update_card_future_ = trello_host_->update_card(current_card_.id, edit_card_name_, edit_card_desc_);
}

void trello_card::move_card_to_list() {
    if (current_card_.id.empty() || target_list_id_.empty()) return;
    
    moving_card_ = true;
    move_card_future_ = trello_host_->move_card(current_card_.id, target_list_id_);
}

void trello_card::archive_card() {
    // Implementation for archiving - Trello uses "closed" field
    // This would require an API method to set closed=true
    ImGui::OpenPopup("Archive Card?");
}

void trello_card::delete_card() {
    // Implementation for deletion
    ImGui::OpenPopup("Delete Card?");
}

void trello_card::populate_edit_form() {
    if (current_card_.id.empty()) return;
    
    std::strncpy(edit_card_name_, current_card_.name.c_str(), sizeof(edit_card_name_) - 1);
    edit_card_name_[sizeof(edit_card_name_) - 1] = '\0';
    
    std::strncpy(edit_card_desc_, current_card_.desc.c_str(), sizeof(edit_card_desc_) - 1);
    edit_card_desc_[sizeof(edit_card_desc_) - 1] = '\0';
    
    target_list_id_ = current_card_.idList;  // Default to current list
}

void trello_card::reset_edit_form() {
    populate_edit_form();
}

void trello_card::create_card_tab(const std::string& card_id) {
    if (card_id.empty()) return;
    
    // Create a new Trello card for this specific card
    "create_card"_sfn("trello-card:" + card_id);
}

} // namespace rouen::cards
