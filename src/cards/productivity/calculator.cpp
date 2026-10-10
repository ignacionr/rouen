#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include <imgui.h>

#include "calculator.hpp"
#include "cards/interface/card.hpp"

namespace rouen::cards {

namespace {

struct math_parser {
    std::string text;
    size_t pos{0};
    double ans_val{0.0};

    math_parser(std::string expr, double ans) : text(std::move(expr)), ans_val(ans) {}

    void skip_ws() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            pos++;
        }
    }

    char peek() {
        skip_ws();
        return pos < text.size() ? text[pos] : '\0';
    }

    char get() {
        skip_ws();
        return pos < text.size() ? text[pos++] : '\0';
    }

    bool match(char expected) {
        if (peek() == expected) {
            get();
            return true;
        }
        return false;
    }

    double parse() {
        if (text.empty()) return 0.0;
        double const res = parse_expression();
        skip_ws();
        if (pos < text.size()) {
            throw std::runtime_error(std::format("Unexpected character '{}'", text[pos]));
        }
        if (std::isnan(res)) throw std::runtime_error("Result is NaN");
        if (std::isinf(res)) throw std::runtime_error("Result is Infinity");
        return res;
    }

    double parse_expression() {
        double left = parse_term();
        while (true) {
            char const op = peek();
            if (op == '+' || op == '-') {
                get();
                double const right = parse_term();
                if (op == '+') left += right;
                else left -= right;
            } else {
                break;
            }
        }
        return left;
    }

    double parse_term() {
        double left = parse_unary();
        while (true) {
            char const op = peek();
            if (op == '*' || op == '/' || op == '%') {
                get();
                double const right = parse_unary();
                if (op == '*') left *= right;
                else if (op == '/') {
                    if (right == 0.0) throw std::runtime_error("Division by zero");
                    left /= right;
                } else if (op == '%') {
                    if (right == 0.0) throw std::runtime_error("Modulo by zero");
                    left = std::fmod(left, right);
                }
            } else if (peek() == '(' || std::isalpha(static_cast<unsigned char>(peek()))) {
                // Implicit multiplication, e.g. 2(3+4) or 3pi or 2sqrt(9)
                double const right = parse_unary();
                left *= right;
            } else {
                break;
            }
        }
        return left;
    }

    double parse_unary() {
        if (match('+')) return parse_unary();
        if (match('-')) return -parse_unary();
        return parse_power();
    }

    double parse_power() {
        double const base = parse_postfix();
        if (match('^')) {
            double const exponent = parse_unary();
            return std::pow(base, exponent);
        }
        return base;
    }

    double parse_postfix() {
        double val = parse_primary();
        while (match('!')) {
            if (val < 0 || val != std::floor(val) || val > 170) {
                throw std::runtime_error("Invalid argument for factorial");
            }
            double f = 1.0;
            for (int i = 2; i <= static_cast<int>(val); ++i) f *= i;
            val = f;
        }
        return val;
    }

    double parse_primary() {
        skip_ws();
        char c = peek();

        if (c == '(') {
            get();
            double const val = parse_expression();
            if (!match(')')) throw std::runtime_error("Missing ')'");
            return val;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            return parse_number();
        }

        if (std::isalpha(static_cast<unsigned char>(c))) {
            return parse_identifier();
        }

        if (c == '\0') {
            throw std::runtime_error("Unexpected end of expression");
        }

        throw std::runtime_error(std::format("Unexpected token '{}'", c));
    }

