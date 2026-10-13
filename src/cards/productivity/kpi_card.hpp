#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstring>
#include <format>
#include "../interface/card.hpp"
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

struct kpi_entry {
    std::string id;
    std::string name;
    double current_val{0.0};
    double target_val{0.0};
    std::string unit;
    std::string choice_title;
    std::string status{"Healthy"};
    bool loggable{true};
};

inline std::string format_with_commas(int64_t val) {
    std::string s = std::to_string(val);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(static_cast<size_t>(n), ",");
        n -= 3;
    }
    return s;
}

inline std::string format_kpi_fact_value(const kpi_entry& kpi) {
    if (kpi.id == "mrr") {
        int pct = kpi.target_val > 0.0 ? static_cast<int>((kpi.current_val / kpi.target_val) * 100.0) : 0;
        return std::format("${} / ${} ({}% Target)",
                           format_with_commas(static_cast<int64_t>(kpi.current_val)),
                           format_with_commas(static_cast<int64_t>(kpi.target_val)),
                           pct);
    }
    if (kpi.id == "uptime") {
        return std::format("{:.2f}% (Target: >{:.1f}%)", kpi.current_val, kpi.target_val);
    }
    if (kpi.id == "build_time") {
        return std::format("{:.1f}s (Target: <{:.0f}s)", kpi.current_val, kpi.target_val);
    }
    if (kpi.id == "open_issues" || kpi.id == "backlog") {
        std::string status = kpi.current_val <= kpi.target_val ? "Healthy" : "Attention";
        return std::format("{:.0f} tickets ({})", kpi.current_val, status);
    }
    if (kpi.target_val > 0.0) {
        int pct = static_cast<int>((kpi.current_val / kpi.target_val) * 100.0);
        if (kpi.unit == "%") {
            return std::format("{:.1f}% (Target: {:.1f}%)", kpi.current_val, kpi.target_val);
        }
        return std::format("{:.0f} / {:.0f} {} ({}% Target)",
                           kpi.current_val, kpi.target_val, kpi.unit, pct);
    }
    return std::format("{:.1f} {}", kpi.current_val, kpi.unit);
}

struct kpi_optimization {
    std::string description;
    std::string impact;
    std::string date;
};

struct kpi_invoice_state {
    bool invoice_sent = false;
    bool banking_cleared = false;
    bool tax_filed = false;
};

struct kpi_goals_checklist {
    // Goal A targets
    bool a_std_lib = false;
    bool a_optimization = false;
    bool a_simplification = false;
    bool a_cpp23 = false;
    
    // Goal B targets
    bool b_async = false;
    bool b_self_documenting = false;
    bool b_no_meetings = false;
    bool b_timezone = false;
    
    // Goal C targets
    bool c_ie_structure = false;
    bool c_banking_paperwork = false;
    bool c_invoices_on_time = false;
    bool c_tax_statements = false;
};

struct kpi_state {
    int dependency_count = 0;
    int added_lines = 0;
    int deleted_lines = 0;
    std::vector<kpi_optimization> optimizations;
    bool ops_timezone_compliance = true;
    bool ops_async_alignment = true;
    std::map<std::string, kpi_invoice_state> invoices = {
        {"July", {}},
        {"August", {}},
        {"September", {}}
    };
    kpi_goals_checklist goals_checklist{};
};

struct kpi_card : public card {
    kpi_state state;
    bool is_open = true;
    int selected_tab = 0; // 0 = Overview & Goals, 1 = Tech KPIs, 2 = Ops & Admin
    
    // Add temp variables
    char new_opt_desc[128] = "";
    char new_opt_impact[64] = "";
    char new_opt_date[32] = "";

    kpi_card() {
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};     // Blue primary
        colors[1] = {0.18f, 0.80f, 0.44f, 0.7f};     // Emerald
        colors[2] = {0.11f, 0.63f, 0.94f, 0.7f};     // Cyan
        
        name("Sovereign KPIs");
        width = 520.0f;
        
        // Default optimization date to today
        std::time_t t = std::time(nullptr);
        std::strftime(new_opt_date, sizeof(new_opt_date), "%Y-%m-%d", std::localtime(&t));
        
