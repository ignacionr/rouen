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
    // Description (now prominently displayed where card name was)
    if (!current_card_.desc.empty()) {
        ImGui::TextColored(colors[0], "%s", current_card_.desc.c_str());
    } else {
        ImGui::TextColored(colors[5], "(No description)");
    }
    
    // Breadcrumb navigation
    if (!parent_board_.name.empty() && !parent_list_.name.empty()) {
        ImGui::TextColored(colors[5], "Board: %s > List: %s", 
                          parent_board_.name.c_str(), parent_list_.name.c_str());
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
        
        // Find current list name for display
        std::string current_list_name = "Unknown List";
        std::string combo_preview = "Select list...";
        
        for (const auto& list : parent_board_.lists) {
            if (list.id == current_card_.idList) {
                current_list_name = list.name;
                break;
            }
        }
        
        if (!target_list_id_.empty()) {
            for (const auto& list : parent_board_.lists) {
                if (list.id == target_list_id_) {
                    combo_preview = list.name;
                    break;
                }
            }
        } else {
            combo_preview = std::format("Currently in: {}", current_list_name);
        }
        
        if (ImGui::BeginCombo("##target_list", combo_preview.c_str())) {
            for (const auto& list : parent_board_.lists) {
                if (!list.closed) {
                    bool is_selected = (target_list_id_ == list.id);
                    bool is_current = (list.id == current_card_.idList);
                    
                    std::string display_name = list.name;
                    if (is_current) {
                        display_name += " (current)";
                    }
                    
                    if (ImGui::Selectable(display_name.c_str(), is_selected)) {
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
    if (ImGui::Button("Open in Trello", ImVec2(150, 0))) {
        open_in_browser(current_card_.url);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Copy Link", ImVec2(100, 0))) {
        ImGui::SetClipboardText(current_card_.url.c_str());
    }
    
    // Quick move section
    if (!parent_board_.lists.empty()) {
        ImGui::Spacing();
        
        // Find current list name
        std::string current_list_name = "Unknown List";
        for (const auto& list : parent_board_.lists) {
            if (list.id == current_card_.idList) {
                current_list_name = list.name;
                break;
            }
        }
        
        ImGui::Text("Current list: %s", current_list_name.c_str());
        
        // Show quick move buttons for other lists
        int button_count = 0;
        for (const auto& list : parent_board_.lists) {
            if (!list.closed && list.id != current_card_.idList) {
                if (button_count > 0 && button_count % 2 == 0) {
                    // Start new row after every 2 buttons
                } else if (button_count > 0) {
                    ImGui::SameLine();
                }
                
                if (ImGui::Button(std::format("→ {}", list.name).c_str(), ImVec2(140, 0))) {
                    target_list_id_ = list.id;
                    move_card_to_list();
                }
                button_count++;
                
                if (button_count >= 4) break; // Limit to 4 quick move buttons
            }
        }
        
        if (button_count >= 4) {
            ImGui::Text("For more lists, use the Edit tab");
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
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

void trello_card::render_card_metadata() const {
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

void trello_card::create_new_card() {
    if (selected_list_id_.empty() || strlen(new_card_name_) == 0) return;
    
    creating_card_ = true;
    create_card_future_ = trello_host_->create_card(selected_list_id_, new_card_name_, new_card_desc_);
}

} // namespace rouen::cards
