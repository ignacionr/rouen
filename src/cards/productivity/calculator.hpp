#pragma once

#include <string>
#include <vector>
#include <memory>
#include <utility>

#include "../interface/card.hpp"

namespace rouen::cards {

struct history_entry {
    std::string expression;
    std::string result;
};

class calculator : public card {
public:
    calculator();
    ~calculator() override = default;

    bool render() override;
    [[nodiscard]] std::string get_uri() const override;

    std::vector<mcp_function> get_mcp_functions() const override;

    // Evaluates mathematical expression, returning (result_string, error_message)
    static std::pair<std::string, std::string> evaluate(const std::string& expr, double ans_val = 0.0);

private:
    void handle_keyboard_input();
    void append_to_expression(std::string_view str);
    void backspace_expression();
    void clear_all();
    void evaluate_current();
    void negate_current();
    void trigger_key_flash(std::string_view key_id);
    bool is_key_flashed(std::string_view key_id) const;

    void render_display_panel();
    void render_mode_bar();
    void render_buttons_grid();
    void render_history_panel();

    std::string display_expr_{"0"};
    std::string evaluated_result_{""};
    std::string error_message_{""};
    bool newly_evaluated_{false};
    double ans_value_{0.0};
    double memory_value_{0.0};

    bool show_scientific_{false};
    bool show_history_{false};

    std::vector<history_entry> history_;
    int history_index_{-1};

    std::string last_flashed_key_{""};
    float flash_timer_{0.0f};
};

} // namespace rouen::cards
