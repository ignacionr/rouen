#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstring>
#include <format>
#include "../interface/card.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

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

    bool render(rouen::ui::ui_context& ui) override {
        if (!is_open) return false;

        return render_window([this, &ui]() {
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
};

} // namespace rouen::cards
