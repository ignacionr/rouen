#pragma once

#include "ui_context.hpp"
#include <imgui.h>

namespace rouen::ui {

class imgui_draw_context_impl : public draw_context {
public:
    imgui_draw_context_impl() = default;
    
    void set_draw_list(ImDrawList* dl) { draw_list_ = dl; }
    
    void channels_split(int count) override {
        if (draw_list_) draw_list_->ChannelsSplit(count);
    }
    
    void channels_set_current(int index) override {
        if (draw_list_) draw_list_->ChannelsSetCurrent(index);
    }
    
    void channels_merge() override {
        if (draw_list_) draw_list_->ChannelsMerge();
    }
    
    void add_rect_filled(const ImVec2& p_min, const ImVec2& p_max, uint32_t col, float rounding = 0.0f) override {
        if (draw_list_) draw_list_->AddRectFilled(p_min, p_max, col, rounding);
    }
    
    void add_rect(const ImVec2& p_min, const ImVec2& p_max, uint32_t col, float rounding = 0.0f) override {
        if (draw_list_) draw_list_->AddRect(p_min, p_max, col, rounding);
    }

private:
    ImDrawList* draw_list_ = nullptr;
};

class imgui_ui_context_impl : public ui_context {
public:
    imgui_ui_context_impl() = default;
    
    void prepare() {
        draw_impl_.set_draw_list(ImGui::GetWindowDrawList());
    }

    void text(const std::string& text) override {
        ImGui::Text("%s", text.c_str());
    }
    
    void text_colored(const ImVec4& color, const std::string& text) override {
        ImGui::TextColored(color, "%s", text.c_str());
    }
    
    void text_wrapped(const std::string& text) override {
        ImGui::TextWrapped("%s", text.c_str());
    }

    void text_unformatted(const std::string& text) override {
        ImGui::TextUnformatted(text.c_str());
    }
    
    bool button(const std::string& label, const ImVec2& size = ImVec2(0, 0)) override {
        return ImGui::Button(label.c_str(), size);
    }
    
    bool checkbox(const std::string& label, bool* checked) override {
        return ImGui::Checkbox(label.c_str(), checked);
    }
    
    bool input_text(const std::string& label, char* buf, size_t buf_size) override {
        return ImGui::InputText(label.c_str(), buf, buf_size);
    }

