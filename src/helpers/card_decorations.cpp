#include "card_decorations.hpp"
#include "tag_manager.hpp"
#include "../cards/interface/card.hpp"
#include "imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <format>

namespace rouen::helpers {

void card_decorations::render_decorations(card* c) {
    if (!c || !c->supports_menu_decoration()) {
        return;
    }

    std::string uri = c->get_uri();
    if (uri.empty()) {
        return;
    }

    bool is_in_menu = is_added_to_menu(uri);

    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();
    ImVec2 content_min = ImGui::GetWindowContentRegionMin(); // Content top-left relative to window

    float font_size = ImGui::GetFontSize();
    float button_w = font_size + 8.0f;
    float button_h = font_size + 4.0f;

    float btn_x = 0.0f;
    float btn_y = 0.0f;

    // If content_min.y > 10.0f, a window title bar is visible above content
    if (content_min.y > 10.0f) {
        float title_bar_h = content_min.y;
        btn_x = win_pos.x + win_size.x - title_bar_h - button_w - 6.0f;
        btn_y = win_pos.y + (title_bar_h - button_h) * 0.5f;
    } else {
        // Position at top right inside content region
        btn_x = win_pos.x + win_size.x - button_w - 8.0f;
        btn_y = win_pos.y + 6.0f;
    }

    // Set absolute screen position so button renders on top of card content
    ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));

    // Button style
    if (is_in_menu) {
        // Active gold/yellow pin badge
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.65f, 0.1f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.55f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
    } else {
        // Subtle, crisp semi-transparent badge
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.2f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.45f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.95f, 0.9f));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));

    const char* icon = is_in_menu ? ICON_MD_BOOKMARK : ICON_MD_BOOKMARK_BORDER;
    std::string btn_id = std::format("{}##menu_dec_{}", icon, static_cast<const void*>(c));

    if (ImGui::Button(btn_id.c_str(), ImVec2(button_w, button_h))) {
        toggle_menu_tag(c);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", is_in_menu ? "Remove from Application Menu (.menu tag)" : "Add to Application Menu (.menu tag)");
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

bool card_decorations::is_added_to_menu(const std::string& uri) {
    if (uri.empty()) return false;
    return tag_manager::get().has_tag(uri, ".menu");
}

void card_decorations::toggle_menu_tag(card* c) {
    if (!c) return;
    std::string uri = c->get_uri();
    if (uri.empty()) return;

    auto& tm = tag_manager::get();
    if (tm.has_tag(uri, ".menu")) {
        tm.remove_tag(uri, ".menu");
    } else {
        std::string title = c->window_title;
        size_t hash_pos = title.find("##");
        if (hash_pos != std::string::npos) {
            title = title.substr(0, hash_pos);
        }
        if (title.empty()) {
            title = uri;
        }
        tm.add_tag(uri, ".menu", title);
    }
}

std::vector<card_decorations::menu_item_info> card_decorations::get_menu_tagged_items() {
    auto uris = tag_manager::get().get_uris_by_tag(".menu");
    std::vector<menu_item_info> result;
    result.reserve(uris.size());

    for (const auto& uri : uris) {
        std::string title = tag_manager::get().get_uri_title(uri);
        if (title.empty()) {
            // Generate fallback title from URI if none saved
            if (uri.starts_with("weather:")) {
                title = "Weather: " + uri.substr(8);
            } else if (uri.starts_with("git:")) {
                title = "Git: " + uri.substr(4);
            } else if (uri.starts_with("dir:")) {
                title = "Directory: " + uri.substr(4);
            } else if (uri.starts_with("notes:")) {
                title = "Notes: " + uri.substr(6);
            } else if (uri.starts_with("ai-chat:")) {
                title = "AI Chat: " + uri.substr(8);
            } else {
                title = uri;
            }
        }
        result.push_back({uri, title});
    }

    return result;
}

} // namespace rouen::helpers