    double parse_number() {
        size_t const start = pos;
        bool has_dot = false;
        bool has_e = false;

        while (pos < text.size()) {
            char const ch = text[pos];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                pos++;
            } else if (ch == '.' && !has_dot && !has_e) {
                has_dot = true;
                pos++;
            } else if ((ch == 'e' || ch == 'E') && !has_e) {
                if (pos + 1 < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos+1])) || text[pos+1] == '+' || text[pos+1] == '-')) {
                    has_e = true;
                    pos++;
                    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
                        pos++;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        std::string const num_str = text.substr(start, pos - start);
        try {
            return std::stod(num_str);
        } catch (...) {
            throw std::runtime_error("Invalid number: " + num_str);
        }
    }

    double parse_identifier() {
        size_t const start = pos;
        while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
            pos++;
        }
        std::string name = text.substr(start, pos - start);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (name == "pi") return std::numbers::pi;
        if (name == "e") return std::numbers::e;
        if (name == "ans") return ans_val;

        if (peek() == '(') {
            get(); // consume '('
            double const arg = parse_expression();
            if (!match(')')) throw std::runtime_error("Missing ')' for " + name);

            if (name == "sqrt") {
                if (arg < 0) throw std::runtime_error("Domain error: sqrt of negative number");
                return std::sqrt(arg);
            }
            if (name == "sqr") return arg * arg;
            if (name == "sin") return std::sin(arg);
            if (name == "cos") return std::cos(arg);
            if (name == "tan") return std::tan(arg);
            if (name == "asin") {
                if (arg < -1.0 || arg > 1.0) throw std::runtime_error("Domain error: asin out of [-1, 1]");
                return std::asin(arg);
            }
            if (name == "acos") {
                if (arg < -1.0 || arg > 1.0) throw std::runtime_error("Domain error: acos out of [-1, 1]");
                return std::acos(arg);
            }
            if (name == "atan") return std::atan(arg);
            if (name == "log") {
                if (arg <= 0) throw std::runtime_error("Domain error: log of non-positive");
                return std::log10(arg);
            }
            if (name == "ln") {
                if (arg <= 0) throw std::runtime_error("Domain error: ln of non-positive");
                return std::log(arg);
            }
            if (name == "abs") return std::abs(arg);
            if (name == "floor") return std::floor(arg);
            if (name == "ceil") return std::ceil(arg);
            if (name == "fact") {
                if (arg < 0 || arg != std::floor(arg) || arg > 170) throw std::runtime_error("Invalid argument for fact()");
                double f = 1.0;
                for (int i = 2; i <= static_cast<int>(arg); ++i) f *= i;
                return f;
            }

            throw std::runtime_error("Unknown function: " + name);
        }

        throw std::runtime_error("Unknown symbol: " + name);
    }
};

std::string format_number(double val) {
    if (std::isnan(val)) return "NaN";
    if (std::isinf(val)) return val > 0 ? "Infinity" : "-Infinity";

    if (std::abs(val - std::round(val)) < 1e-10) {
        return std::format("{:.0f}", std::round(val));
    }
    return std::format("{:.10g}", val);
}

} // namespace

calculator::calculator() {
    name("Calculator");
    width = 330.0f;
    requested_fps = 4;

    colors[0] = ImVec4{0.20f, 0.55f, 0.90f, 1.0f}; // Primary accent
    colors[1] = ImVec4{0.15f, 0.75f, 0.65f, 0.8f}; // Secondary accent
}

std::string calculator::get_uri() const {
    return "calculator";
}

std::pair<std::string, std::string> calculator::evaluate(const std::string& expr, double ans_val) {
    math_parser parser(expr, ans_val);
    try {
        double const val = parser.parse();
        return {format_number(val), ""};
    } catch (const std::exception& e) {
        return {"", e.what()};
    }
}

void calculator::trigger_key_flash(std::string_view key_id) {
    last_flashed_key_ = std::string(key_id);
    flash_timer_ = 0.18f;
}

bool calculator::is_key_flashed(std::string_view key_id) const {
    return flash_timer_ > 0.0f && last_flashed_key_ == key_id;
}

void calculator::append_to_expression(std::string_view str) {
    if (newly_evaluated_) {
        if (str.starts_with(" ") || str == "^" || str == "%") {
            display_expr_ = evaluated_result_.empty() ? "0" : evaluated_result_;
            display_expr_ += str;
        } else {
            display_expr_ = str;
        }
        newly_evaluated_ = false;
    } else {
        if (display_expr_ == "0" && !str.starts_with(" ") && str != "." && str != ")") {
            display_expr_ = str;
        } else {
            display_expr_ += str;
        }
    }
    error_message_.clear();
}

void calculator::backspace_expression() {
    if (newly_evaluated_) {
        clear_all();
        return;
    }
    if (!display_expr_.empty()) {
        if (display_expr_.ends_with(" ")) {
            display_expr_.pop_back();
            if (!display_expr_.empty()) display_expr_.pop_back();
            if (!display_expr_.empty() && display_expr_.ends_with(" ")) display_expr_.pop_back();
        } else {
            display_expr_.pop_back();
        }
    }
    if (display_expr_.empty()) {
        display_expr_ = "0";
    }
    error_message_.clear();
}