    bool input_text_with_placeholder(const std::string& label, char* buf, size_t buf_size, const std::string& placeholder, bool enter_returns_true = false) override {
        ImGuiInputTextFlags flags = enter_returns_true ? ImGuiInputTextFlags_EnterReturnsTrue : 0;
        bool result = ImGui::InputText(label.c_str(), buf, buf_size, flags);
        if (buf[0] == '\0' && !ImGui::IsItemActive()) {
            auto pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + 5, pos.y + 2), 
                ImGui::GetColorU32(ImGuiCol_TextDisabled), 
                placeholder.c_str()
            );
        }
        return result;
    }
    
    void progress_bar(float fraction, const ImVec2& size = ImVec2(-1, 0), const std::string& overlay = "") override {
        ImGui::ProgressBar(fraction, size, overlay.empty() ? nullptr : overlay.c_str());
    }

    bool selectable(const std::string& label, bool selected = false) override {
        return ImGui::Selectable(label.c_str(), selected);
    }
    
    void image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0)) override {
        ImGui::Image(user_texture_id, size, uv0, uv1, tint_col, border_col);
    }
    
    void set_clipboard_text(const std::string& text) override {
        ImGui::SetClipboardText(text.c_str());
    }
    
    void separator() override {
        ImGui::Separator();
    }
    
    void spacing() override {
        ImGui::Spacing();
    }
    
    void same_line(float offset_from_start_x = 0.0f, float spacing = -1.0f) override {
        ImGui::SameLine(offset_from_start_x, spacing);
    }
    
    void indent(float indent_w = 0.0f) override {
        ImGui::Indent(indent_w);
    }
    
    void unindent(float indent_w = 0.0f) override {
        ImGui::Unindent(indent_w);
    }
    
    void dummy(const ImVec2& size) override {
        ImGui::Dummy(size);
    }
    
    void push_style_color(style_color idx, const ImVec4& col) override {
        ImGuiCol imgui_idx;
        switch (idx) {
            case style_color::frame_bg: imgui_idx = ImGuiCol_FrameBg; break;
            case style_color::text: imgui_idx = ImGuiCol_Text; break;
            case style_color::header: imgui_idx = ImGuiCol_Header; break;
            case style_color::header_hovered: imgui_idx = ImGuiCol_HeaderHovered; break;
            case style_color::header_active: imgui_idx = ImGuiCol_HeaderActive; break;
            case style_color::plot_histogram: imgui_idx = ImGuiCol_PlotHistogram; break;
            default: return;
        }
        ImGui::PushStyleColor(imgui_idx, col);
    }
    
    void pop_style_color(int count = 1) override {
        ImGui::PopStyleColor(count);
    }
    
    float get_item_inner_spacing_x() const override {
        return ImGui::GetStyle().ItemInnerSpacing.x;
    }

    void push_item_width(float item_width) override {
        ImGui::PushItemWidth(item_width);
    }

    void pop_item_width() override {
        ImGui::PopItemWidth();
    }

    bool is_window_focused() override {
        return ImGui::IsWindowFocused();
    }

    bool is_any_item_active() override {
        return ImGui::IsAnyItemActive();
    }

    bool is_mouse_clicked(int button = 0) override {
        return ImGui::IsMouseClicked(button);
    }

    void set_keyboard_focus_here(int offset = 0) override {
        ImGui::SetKeyboardFocusHere(offset);
    }

    bool is_item_active() override {
        return ImGui::IsItemActive();
    }

    bool is_key_pressed(key k) override {
        ImGuiKey imgui_k;
        switch (k) {
            case key::up_arrow: imgui_k = ImGuiKey_UpArrow; break;
            case key::down_arrow: imgui_k = ImGuiKey_DownArrow; break;
            case key::enter: imgui_k = ImGuiKey_Enter; break;
            case key::escape: imgui_k = ImGuiKey_Escape; break;
            default: return false;
        }
        return ImGui::IsKeyPressed(imgui_k);
    }

    void set_item_default_focus() override {
        ImGui::SetItemDefaultFocus();
    }
    
    bool begin_child(const std::string& id, const ImVec2& size = ImVec2(0,0), bool border = false, int flags = 0) override {
        return ImGui::BeginChild(id.c_str(), size, border, flags);
    }
    
    void end_child() override {
        ImGui::EndChild();
    }
    
    void begin_group() override {
        ImGui::BeginGroup();
    }
    
    void end_group() override {
        ImGui::EndGroup();
    }
    
    bool collapsing_header(const std::string& label, int flags = 0) override {
        return ImGui::CollapsingHeader(label.c_str(), flags);
    }
    
    void columns(int count, const std::string& id = "", bool border = true) override {
        ImGui::Columns(count, id.empty() ? nullptr : id.c_str(), border);
    }
    
    void next_column() override {
        ImGui::NextColumn();
    }
    
    void set_column_width(int index, float width) override {
        ImGui::SetColumnWidth(index, width);
    }
    
    bool begin_table(const std::string& str_id, int column, int flags = 0) override {
        return ImGui::BeginTable(str_id.c_str(), column, flags);
    }
    
    void table_setup_column(const std::string& label, int flags = 0, float init_width_or_weight = 0.0f) override {
        ImGui::TableSetupColumn(label.c_str(), flags, init_width_or_weight);
    }
    
    void table_headers_row() override {
        ImGui::TableHeadersRow();
    }
    
    void table_next_row() override {
        ImGui::TableNextRow();
    }
    
    void table_set_column_index(int column_n) override {
        ImGui::TableSetColumnIndex(column_n);
    }
    
    void end_table() override {
        ImGui::EndTable();
    }
    
    draw_context& get_draw_context() override {
        return draw_impl_;
    }

private:
    imgui_draw_context_impl draw_impl_;
};

} // namespace rouen::ui
