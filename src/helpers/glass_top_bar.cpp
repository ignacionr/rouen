#include "glass_top_bar.hpp"
#include <cmath>
#include <string>

namespace rouen::helpers {

void glass_top_bar::render(
    std::string_view child_id,
    ImVec2 const& region_size,
    const std::function<void()>& render_top_controls_fn,
    const std::function<void()>& render_content_fn)
{
    render(child_id, region_size, render_top_controls_fn, render_content_fn, style_config{});
}

void glass_top_bar::render(
    std::string_view child_id,
    ImVec2 const& region_size,
    const std::function<void()>& render_top_controls_fn,
    const std::function<void()>& render_content_fn,
    const style_config& config)
{
    ImVec2 const start_pos = ImGui::GetCursorPos();
    ImVec2 const avail = ImGui::GetContentRegionAvail();

    float const w = (region_size.x > 0.0f) ? region_size.x : avail.x;
    float const h = (region_size.y > 0.0f) ? region_size.y : avail.y;

    if (w <= 0.0f || h <= 0.0f) return;

    std::string const content_child_id(child_id);
    std::string const top_child_id = std::string(child_id) + "_top_glass_bar";

    ImGuiID const storage_id = ImGui::GetID(top_child_id.c_str());
    ImGuiStorage* storage = ImGui::GetStateStorage();

    float const last_top_h = storage->GetFloat(storage_id, 46.0f);

    // 1. CHILD WINDOW 1: Scroll Content Area (renders covers & items first)
    ImGui::SetCursorPos(start_pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild(content_child_id.c_str(), ImVec2(w, h), false, ImGuiWindowFlags_NavFlattened)) {
        if (last_top_h > 0.0f) {
            ImGui::Dummy(ImVec2(w, last_top_h));
        }
        render_content_fn();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // 2. CHILD WINDOW 2: Semi-Transparent Glass Top Bar (renders second, ON TOP of Child Window 1)
    ImGui::SetCursorPos(start_pos);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, config.glass_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(config.padding_x, config.padding_y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags const glass_flags = ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoScrollWithMouse |
                                         ImGuiWindowFlags_NavFlattened;

    if (ImGui::BeginChild(top_child_id.c_str(), ImVec2(w, last_top_h), false, glass_flags)) {
        ImVec2 const glass_min = ImGui::GetWindowPos();
        ImVec2 const glass_max = ImVec2(glass_min.x + w, glass_min.y + last_top_h);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        if (config.draw_glow) {
            // Ambient glass top glow
            draw_list->AddRectFilled(glass_min, ImVec2(glass_max.x, glass_min.y + 2.0f), ImGui::GetColorU32(config.glass_glow));
        }
        // Frosted glass bottom border line
        draw_list->AddLine(ImVec2(glass_min.x, glass_max.y - 1.0f), ImVec2(glass_max.x, glass_max.y - 1.0f), ImGui::GetColorU32(config.glass_border), 1.0f);

        // Render interactive top bar controls
        ImGui::BeginGroup();
        ImGui::PushItemWidth(w - config.padding_x * 2.0f);
        render_top_controls_fn();
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        float const measured_top_h = ImGui::GetItemRectSize().y + config.padding_y * 2.0f;
        if (std::abs(measured_top_h - last_top_h) > 0.5f) {
            storage->SetFloat(storage_id, measured_top_h);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Advance parent cursor past the full region height
    ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + h));
}

} // namespace rouen::helpers