void calculator::clear_all() {
    display_expr_ = "0";
    evaluated_result_.clear();
    error_message_.clear();
    newly_evaluated_ = false;
}

void calculator::evaluate_current() {
    auto [res, err] = evaluate(display_expr_, ans_value_);
    if (!err.empty()) {
        error_message_ = err;
    } else {
        std::string orig_expr = display_expr_;
        evaluated_result_ = res;
        display_expr_ = res;
        error_message_.clear();
        try {
            ans_value_ = std::stod(res);
        } catch (...) {
            // Non-numeric result, ignore stod failure
        }

        history_.push_back({orig_expr, res});
        if (history_.size() > 50) {
            history_.erase(history_.begin());
        }
        history_index_ = -1;
        newly_evaluated_ = true;
    }
}

void calculator::negate_current() {
    if (display_expr_.starts_with("-(")) {
        display_expr_ = display_expr_.substr(2, display_expr_.size() - 3);
    } else if (display_expr_.starts_with("-")) {
        display_expr_ = display_expr_.substr(1);
    } else {
        display_expr_ = "-(" + display_expr_ + ")";
    }
    newly_evaluated_ = false;
}

void calculator::handle_keyboard_input() {
    bool const active_focus = is_focused || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!active_focus) return;

    ImGuiIO const& io = ImGui::GetIO();

    if (flash_timer_ > 0.0f) {
        flash_timer_ -= io.DeltaTime;
        if (flash_timer_ <= 0.0f) {
            last_flashed_key_.clear();
        }
    }

    bool const shift = io.KeyShift;
    bool const ctrl = io.KeyCtrl || io.KeySuper;

    // Do not interfere with Ctrl/Cmd shortcuts
    if (ctrl) return;

    // Digit Keys (Numpad or Main Keyboard)
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad0) || (!shift && ImGui::IsKeyPressed(ImGuiKey_0))) { append_to_expression("0"); trigger_key_flash("0"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad1) || (!shift && ImGui::IsKeyPressed(ImGuiKey_1))) { append_to_expression("1"); trigger_key_flash("1"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad2) || (!shift && ImGui::IsKeyPressed(ImGuiKey_2))) { append_to_expression("2"); trigger_key_flash("2"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad3) || (!shift && ImGui::IsKeyPressed(ImGuiKey_3))) { append_to_expression("3"); trigger_key_flash("3"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad4) || (!shift && ImGui::IsKeyPressed(ImGuiKey_4))) { append_to_expression("4"); trigger_key_flash("4"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad5) || (!shift && ImGui::IsKeyPressed(ImGuiKey_5))) { append_to_expression("5"); trigger_key_flash("5"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad6) || (!shift && ImGui::IsKeyPressed(ImGuiKey_6))) { append_to_expression("6"); trigger_key_flash("6"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad7) || (!shift && ImGui::IsKeyPressed(ImGuiKey_7))) { append_to_expression("7"); trigger_key_flash("7"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad8) || (!shift && ImGui::IsKeyPressed(ImGuiKey_8))) { append_to_expression("8"); trigger_key_flash("8"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Keypad9) || (!shift && ImGui::IsKeyPressed(ImGuiKey_9))) { append_to_expression("9"); trigger_key_flash("9"); }
    
    // Numpad Operators
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) { append_to_expression(" + "); trigger_key_flash("+"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) { append_to_expression(" - "); trigger_key_flash("-"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply)) { append_to_expression(" * "); trigger_key_flash("*"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide)) { append_to_expression(" / "); trigger_key_flash("/"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal)) { append_to_expression("."); trigger_key_flash("."); }
    else if (ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) { evaluate_current(); trigger_key_flash("="); }

    // Shift combinations
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_5)) { append_to_expression(" % "); trigger_key_flash("%"); }
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_6)) { append_to_expression(" ^ "); trigger_key_flash("^"); }
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_8)) { append_to_expression(" * "); trigger_key_flash("*"); }
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_9)) { append_to_expression("("); trigger_key_flash("("); }
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_0)) { append_to_expression(")"); trigger_key_flash(")"); }
    else if (shift && ImGui::IsKeyPressed(ImGuiKey_Equal)) { append_to_expression(" + "); trigger_key_flash("+"); }

    // Main Keyboard Operators & Punctuation
    else if (!shift && ImGui::IsKeyPressed(ImGuiKey_Equal)) { evaluate_current(); trigger_key_flash("="); }
    else if (!shift && ImGui::IsKeyPressed(ImGuiKey_Minus)) { append_to_expression(" - "); trigger_key_flash("-"); }
    else if (!shift && ImGui::IsKeyPressed(ImGuiKey_Slash)) { append_to_expression(" / "); trigger_key_flash("/"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Period) || ImGui::IsKeyPressed(ImGuiKey_Comma)) { append_to_expression("."); trigger_key_flash("."); }

    // Actions & Navigation
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) { evaluate_current(); trigger_key_flash("="); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) { backspace_expression(); trigger_key_flash("⌫"); }
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_C)) { clear_all(); trigger_key_flash("C"); }

    // History navigation with Arrow keys
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (!history_.empty()) {
            if (history_index_ < 0) {
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                history_index_--;
            }
            if (history_index_ >= 0 && history_index_ < static_cast<int>(history_.size())) {
                display_expr_ = history_[static_cast<size_t>(history_index_)].expression;
                newly_evaluated_ = false;
                error_message_.clear();
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (!history_.empty() && history_index_ >= 0) {
            history_index_++;
            if (history_index_ >= static_cast<int>(history_.size())) {
                history_index_ = -1;
                display_expr_ = "0";
            } else {
                display_expr_ = history_[static_cast<size_t>(history_index_)].expression;
            }
            newly_evaluated_ = false;
            error_message_.clear();
        }
    }
}