        // Initialize default strategic KPIs
        kpi_metrics = {
            kpi_entry{
                .id = "mrr",
                .name = "Monthly Recurring Revenue",
                .current_val = 14250.0,
                .target_val = 20000.0,
                .unit = "USD",
                .choice_title = "Monthly Recurring Revenue ($)",
                .status = "Good",
                .loggable = true
            },
            kpi_entry{
                .id = "uptime",
                .name = "Rouen Mesh Uptime",
                .current_val = 99.98,
                .target_val = 99.9,
                .unit = "%",
                .choice_title = "Rouen Mesh Uptime (%)",
                .status = "Good",
                .loggable = true
            },
            kpi_entry{
                .id = "build_time",
                .name = "C++ Module Compilation Time",
                .current_val = 12.4,
                .target_val = 15.0,
                .unit = "s",
                .choice_title = "Build Time (seconds)",
                .status = "Good",
                .loggable = true
            },
            kpi_entry{
                .id = "open_issues",
                .name = "Open Issues Backlog",
                .current_val = 8.0,
                .target_val = 15.0,
                .unit = "tickets",
                .choice_title = "Open Issues Backlog (tickets)",
                .status = "Healthy",
                .loggable = false
            }
        };

        load_state();
    }

    void load_state() {
        try {
            auto path = platform::get_user_data_path("kpis.json", false);
            if (std::filesystem::exists(path)) {
                std::string content;
                std::ifstream f(path);
                if (f) {
                    content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    auto err = glz::read_json(state, content);
                    if (err) {
                        // Handle error or log in debug
                    }
                }
            }
        } catch (...) {
            // Ignore load errors, fallback to default state
        }
    }

    void save_state() {
        try {
            auto path = platform::get_user_data_path("kpis.json", true);
            std::string content = glz::write_json(state).value_or("");
            if (!content.empty()) {
                std::ofstream f(path);
                if (f) {
                    f << content;
                }
            }
        } catch (...) {
            // Ignore save errors
        }
    }

    std::string get_uri() const override {
        return "kpis";
    }

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        if (!is_open) return false;

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

            // Title & Subtitle
            ui.text_colored(colors[0], "Sovereign Engineering Blueprint");
            ui.text("Quarterly Goals & Personal KPIs");
            ui.separator();

            // Tab switcher
            if (ui.selectable(ICON_MD_DASHBOARD " Overview & Goals", selected_tab == 0)) { selected_tab = 0; }
            ui.same_line();
            if (ui.selectable(ICON_MD_MEMORY " Technical KPIs", selected_tab == 1)) { selected_tab = 1; }
            ui.same_line();
            if (ui.selectable(ICON_MD_BUSINESS " Ops & Admin", selected_tab == 2)) { selected_tab = 2; }
            ui.separator();

            if (selected_tab == 0) {
                render_overview(ui);
            } else if (selected_tab == 1) {
                render_tech_kpis(ui);
            } else {
                render_ops_kpis(ui);
            }

            ui.separator();
            if (ui.button(ICON_MD_SAVE " Save Progress")) {
                save_state();
            }
            ui.same_line();
            if (ui.button("Close")) {
                is_open = false;
            }
        });
    }

    void render_overview(rouen::ui::ui_context& ui) {
        // Setup Grid
        ui.text_colored(colors[0], "Operational Setup");
        
        ui.text(ICON_MD_WORK " Engagement Model: Raptor B2B Strategy");
        ui.text(ICON_MD_PERCENT " Tax Structure: Georgian IE (1% optimized)");
        ui.text(ICON_MD_FLIGHT " Travel & Rotation: 3-3-3-3 travel model");
        ui.spacing();
        ui.separator();

        // Goals Breakdown
        ui.text_colored(colors[0], "Sovereign Goals Checklist");
        ui.spacing();

        // Goal A
        float progress_a = 0.0f;
        if (state.goals_checklist.a_std_lib) progress_a += 0.25f;
        if (state.goals_checklist.a_optimization) progress_a += 0.25f;
        if (state.goals_checklist.a_simplification) progress_a += 0.25f;
        if (state.goals_checklist.a_cpp23) progress_a += 0.25f;

        ui.text_colored(colors[1], "GOAL A: Modernization & Minimalist Design");
        ui.progress_bar(progress_a, ImVec2(-1.0f, 15.0f), std::format("{:.0f}%", progress_a * 100.0f));
        ui.indent(15.0f);
        ui.checkbox("Modern C++ standard library priority (no new bloat)##a1", &state.goals_checklist.a_std_lib);
        ui.checkbox("Hot path latency / memory optimization completed##a2", &state.goals_checklist.a_optimization);
        ui.checkbox("Code simplification (deleted > added) achieved##a3", &state.goals_checklist.a_simplification);
        ui.checkbox("Standard C++23 standards leveraged##a4", &state.goals_checklist.a_cpp23);
        ui.unindent(15.0f);
        ui.spacing();

        // Goal B
        float progress_b = 0.0f;
        if (state.goals_checklist.b_async) progress_b += 0.25f;
        if (state.goals_checklist.b_self_documenting) progress_b += 0.25f;
        if (state.goals_checklist.b_no_meetings) progress_b += 0.25f;
        if (state.goals_checklist.b_timezone) progress_b += 0.25f;

        ui.text_colored(colors[2], "GOAL B: Operational Independence & Autonomy");
        ui.progress_bar(progress_b, ImVec2(-1.0f, 15.0f), std::format("{:.0f}%", progress_b * 100.0f));
        ui.indent(15.0f);
        ui.checkbox("Asynchronous status updates (minimized sync alignment)##b1", &state.goals_checklist.b_async);
        ui.checkbox("Self-documenting technical designs and comments##b2", &state.goals_checklist.b_self_documenting);
        ui.checkbox("Autonomy maintained (no micro-management meetings)##b3", &state.goals_checklist.b_no_meetings);
        ui.checkbox("Consistent timezone compliance maintained on rotation##b4", &state.goals_checklist.b_timezone);
        ui.unindent(15.0f);
        ui.spacing();

        // Goal C
        float progress_c = 0.0f;
        if (state.goals_checklist.c_ie_structure) progress_c += 0.25f;
        if (state.goals_checklist.c_banking_paperwork) progress_c += 0.25f;
        if (state.goals_checklist.c_invoices_on_time) progress_c += 0.25f;
        if (state.goals_checklist.c_tax_statements) progress_c += 0.25f;

        ui.text_colored(colors[0], "GOAL C: Flawless Invoicing & Banking Trail");
        ui.progress_bar(progress_c, ImVec2(-1.0f, 15.0f), std::format("{:.0f}%", progress_c * 100.0f));
        ui.indent(15.0f);
        ui.checkbox("Georgian IE structure compliant (1% tax rate secured)##c1", &state.goals_checklist.c_ie_structure);
        ui.checkbox("Banking paperwork for international transfers finalized##c2", &state.goals_checklist.c_banking_paperwork);
        ui.checkbox("Compliant invoices sent on-time monthly##c3", &state.goals_checklist.c_invoices_on_time);
        ui.checkbox("Clean ledger and Georgian tax statements filed##c4", &state.goals_checklist.c_tax_statements);
        ui.unindent(15.0f);
    }

    void render_tech_kpis(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[0], "Technical Metrics");
        ui.spacing();

        // Dependency count
        ui.text(ICON_MD_EXTENSION " Dependency Count (Target: 0 New Libs)");
        ui.push_item_width(120.0f);
        if (ui.input_int("Added Dependencies##dep_count", &state.dependency_count)) {
            if (state.dependency_count < 0) state.dependency_count = 0;
        }
        ui.pop_item_width();
        if (state.dependency_count == 0) {
            ui.text_colored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), ICON_MD_CHECK_CIRCLE " On track (0 new dependencies)");
        } else {
            ui.text_colored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), ICON_MD_WARNING " Warning: New dependency added");
        }
        ui.spacing();
        ui.separator();

        // Refactoring debt
        ui.text(ICON_MD_DELETE " Refactoring Debt (Target: Simplified > Added)");
        ui.push_item_width(120.0f);
        ui.input_int("Added Lines##added", &state.added_lines);
        ui.input_int("Deleted/Simplified Lines##deleted", &state.deleted_lines);
        ui.pop_item_width();
        
        int net_lines = state.added_lines - state.deleted_lines;
        if (net_lines <= 0) {
            ui.text_colored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), std::format(ICON_MD_CHECK_CIRCLE " Codebase shrunk by {} lines!", -net_lines));
        } else {
            ui.text_colored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), std::format(ICON_MD_INFO " Net growth: +{} lines", net_lines));
        }
        ui.spacing();
        ui.separator();

        // Resource Optimizations
        ui.text(ICON_MD_MEMORY " Resource Optimizations Log");
        
        for (const auto& opt : state.optimizations) {
            ui.text_colored(colors[1], std::format("* {} ({})", opt.description, opt.impact));
            ui.indent(15.0f);
            ui.text(std::format("Logged on: {}", opt.date));
            ui.unindent(15.0f);
        }
        
        if (state.optimizations.empty()) {
            ui.text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No optimizations logged yet this quarter.");
        }
        
        ui.spacing();
        ui.text_colored(colors[0], "Add Optimization");
        ui.input_text("Description##opt_desc", new_opt_desc, sizeof(new_opt_desc));
        ui.input_text("Impact (CPU/RAM)##opt_impact", new_opt_impact, sizeof(new_opt_impact));
        ui.input_text("Date (YYYY-MM-DD)##opt_date", new_opt_date, sizeof(new_opt_date));
        
        if (ui.button("Log Optimization")) {
            if (strlen(new_opt_desc) > 0 && strlen(new_opt_impact) > 0) {
                state.optimizations.push_back({new_opt_desc, new_opt_impact, new_opt_date});
                new_opt_desc[0] = '\0';
                new_opt_impact[0] = '\0';
            }
        }
    }

    void render_ops_kpis(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[0], "Operational & Lifestyle Metrics");
        ui.spacing();

        // Timezone compliance & Autonomy
        ui.checkbox(ICON_MD_PUBLIC " 100% Timezone Output Compliance", &state.ops_timezone_compliance);
        ui.checkbox(ICON_MD_HOURGLASS_EMPTY " Asynchronous Alignment First (Min sync calls)", &state.ops_async_alignment);
        ui.spacing();
        ui.separator();

        // Invoicing & Administration
        ui.text(ICON_MD_RECEIPT " Invoicing & Banking Trail");
        
        for (auto& [month, data] : state.invoices) {
            ui.text_colored(colors[0], month);
            ui.indent(15.0f);
            ui.checkbox("Invoice Sent##" + month, &data.invoice_sent);
            ui.same_line(150.0f);
            ui.checkbox("Banking Cleared##" + month, &data.banking_cleared);
            ui.same_line(300.0f);
            ui.checkbox("Tax Filed (1%)##" + month, &data.tax_filed);
            ui.unindent(15.0f);
            ui.spacing();
        }
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
        card["refreshIntervalMs"] = 30000;

        std::vector<glz::json_t> body;

        // 1. Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Emoji icon
            {
                glz::json_t col1;
                col1["type"] = "Column";
                col1["width"] = "auto";
                std::vector<glz::json_t> col1_items;
                glz::json_t tb_icon;
                tb_icon["type"] = "TextBlock";
                tb_icon["text"] = "🎯";
                tb_icon["size"] = "Large";
                col1_items.push_back(std::move(tb_icon));
                col1["items"] = std::move(col1_items);
                columns.push_back(std::move(col1));
            }

            // Column 2: Title & subtitle
            {
                glz::json_t col2;
                col2["type"] = "Column";
                col2["width"] = "stretch";
                std::vector<glz::json_t> col2_items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = "Sovereign KPI Command Center";
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                col2_items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "Strategic performance targets and live metrics";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                col2_items.push_back(std::move(tb_sub));

                col2["items"] = std::move(col2_items);
                columns.push_back(std::move(col2));
            }

            // Column 3: Active count badge
            {
                glz::json_t col3;
                col3["type"] = "Column";
                col3["width"] = "auto";
                std::vector<glz::json_t> col3_items;

                glz::json_t tb_badge;
                tb_badge["type"] = "TextBlock";
                tb_badge["text"] = std::format("{} ACTIVE", kpi_metrics.size());
                tb_badge["weight"] = "Bolder";
                tb_badge["color"] = "Good";
                tb_badge["size"] = "Medium";
                col3_items.push_back(std::move(tb_badge));

                col3["items"] = std::move(col3_items);
                columns.push_back(std::move(col3));
            }

            col_set["columns"] = std::move(columns);
            std::vector<glz::json_t> header_items;
            header_items.push_back(std::move(col_set));
            header_container["items"] = std::move(header_items);
            body.push_back(std::move(header_container));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            for (const auto& kpi : kpi_metrics) {
                facts.push_back(glz::json_t::object_t{
                    {"title", kpi.name + ":"},
                    {"value", format_kpi_fact_value(kpi)}
                });
            }

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Log Metric Progress Input Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            input_container["separator"] = true;
            std::vector<glz::json_t> input_items;

            glz::json_t tb_log;
            tb_log["type"] = "TextBlock";
            tb_log["text"] = "Log Metric Progress";
            tb_log["weight"] = "Bolder";
            input_items.push_back(std::move(tb_log));

            // ChoiceSet for metric selection
            glz::json_t choice_set;
            choice_set["type"] = "Input.ChoiceSet";
            choice_set["id"] = "kpi_id";
            choice_set["label"] = "Metric";
            choice_set["title"] = "Metric";
            choice_set["value"] = selected_kpi_id.empty() ? "mrr" : selected_kpi_id;
            std::vector<glz::json_t> choices;

            for (const auto& kpi : kpi_metrics) {
                if (kpi.loggable && !kpi.choice_title.empty()) {
                    choices.push_back(glz::json_t::object_t{
                        {"title", kpi.choice_title},
                        {"value", kpi.id}
                    });
                }
            }
            choice_set["choices"] = std::move(choices);
            input_items.push_back(std::move(choice_set));

            // Input.Number for new measurement value
            glz::json_t num_input;
            num_input["type"] = "Input.Number";
            num_input["id"] = "new_value";
            num_input["label"] = "New Measurement Value";
            num_input["title"] = "New Measurement Value";
            num_input["value"] = static_cast<int64_t>(input_new_value_);
            input_items.push_back(std::move(num_input));

            input_container["items"] = std::move(input_items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_update;
            act_update["type"] = "Action.Execute";
            act_update["title"] = "Update Metric";
            act_update["verb"] = "update_kpi";
            actions.push_back(std::move(act_update));

            glz::json_t act_refresh;
            act_refresh["type"] = "Action.Execute";
            act_refresh["title"] = "Refresh KPIs";
            act_refresh["verb"] = "refresh_kpis";
            actions.push_back(std::move(act_refresh));
        }
        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
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
                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "update_kpi") {
                std::string kpi_id;
                double new_val = 0.0;
                bool has_val = false;

                if (data.is_object()) {
                    if (data.contains("kpi_id") && data["kpi_id"].holds<std::string>()) {
                        kpi_id = data["kpi_id"].get<std::string>();
                    } else if (data.contains("id") && data["id"].holds<std::string>()) {
                        kpi_id = data["id"].get<std::string>();
                    }

                    if (data.contains("new_value")) {
                        if (data["new_value"].holds<double>()) {
                            new_val = data["new_value"].get<double>();
                            has_val = true;
                        } else if (data["new_value"].holds<std::string>()) {
                            try {
                                new_val = std::stod(data["new_value"].get<std::string>());
                                has_val = true;
                            } catch (...) {}
                        }
                    } else if (data.contains("value")) {
                        if (data["value"].holds<double>()) {
                            new_val = data["value"].get<double>();
                            has_val = true;
                        } else if (data["value"].holds<std::string>()) {
                            try {
                                new_val = std::stod(data["value"].get<std::string>());
                                has_val = true;
                            } catch (...) {}
                        }
                    }
                }

                if (!kpi_id.empty() && has_val) {
                    for (auto& kpi : kpi_metrics) {
                        if (kpi.id == kpi_id || (kpi_id == "backlog" && kpi.id == "open_issues") || (kpi_id == "open_issues" && kpi.id == "backlog")) {
                            kpi.current_val = new_val;
                            break;
                        }
                    }
                    selected_kpi_id = kpi_id;
                    input_new_value_ = new_val;
                }
                last_action_ = "update_kpi";
                last_updated = std::chrono::system_clock::now();
                last_adaptive_parse_time_ = {};
            } else if (verb == "add_kpi") {
                std::string name;
                double target_val = 0.0;
                std::string unit;
                double current_val = 0.0;

                if (data.is_object()) {
                    if (data.contains("name") && data["name"].holds<std::string>()) {
                        name = data["name"].get<std::string>();
                    }
                    if (data.contains("target_val")) {
                        if (data["target_val"].holds<double>()) {
                            target_val = data["target_val"].get<double>();
                        } else if (data["target_val"].holds<std::string>()) {
                            try { target_val = std::stod(data["target_val"].get<std::string>()); } catch (...) {}
                        }
                    }
                    if (data.contains("unit") && data["unit"].holds<std::string>()) {
                        unit = data["unit"].get<std::string>();
                    }
                    if (data.contains("current_val")) {
                        if (data["current_val"].holds<double>()) {
                            current_val = data["current_val"].get<double>();
                        } else if (data["current_val"].holds<std::string>()) {
                            try { current_val = std::stod(data["current_val"].get<std::string>()); } catch (...) {}
                        }
                    }
                }

                if (!name.empty()) {
                    std::string id = data.contains("id") && data["id"].holds<std::string>() ? data["id"].get<std::string>() : "";
                    if (id.empty()) {
                        for (char c : name) {
                            if (std::isalnum(static_cast<unsigned char>(c))) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                            else if (c == ' ' || c == '-' || c == '_') id.push_back('_');
                        }
                    }
                    kpi_entry new_entry{
                        .id = id,
                        .name = name,
                        .current_val = current_val,
                        .target_val = target_val,
                        .unit = unit,
                        .choice_title = std::format("{} ({})", name, unit.empty() ? "units" : unit),
                        .status = "Good",
                        .loggable = true
                    };
                    kpi_metrics.push_back(std::move(new_entry));
                    selected_kpi_id = id;
                }
                last_action_ = "add_kpi";
                last_updated = std::chrono::system_clock::now();
                last_adaptive_parse_time_ = {};
            } else if (verb == "refresh_kpis") {
                for (auto& kpi : kpi_metrics) {
                    if (kpi.target_val > 0.0) {
                        double delta_pct = (kpi.current_val / kpi.target_val) * 100.0;
                        if (kpi.id == "build_time") {
                            kpi.status = kpi.current_val <= kpi.target_val ? "Good" : "Warning";
                        } else if (kpi.id == "open_issues" || kpi.id == "backlog") {
                            kpi.status = kpi.current_val <= kpi.target_val ? "Healthy" : "Attention";
                        } else {
                            kpi.status = delta_pct >= 70.0 ? "Good" : "Warning";
                        }
                    }
                }
                kpis_refreshed_ = true;
                last_action_ = "refresh_kpis";
                last_updated = std::chrono::system_clock::now();
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[kpis] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }

    [[nodiscard]] std::string get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::vector<kpi_entry>& get_kpi_metrics() const noexcept { return kpi_metrics; }
    [[nodiscard]] std::string get_selected_kpi_id() const noexcept { return selected_kpi_id; }
    [[nodiscard]] std::chrono::system_clock::time_point get_last_updated() const noexcept { return last_updated; }
    [[nodiscard]] bool is_kpis_refreshed() const noexcept { return kpis_refreshed_; }

    [[nodiscard]] const kpi_entry* find_kpi(std::string_view id) const {
        for (const auto& kpi : kpi_metrics) {
            if (kpi.id == id || (id == "backlog" && kpi.id == "open_issues") || (id == "open_issues" && kpi.id == "backlog")) {
                return &kpi;
            }
        }
        return nullptr;
    }

    [[nodiscard]] double get_kpi_value(std::string_view id) const {
        const auto* kpi = find_kpi(id);
        return kpi ? kpi->current_val : 0.0;
    }

    // State Synchronization
    std::vector<kpi_entry> kpi_metrics{};
    std::string selected_kpi_id{"mrr"};
    std::chrono::system_clock::time_point last_updated{std::chrono::system_clock::now()};

private:
    double input_new_value_{14500.0};
    std::string last_action_{};
    bool kpis_refreshed_{false};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
};

} // namespace rouen::cards
