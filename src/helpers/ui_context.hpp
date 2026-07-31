#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "imgui_include.hpp"

namespace rouen::ui {

enum class style_color {
    frame_bg,
    text,
    header,
    header_hovered,
    header_active,
    plot_histogram
};

enum class key {
    up_arrow,
    down_arrow,
    enter,
    escape
};

class draw_context {
public:
    virtual ~draw_context() = default;

    virtual void channels_split(int count) = 0;
    virtual void channels_set_current(int index) = 0;
    virtual void channels_merge() = 0;
    
    virtual void add_rect_filled(const ImVec2& p_min, const ImVec2& p_max, uint32_t col, float rounding = 0.0f) = 0;
    virtual void add_rect(const ImVec2& p_min, const ImVec2& p_max, uint32_t col, float rounding = 0.0f) = 0;
};

class ui_context {
public:
    virtual ~ui_context() = default;

    // Basic Widgets
    virtual void text(const std::string& text) = 0;
    virtual void text_view(std::string_view text) { this->text_unformatted(std::string(text)); }
    virtual void text_colored(const ImVec4& color, const std::string& text) = 0;
    virtual void text_wrapped(const std::string& text) = 0;
    virtual void text_unformatted(const std::string& text) = 0;
    virtual bool button(const std::string& label, const ImVec2& size = ImVec2(0, 0)) = 0;
    virtual bool checkbox(const std::string& label, bool* checked) = 0;
    virtual bool input_text(const std::string& label, char* buf, size_t buf_size) = 0;
    virtual bool input_int(const std::string& label, int* v, int step = 1, int step_fast = 100) = 0;
    virtual bool input_text_with_placeholder(const std::string& label, char* buf, size_t buf_size, const std::string& placeholder, bool enter_returns_true = false) = 0;
    virtual void progress_bar(float fraction, const ImVec2& size = ImVec2(-1, 0), const std::string& overlay = "") = 0;
    virtual bool selectable(const std::string& label, bool selected = false) = 0;
    virtual void image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0)) = 0;
    virtual void set_clipboard_text(const std::string& text) = 0;
    
    // Layout and Styling
    virtual void separator() = 0;
    virtual void spacing() = 0;
    virtual void same_line(float offset_from_start_x = 0.0f, float spacing = -1.0f) = 0;
    virtual void indent(float indent_w = 0.0f) = 0;
    virtual void unindent(float indent_w = 0.0f) = 0;
    virtual void dummy(const ImVec2& size) = 0;
    
    // Styling states
    virtual void push_style_color(style_color idx, const ImVec4& col) = 0;
    virtual void pop_style_color(int count = 1) = 0;
    virtual float get_item_inner_spacing_x() const = 0;
    virtual void push_item_width(float item_width) = 0;
    virtual void pop_item_width() = 0;
    
    // Input / Keyboard State
    virtual bool is_window_focused() = 0;
    virtual bool is_any_item_active() = 0;
    virtual bool is_mouse_clicked(int button = 0) = 0;
    virtual void set_keyboard_focus_here(int offset = 0) = 0;
    virtual bool is_item_active() = 0;
    virtual bool is_key_pressed(key k) = 0;
    virtual void set_item_default_focus() = 0;
    
    // Containers
    virtual bool begin_child(const std::string& id, const ImVec2& size = ImVec2(0,0), bool border = false, int flags = 0) = 0;
    virtual void end_child() = 0;
    virtual void begin_group() = 0;
    virtual void end_group() = 0;
    virtual bool collapsing_header(const std::string& label, int flags = 0) = 0;
    
    // Columns Layout
    virtual void columns(int count, const std::string& id = "", bool border = true) = 0;
    virtual void next_column() = 0;
    virtual void set_column_width(int index, float width) = 0;
    
    // Tables API
    virtual bool begin_table(const std::string& str_id, int column, int flags = 0) = 0;
    virtual void table_setup_column(const std::string& label, int flags = 0, float init_width_or_weight = 0.0f) = 0;
    virtual void table_headers_row() = 0;
    virtual void table_next_row() = 0;
    virtual void table_set_column_index(int column_n) = 0;
    virtual void end_table() = 0;
    
    // Draw list for low-level drawing
    virtual draw_context& get_draw_context() = 0;
};

} // namespace rouen::ui