void calculator::render_display_panel() {
    float const avail_width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.55f, 0.90f, 0.4f));

    if (ImGui::BeginChild("calc_display", ImVec2(avail_width, 82.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        // Status & preview line
        if (memory_value_ != 0.0) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.5f, 1.0f), "M (%.4g)", memory_value_);
            ImGui::SameLine();
        }

        if (!error_message_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error_message_.c_str());
        } else if (newly_evaluated_) {
            ImGui::TextDisabled("= %s", evaluated_result_.c_str());
        } else {
            auto [preview_res, preview_err] = evaluate(display_expr_, ans_value_);
            if (preview_err.empty() && display_expr_ != preview_res && !display_expr_.empty()) {
                ImGui::TextDisabled("= %s", preview_res.c_str());
            } else {
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            }
        }

        // Display expression text right-aligned
        ImGui::SetWindowFontScale(1.35f);
        float const text_width = ImGui::CalcTextSize(display_expr_.c_str()).x;
        if (text_width < avail_width - 20.0f) {
            ImGui::SetCursorPosX(avail_width - text_width - 15.0f);
        }
        ImGui::TextUnformatted(display_expr_.c_str());
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void calculator::render_mode_bar() {
    ImGui::Spacing();
    float const avail_width = ImGui::GetContentRegionAvail().x;
    float const btn_w = (avail_width - 24.0f) / 4.0f;

    if (ImGui::Button(show_scientific_ ? "[Sci]" : "Sci", ImVec2(btn_w, 26.0f))) {
        show_scientific_ = !show_scientific_;
    }
    ImGui::SameLine();
    std::string const hist_label = std::format("Hist ({})", history_.size());
    if (ImGui::Button(show_history_ ? "[Hist]" : hist_label.c_str(), ImVec2(btn_w, 26.0f))) {
        show_history_ = !show_history_;
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("AC", ImVec2(btn_w, 26.0f)) || is_key_flashed("C")) {
        clear_all();
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();

    if (ImGui::Button("⌫", ImVec2(btn_w, 26.0f)) || is_key_flashed("⌫")) {
        backspace_expression();
    }
    ImGui::Spacing();
}

void calculator::render_buttons_grid() {
    float const avail_width = ImGui::GetContentRegionAvail().x;
    int const cols = show_scientific_ ? 5 : 4;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float btn_w = (avail_width - spacing * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    float btn_h = show_scientific_ ? 32.0f : 40.0f;

    ImVec4 const num_bg{0.18f, 0.22f, 0.28f, 0.9f};
    ImVec4 const num_text{1.0f, 1.0f, 1.0f, 1.0f};

    ImVec4 const op_bg{0.12f, 0.35f, 0.50f, 0.9f};
    ImVec4 const op_text{0.4f, 0.85f, 1.0f, 1.0f};

    ImVec4 const eq_bg{0.20f, 0.55f, 0.90f, 1.0f};
    ImVec4 const eq_text{1.0f, 1.0f, 1.0f, 1.0f};

    ImVec4 const sci_bg{0.14f, 0.16f, 0.20f, 0.9f};
    ImVec4 const sci_text{0.8f, 0.8f, 0.9f, 1.0f};

    auto make_btn = [&](const char* label, std::string_view key_id, std::string_view append_str, const ImVec4& bg, const ImVec4& txt, float width_mult = 1.0f) {
        ImVec2 const size(btn_w * width_mult + (width_mult > 1.0f ? spacing * (width_mult - 1.0f) : 0.0f), btn_h);
        bool const flashed = is_key_flashed(key_id);

        ImGui::PushStyleColor(ImGuiCol_Button, flashed ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bg.x * 1.25f, bg.y * 1.25f, bg.z * 1.25f, bg.w));
        ImGui::PushStyleColor(ImGuiCol_Text, txt);

        if (ImGui::Button(label, size)) {
            if (!append_str.empty()) {
                append_to_expression(append_str);
            }
            trigger_key_flash(key_id);
        }

        ImGui::PopStyleColor(3);
    };

    if (show_scientific_) {
        // Sci Row 1
        make_btn("sin", "sin", "sin(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("cos", "cos", "cos(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("tan", "tan", "tan(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("√", "sqrt", "sqrt(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("^", "^", " ^ ", op_bg, op_text);

        // Sci Row 2
        make_btn("log", "log", "log(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("ln", "ln", "ln(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("π", "pi", "pi", sci_bg, sci_text); ImGui::SameLine();
        make_btn("e", "e", "e", sci_bg, sci_text); ImGui::SameLine();
        make_btn("Ans", "ans", "ans", sci_bg, sci_text);

        // Sci Row 3 (Memory & Modulo)
        if (ImGui::Button("MC", ImVec2(btn_w, btn_h))) { memory_value_ = 0.0; } ImGui::SameLine();
        if (ImGui::Button("MR", ImVec2(btn_w, btn_h))) { append_to_expression(format_number(memory_value_)); } ImGui::SameLine();
        if (ImGui::Button("M+", ImVec2(btn_w, btn_h))) { if (!evaluated_result_.empty()) try { memory_value_ += std::stod(evaluated_result_); } catch (...) { /* ignore invalid number */ } } ImGui::SameLine();
        if (ImGui::Button("M-", ImVec2(btn_w, btn_h))) { if (!evaluated_result_.empty()) try { memory_value_ -= std::stod(evaluated_result_); } catch (...) { /* ignore invalid number */ } } ImGui::SameLine();
        make_btn("%", "%", " % ", op_bg, op_text);

        // Sci Main Row 1
        make_btn("(", "(", "(", sci_bg, sci_text); ImGui::SameLine();
        make_btn(")", ")", ")", sci_bg, sci_text); ImGui::SameLine();
        make_btn("x²", "sqr", "^2", sci_bg, sci_text); ImGui::SameLine();
        make_btn("x!", "fact", "!", sci_bg, sci_text); ImGui::SameLine();
        make_btn("÷", "/", " / ", op_bg, op_text);

        // Sci Main Row 2
        make_btn("7", "7", "7", num_bg, num_text); ImGui::SameLine();
        make_btn("8", "8", "8", num_bg, num_text); ImGui::SameLine();
        make_btn("9", "9", "9", num_bg, num_text); ImGui::SameLine();
        make_btn("abs", "abs", "abs(", sci_bg, sci_text); ImGui::SameLine();
        make_btn("×", "*", " * ", op_bg, op_text);

        // Sci Main Row 3
        make_btn("4", "4", "4", num_bg, num_text); ImGui::SameLine();
        make_btn("5", "5", "5", num_bg, num_text); ImGui::SameLine();
        make_btn("6", "6", "6", num_bg, num_text); ImGui::SameLine();
        make_btn("1/x", "inv", "^(-1)", sci_bg, sci_text); ImGui::SameLine();
        make_btn("-", "-", " - ", op_bg, op_text);

        // Sci Main Row 4
        make_btn("1", "1", "1", num_bg, num_text); ImGui::SameLine();
        make_btn("2", "2", "2", num_bg, num_text); ImGui::SameLine();
        make_btn("3", "3", "3", num_bg, num_text); ImGui::SameLine();
        ImVec2 const pm_size(btn_w, btn_h);
        if (ImGui::Button("±", pm_size)) { negate_current(); } ImGui::SameLine();
        make_btn("+", "+", " + ", op_bg, op_text);

        // Sci Main Row 5
        make_btn("0", "0", "0", num_bg, num_text); ImGui::SameLine();
        make_btn(".", ".", ".", num_bg, num_text); ImGui::SameLine();

        ImVec2 const eq_size(btn_w * 3.0f + spacing * 2.0f, btn_h);
        bool const eq_flashed = is_key_flashed("=");
        ImGui::PushStyleColor(ImGuiCol_Button, eq_flashed ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : eq_bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, eq_text);
        if (ImGui::Button("=", eq_size)) {
            evaluate_current();
            trigger_key_flash("=");
        }
        ImGui::PopStyleColor(3);
    } else {
        // Normal 4-Column Grid
        // Row 1
        make_btn("(", "(", "(", sci_bg, sci_text); ImGui::SameLine();
        make_btn(")", ")", ")", sci_bg, sci_text); ImGui::SameLine();
        make_btn("%", "%", " % ", op_bg, op_text); ImGui::SameLine();
        make_btn("÷", "/", " / ", op_bg, op_text);

        // Row 2
        make_btn("7", "7", "7", num_bg, num_text); ImGui::SameLine();
        make_btn("8", "8", "8", num_bg, num_text); ImGui::SameLine();
        make_btn("9", "9", "9", num_bg, num_text); ImGui::SameLine();
        make_btn("×", "*", " * ", op_bg, op_text);

        // Row 3
        make_btn("4", "4", "4", num_bg, num_text); ImGui::SameLine();
        make_btn("5", "5", "5", num_bg, num_text); ImGui::SameLine();
        make_btn("6", "6", "6", num_bg, num_text); ImGui::SameLine();
        make_btn("-", "-", " - ", op_bg, op_text);

        // Row 4
        make_btn("1", "1", "1", num_bg, num_text); ImGui::SameLine();
        make_btn("2", "2", "2", num_bg, num_text); ImGui::SameLine();
        make_btn("3", "3", "3", num_bg, num_text); ImGui::SameLine();
        make_btn("+", "+", " + ", op_bg, op_text);

        // Row 5
        ImVec2 const pm_size(btn_w, btn_h);
        if (ImGui::Button("±", pm_size)) {
            negate_current();
        }
        ImGui::SameLine();

        make_btn("0", "0", "0", num_bg, num_text); ImGui::SameLine();
        make_btn(".", ".", ".", num_bg, num_text); ImGui::SameLine();

        ImVec2 const eq_size(btn_w, btn_h);
        bool const eq_flashed = is_key_flashed("=");
        ImGui::PushStyleColor(ImGuiCol_Button, eq_flashed ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : eq_bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, eq_text);
        if (ImGui::Button("=", eq_size)) {
            evaluate_current();
            trigger_key_flash("=");
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Keyboard: Type numbers/ops, Numpad, or [↑/↓] History");
}

void calculator::render_history_panel() {
    float const avail_width = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("Calculation History (%zu entries)", history_.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear History")) {
        history_.clear();
        history_index_ = -1;
    }

    ImGui::Separator();

    ImGui::BeginChild("history_list", ImVec2(avail_width, 220.0f), true);
    if (history_.empty()) {
        ImGui::TextDisabled("No calculation history yet.");
    } else {
        for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
            const auto& item = history_[static_cast<size_t>(i)];
            ImGui::PushID(i);
            std::string const label = std::format("{} = {}", item.expression, item.result);
            if (ImGui::Selectable(label.c_str())) {
                display_expr_ = item.expression;
                evaluated_result_ = item.result;
                newly_evaluated_ = false;
                error_message_.clear();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to load equation into calculator");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

bool calculator::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        handle_keyboard_input();

        render_display_panel();
        render_mode_bar();
        if (show_history_) {
            render_history_panel();
        } else {
            render_buttons_grid();
        }
    });
}

bool calculator::render() {
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
}

std::string calculator::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::string upper_text;
    if (!error_message_.empty()) {
        upper_text = "Error: " + error_message_;
    } else if (!history_.empty()) {
        upper_text = history_.back().expression + " =";
    } else if (newly_evaluated_ && !evaluated_result_.empty()) {
        upper_text = display_expr_ + " =";
    } else {
        upper_text = display_expr_ + " =";
    }

    std::string main_text = !error_message_.empty() ? error_message_ : display_expr_;

    std::vector<glz::json_t> body;

    // 1. Display Container (emphasis style)
    glz::json_t display_container;
    display_container["type"] = "Container";
    display_container["style"] = "emphasis";

    std::vector<glz::json_t> display_items;
    glz::json_t upper_tb;
    upper_tb["type"] = "TextBlock";
    upper_tb["text"] = upper_text;
    upper_tb["isSubtle"] = true;
    upper_tb["horizontalAlignment"] = "Right";
    upper_tb["size"] = "Small";
    display_items.push_back(std::move(upper_tb));

    glz::json_t main_tb;
    main_tb["type"] = "TextBlock";
    main_tb["text"] = main_text;
    main_tb["weight"] = "Bolder";
    main_tb["size"] = "ExtraLarge";
    main_tb["horizontalAlignment"] = "Right";
    main_tb["color"] = !error_message_.empty() ? "Attention" : "Accent";
    display_items.push_back(std::move(main_tb));

    display_container["items"] = std::move(display_items);
    body.push_back(std::move(display_container));

    // 2. Expression Input Container
    glz::json_t input_container;
    input_container["type"] = "Container";

    std::vector<glz::json_t> input_items;
    glz::json_t input_text;
    input_text["type"] = "Input.Text";
    input_text["id"] = "direct_expr";
    input_text["placeholder"] = "Type math expression (e.g. sqrt(144) + 2^8)...";
    input_items.push_back(std::move(input_text));

    input_container["items"] = std::move(input_items);
    body.push_back(std::move(input_container));

    // 3. FactSet
    glz::json_t fact_set;
    fact_set["type"] = "FactSet";

    std::vector<glz::json_t> facts;
    facts.push_back(glz::json_t::object_t{
        {"title", "Memory (M):"},
        {"value", std::format("{:.2f}", memory_value_)}
    });
    facts.push_back(glz::json_t::object_t{
        {"title", "Mode:"},
        {"value", "Floating-point / Radians"}
    });

    std::string last_eval = !history_.empty()
        ? (history_.back().expression + " = " + history_.back().result)
        : "None";
    facts.push_back(glz::json_t::object_t{
        {"title", "Last Eval:"},
        {"value", last_eval}
    });

    fact_set["facts"] = std::move(facts);
    body.push_back(std::move(fact_set));

    card["body"] = std::move(body);

    // 4. Actions
    std::vector<glz::json_t> actions;
    auto add_action = [&actions](const std::string& title, const std::string& verb) {
        glz::json_t a;
        a["type"] = "Action.Execute";
        a["title"] = title;
        a["verb"] = verb;
        actions.push_back(std::move(a));
    };

    add_action("Evaluate (=)", "calc_eval");
    add_action("Clear (C)", "calc_clear");
    add_action("Memory Recall (MR)", "calc_memory_recall");
    add_action("Memory Store (MS)", "calc_memory_store");

    card["actions"] = std::move(actions);

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void calculator::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

        auto err = glz::read_json(payload, std::string(action_json));
        if (!err && payload.is_object()) {
            if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                verb = payload["verb"].get<std::string>();
            } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                verb = payload["action"].get<std::string>();
            }
            if (payload.contains("data")) {
                data = payload["data"];
            } else {
                data = payload;
            }
        } else {
            verb = std::string(action_json);
        }

        if (verb == "calc_input") {
            std::string token;
            if (data.contains("token")) {
                if (data["token"].holds<std::string>()) {
                    token = data["token"].get<std::string>();
                } else if (data["token"].holds<double>()) {
                    token = format_number(data["token"].get<double>());
                }
            } else if (payload.contains("token")) {
                if (payload["token"].holds<std::string>()) {
                    token = payload["token"].get<std::string>();
                } else if (payload["token"].holds<double>()) {
                    token = format_number(payload["token"].get<double>());
                }
            }
            if (!token.empty()) {
                append_to_expression(token);
            }
        } else if (verb == "calc_eval" || verb == "eval" || verb == "=") {
            std::string direct_expr;
            if (data.contains("direct_expr") && data["direct_expr"].holds<std::string>()) {
                direct_expr = data["direct_expr"].get<std::string>();
            } else if (data.contains("expression") && data["expression"].holds<std::string>()) {
                direct_expr = data["expression"].get<std::string>();
            } else if (payload.contains("direct_expr") && payload["direct_expr"].holds<std::string>()) {
                direct_expr = payload["direct_expr"].get<std::string>();
            } else if (payload.contains("expression") && payload["expression"].holds<std::string>()) {
                direct_expr = payload["expression"].get<std::string>();
            }

            if (!direct_expr.empty()) {
                display_expr_ = direct_expr;
                newly_evaluated_ = false;
            }
            evaluate_current();
        } else if (verb == "calc_clear" || verb == "clear" || verb == "C") {
            clear_all();
        } else if (verb == "calc_memory_store" || verb == "memory_store" || verb == "MS") {
            try {
                if (data.contains("value")) {
                    if (data["value"].holds<double>()) {
                        memory_value_ = data["value"].get<double>();
                    } else if (data["value"].holds<std::string>()) {
                        memory_value_ = std::stod(data["value"].get<std::string>());
                    }
                } else if (!evaluated_result_.empty() && newly_evaluated_) {
                    memory_value_ = std::stod(evaluated_result_);
                } else {
                    auto [eval_res, eval_err] = evaluate(display_expr_, ans_value_);
                    if (eval_err.empty()) {
                        memory_value_ = std::stod(eval_res);
                    } else {
                        memory_value_ = std::stod(display_expr_);
                    }
                }
            } catch (...) {
            }
        } else if (verb == "calc_memory_recall" || verb == "memory_recall" || verb == "MR") {
            append_to_expression(format_number(memory_value_));
        } else if (verb == "calc_memory_clear" || verb == "MC") {
            memory_value_ = 0.0;
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
        }

        last_adaptive_parse_time_ = {};
    } catch (const std::exception& e) {
        std::cerr << "[calculator] Action error: " << e.what() << std::endl;
    } catch (...) {
    }
}

std::vector<card::mcp_function> calculator::get_mcp_functions() const {
    return {
        mcp_function{
            "calculate",
            "Evaluates a mathematical expression and returns the result or error.",
            R"({
                "type": "object",
                "properties": {
                    "expression": {
                        "type": "string",
                        "description": "Mathematical expression, e.g. '12.5 * (3 + 4)' or 'sqrt(144)' or 'sin(pi / 2)'"
                    }
                },
                "required": ["expression"]
            })",
            [](const std::string& params_json) -> std::string {
                std::string expr;
                try {
                    glz::json_t doc;
                    auto err = glz::read_json(doc, params_json);
                    if (!err && doc.contains("expression") && doc["expression"].is_string()) {
                        expr = doc["expression"].get<std::string>();
                    } else {
                        return R"({"error": "Invalid or missing 'expression' parameter"})";
                    }
                } catch (...) {
                    return R"({"error": "Failed to parse JSON parameters"})";
                }

                auto [res, err_msg] = calculator::evaluate(expr);
                glz::json_t response;
                if (!err_msg.empty()) {
                    response["error"] = err_msg;
                } else {
                    response["result"] = res;
                    try {
                        response["value"] = std::stod(res);
                    } catch (...) {
                        // Ignore conversion if result is non-numeric (e.g. error string)
                    }
                }
                std::string out;
                std::ignore = glz::write_json(response, out);
                return out;
            }
        }
    };
}

} // namespace rouen::cards
