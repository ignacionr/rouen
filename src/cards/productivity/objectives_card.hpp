#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <algorithm>
#include <numeric>
#include "../../helpers/imgui_include.hpp"
#include "../../models/productivity/objective_repository.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../interface/card.hpp"

namespace rouen::cards {

    class objectives_card : public card {
    public:
        objectives_card() {
            name("Objectives");
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Accent color
            colors[1] = {0.25f, 0.88f, 0.82f, 0.7f};  // Second accent
            colors[2] = {0.2f, 0.7f, 0.4f, 1.0f};   // Success green
            colors[3] = {0.9f, 0.3f, 0.3f, 1.0f};   // Fail red
            requested_fps = 30;
            
            repo_ = std::make_unique<models::productivity::objective_repository>();
            refresh_state();
        }

        ~objectives_card() override = default;

        bool render() override {
            // Check for Ctrl+E hotkey to toggle Evening Ledger
            if (ImGui::IsKeyPressed(ImGuiKey_E, false) && ImGui::GetIO().KeyCtrl) {
                if (state_ == ui_state::normal_active) {
                    state_ = ui_state::evening_ledger;
                    initialize_evening_ledger();
                } else if (state_ == ui_state::evening_ledger) {
                    state_ = ui_state::normal_active;
                }
            }

            return render_window([this]() {
                // Main layout rendering based on current state
                switch (state_) {
                    case ui_state::forgiveness_buffer:
                        render_forgiveness_buffer();
                        break;
                    case ui_state::temporal_setup:
                        render_temporal_setup();
                        break;
                    case ui_state::morning_staging:
                        render_morning_staging();
                        break;
                    case ui_state::normal_active:
                        render_normal_active();
                        break;
                    case ui_state::evening_ledger:
                        render_evening_ledger();
                        break;
                    case ui_state::day_completed:
                        render_day_completed();
                        break;
                }
            });
        }

        std::string get_uri() const override {
            return "objectives";
        }

    private:
        enum class ui_state {
            forgiveness_buffer,
            temporal_setup,
            morning_staging,
            normal_active,
            evening_ledger,
            day_completed
        };

        std::unique_ptr<models::productivity::objective_repository> repo_;
        ui_state state_{ui_state::morning_staging};
        models::productivity::date_context date_ctx_;
        
        // State data
        std::string unclosed_date_{""};
        std::string setup_period_{""}; // "quarterly", "monthly", "weekly"
        
        // Input buffers for setup screens
        char new_title_buf_[256]{'\0'};
        int selected_parent_idx_{0};
        int selected_type_idx_{0}; // 0 = binary, 1 = volumetric, 2 = constraint
        float input_target_val_{1.0f};

        // Cache lists for rendering
        std::vector<models::productivity::objective_record> quarterly_objs_;
        std::vector<models::productivity::objective_record> monthly_objs_;
        std::vector<models::productivity::objective_record> weekly_objs_;
        std::vector<models::productivity::objective_record> daily_objs_;

        // Staging items (unsaved daily objectives)
        std::vector<models::productivity::objective_record> staging_objs_;
        
        // Evening ledger selections: maps item ID to rollover option ("push", "drop")
        std::map<int, std::string> evening_rollovers_;

        // UI Collapsible control
        bool context_bar_expanded_{false};

        void refresh_state() {
            date_ctx_ = models::productivity::objective_repository::get_current_date_context();
            
            // Check if today is closed
            if (repo_->is_day_closed(date_ctx_.date)) {
                state_ = ui_state::day_completed;
                load_all_data();
                return;
            }

            // Check if there is an unclosed day before today
            unclosed_date_ = repo_->get_unclosed_day_before(date_ctx_.date);
            if (!unclosed_date_.empty()) {
                state_ = ui_state::forgiveness_buffer;
                load_all_data();
                return;
            }

            // Check if Quarterly goals exist
            quarterly_objs_ = repo_->get_objectives("quarterly", date_ctx_.quarter);
            if (quarterly_objs_.empty()) {
                state_ = ui_state::temporal_setup;
                setup_period_ = "quarterly";
                load_all_data();
                return;
            }

            // Check if Monthly goals exist
            monthly_objs_ = repo_->get_objectives("monthly", date_ctx_.month);
            if (monthly_objs_.empty()) {
                state_ = ui_state::temporal_setup;
                setup_period_ = "monthly";
                load_all_data();
                return;
            }

            // Check if Weekly goals exist
            weekly_objs_ = repo_->get_objectives("weekly", date_ctx_.week);
            if (weekly_objs_.empty()) {
                state_ = ui_state::temporal_setup;
                setup_period_ = "weekly";
                load_all_data();
                return;
            }

            // Check if daily goals are committed for today
            daily_objs_ = repo_->get_objectives("daily", date_ctx_.date);
            bool has_committed = false;
            for (const auto& item : daily_objs_) {
                if (item.status != "pending") {
                    has_committed = true;
                    break;
                }
            }

            if (has_committed) {
                state_ = ui_state::normal_active;
            } else {
                state_ = ui_state::morning_staging;
                // Seed staging list with any pending daily objectives carried over (rollovers)
                staging_objs_.clear();
                for (const auto& item : daily_objs_) {
                    if (item.status == "pending") {
                        staging_objs_.push_back(item);
                    }
                }
            }

            load_all_data();
        }

        void load_all_data() {
            quarterly_objs_ = repo_->get_objectives("quarterly", date_ctx_.quarter);
            monthly_objs_ = repo_->get_objectives("monthly", date_ctx_.month);
            weekly_objs_ = repo_->get_objectives("weekly", date_ctx_.week);
            daily_objs_ = repo_->get_objectives("daily", date_ctx_.date);
        }

        // Calculations for automatic progress bar percentages
        double get_objective_progress(const models::productivity::objective_record& obj) {
            if (obj.period == "daily") {
                if (obj.type == "binary") {
                    return obj.current_val >= 1.0 ? 1.0 : 0.0;
                } else if (obj.type == "volumetric") {
                    if (obj.target_val <= 0.0) return 0.0;
                    return std::clamp(obj.current_val / obj.target_val, 0.0, 1.0);
                } else if (obj.type == "constraint") {
                    return obj.current_val <= obj.target_val ? 1.0 : 0.0;
                }
                return 0.0;
            }

            // Find children progress
            std::vector<models::productivity::objective_record> children;
            if (obj.period == "weekly") {
                children = repo_->get_objectives("daily", date_ctx_.date);
            } else if (obj.period == "monthly") {
                children = repo_->get_objectives("weekly", date_ctx_.week);
            } else if (obj.period == "quarterly") {
                children = repo_->get_objectives("monthly", date_ctx_.month);
            }

            // Filter children belonging to this specific parent
            double sum_progress = 0.0;
            int count = 0;
            for (const auto& child : children) {
                if (child.parent_id == obj.id) {
                    sum_progress += get_objective_progress(child);
                    count++;
                }
            }

            if (count == 0) return 0.0;
            return sum_progress / count;
        }

        double get_period_average_progress(const std::string& period, const std::string& identifier) {
            auto list = repo_->get_objectives(period, identifier);
            if (list.empty()) return 0.0;
            double sum = 0.0;
            for (const auto& item : list) {
                sum += get_objective_progress(item);
            }
            return sum / static_cast<double>(list.size());
        }

        void initialize_evening_ledger() {
            evening_rollovers_.clear();
            for (const auto& item : daily_objs_) {
                double prog = get_objective_progress(item);
                if (prog < 1.0) {
                    evening_rollovers_[item.id] = "push"; // Default to rollover push
                }
            }
        }

        std::string format_objectives_markdown(const std::string& period_name, const std::string& period_id, const std::vector<models::productivity::objective_record>& objectives) {
            std::string result = std::format("## {} ({})\n\n", period_name, period_id);
            if (objectives.empty()) {
                result += "_No objectives set for this period_\n";
            } else {
                for (const auto& obj : objectives) {
                    double progress = get_objective_progress(obj);
                    std::string checkbox = (progress >= 1.0) ? "[x]" : "[ ]";
                    result += std::format("- {} {} (Progress: {:.0f}%)\n", checkbox, obj.title, progress * 100.0);
                }
            }
            return result;
        }

        // --- RENDER METHODS ---

        // 1. Context Bar at Top
        void render_context_bar() {
            double q_prog = get_period_average_progress("quarterly", date_ctx_.quarter);
            double m_prog = get_period_average_progress("monthly", date_ctx_.month);
            double w_prog = get_period_average_progress("weekly", date_ctx_.week);

            std::string label = std::format(
                ICON_MD_LAYERS " Context Bar | Q: {:.0f}% > M: {:.0f}% > W: {:.0f}%",
                q_prog * 100.0, m_prog * 100.0, w_prog * 100.0
            );

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                context_bar_expanded_ = !context_bar_expanded_;
            }
            ImGui::PopStyleColor();

            if (context_bar_expanded_) {
                ImGui::BeginChild("ContextDetails", ImVec2(0, 150.0f), true);
                ImGui::Columns(3, "context_cols", true);

                // Quarterly vision column
                ImGui::TextColored(colors[0], ICON_MD_FLAG " Quarterly (%s)", date_ctx_.quarter.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::SmallButton(ICON_MD_CONTENT_COPY "##copy_q")) {
                    std::string text = format_objectives_markdown("Quarterly Objectives", date_ctx_.quarter, quarterly_objs_);
                    ImGui::SetClipboardText(text.c_str());
                }
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (const auto& obj : quarterly_objs_) {
                    double prog = get_objective_progress(obj);
                    ImGui::TextWrapped("- %s", obj.title.c_str());
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[0]);
                    ImGui::ProgressBar(static_cast<float>(prog), ImVec2(-1.0f, 3.0f), "");
                    ImGui::PopStyleColor();
                }

                ImGui::NextColumn();

                // Monthly milestones column
                ImGui::TextColored(colors[1], ICON_MD_ASSIGNMENT " Monthly (%s)", date_ctx_.month.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::SmallButton(ICON_MD_CONTENT_COPY "##copy_m")) {
                    std::string text = format_objectives_markdown("Monthly Objectives", date_ctx_.month, monthly_objs_);
                    ImGui::SetClipboardText(text.c_str());
                }
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (const auto& obj : monthly_objs_) {
                    double prog = get_objective_progress(obj);
                    ImGui::TextWrapped("- %s", obj.title.c_str());
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[1]);
                    ImGui::ProgressBar(static_cast<float>(prog), ImVec2(-1.0f, 3.0f), "");
                    ImGui::PopStyleColor();
                }

                ImGui::NextColumn();

                // Weekly sprint column
                ImGui::TextColored(colors[2], ICON_MD_LIST " Weekly (%s)", date_ctx_.week.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::SmallButton(ICON_MD_CONTENT_COPY "##copy_w")) {
                    std::string text = format_objectives_markdown("Weekly Objectives", date_ctx_.week, weekly_objs_);
                    ImGui::SetClipboardText(text.c_str());
                }
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (const auto& obj : weekly_objs_) {
                    double prog = get_objective_progress(obj);
                    ImGui::TextWrapped("- %s", obj.title.c_str());
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[2]);
                    ImGui::ProgressBar(static_cast<float>(prog), ImVec2(-1.0f, 3.0f), "");
                    ImGui::PopStyleColor();
                }

                ImGui::Columns(1);
                ImGui::EndChild();
            }
            ImGui::Spacing();
        }

        // Forgiveness Buffer screen
        void render_forgiveness_buffer() {
            ImGui::OpenPopup("Forgiveness Buffer Popup");
            ImGui::SetNextWindowSize(ImVec2(480.0f, 220.0f));
            if (ImGui::BeginPopupModal("Forgiveness Buffer Popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(colors[3], ICON_MD_WARNING " FORGIVENESS BUFFER ACTIVE");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped("Yesterday's ledger (%s) was left open. What would you like to do?", unclosed_date_.c_str());
                ImGui::Spacing();
                ImGui::TextDisabled("Never auto-rollover daily objectives. Wipe the slate clean or log retroactively.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button(ICON_MD_ARCHIVE " Quick-Zero & Archive", ImVec2(210.0f, 40.0f))) {
                    repo_->quick_zero_and_archive_day(unclosed_date_);
                    ImGui::CloseCurrentPopup();
                    refresh_state();
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_MD_EDIT " Log Retroactively", ImVec2(210.0f, 40.0f))) {
                    // Quick state swap to log yesterday
                    date_ctx_ = models::productivity::objective_repository::get_date_context_for_time(
                        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() - std::chrono::hours(24))
                    );
                    load_all_data();
                    initialize_evening_ledger();
                    state_ = ui_state::evening_ledger;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        // Temporal setup wizards (Quarterly / Monthly / Weekly)
        void render_temporal_setup() {
            std::string header = "";
            std::string placeholder = "";
            if (setup_period_ == "quarterly") {
                header = std::format(ICON_MD_FLAG " Quarterly Setup - Trajectory Vision ({})", date_ctx_.quarter);
                placeholder = "Add high-level vision (e.g. Complete Rouen networking)";
            } else if (setup_period_ == "monthly") {
                header = std::format(ICON_MD_ASSIGNMENT " Monthly Setup - Major Milestones ({})", date_ctx_.month);
                placeholder = "Add major milestone (e.g. Deploy watchlists feature)";
            } else if (setup_period_ == "weekly") {
                header = std::format(ICON_MD_LIST " Weekly Setup - Sprint Goals ({})", date_ctx_.week);
                placeholder = "Add weekly objective (e.g. Write 5 unit tests)";
            }

            ImGui::TextColored(colors[0], "%s", header.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Two-column setup: list on left, entry form on right
            ImGui::Columns(2, "setup_cols", true);

            // Left column: Current objects added so far
            ImGui::Text("Current Objectives:");
            ImGui::Separator();
            
            std::vector<models::productivity::objective_record> current_setup_list;
            if (setup_period_ == "quarterly") current_setup_list = quarterly_objs_;
            else if (setup_period_ == "monthly") current_setup_list = monthly_objs_;
            else if (setup_period_ == "weekly") current_setup_list = weekly_objs_;

            if (current_setup_list.empty()) {
                ImGui::TextDisabled("No goals added yet. Add at least 1 goal to proceed.");
            } else {
                for (const auto& item : current_setup_list) {
                    ImGui::BulletText("%s", item.title.c_str());
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::SmallButton(std::format(ICON_MD_DELETE "##del_{}", item.id).c_str())) {
                        repo_->delete_objective(item.id);
                        load_all_data();
                    }
                }
            }

            ImGui::NextColumn();

            // Right column: Input new objective
            ImGui::Text("Add New Objective:");
            ImGui::Separator();
            ImGui::InputTextWithHint("##new_obj_title", placeholder.c_str(), new_title_buf_, sizeof(new_title_buf_));

            // Parent selection if applicable
            if (setup_period_ == "monthly" && !quarterly_objs_.empty()) {
                ImGui::Text("Associated Quarterly Vision:");
                if (ImGui::BeginCombo("##parent_q", quarterly_objs_[static_cast<size_t>(selected_parent_idx_)].title.c_str())) {
                    for (int i = 0; i < static_cast<int>(quarterly_objs_.size()); ++i) {
                        if (ImGui::Selectable(quarterly_objs_[static_cast<size_t>(i)].title.c_str(), selected_parent_idx_ == i)) {
                            selected_parent_idx_ = i;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (setup_period_ == "weekly" && !monthly_objs_.empty()) {
                ImGui::Text("Associated Monthly Milestone:");
                if (ImGui::BeginCombo("##parent_m", monthly_objs_[static_cast<size_t>(selected_parent_idx_)].title.c_str())) {
                    for (int i = 0; i < static_cast<int>(monthly_objs_.size()); ++i) {
                        if (ImGui::Selectable(monthly_objs_[static_cast<size_t>(i)].title.c_str(), selected_parent_idx_ == i)) {
                            selected_parent_idx_ = i;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button(ICON_MD_ADD " Add to Period", ImVec2(-1, 0)) && new_title_buf_[0] != '\0') {
                models::productivity::objective_record rec;
                rec.period = setup_period_;
                rec.title = new_title_buf_;
                rec.type = "binary"; // Non-daily objectives are typically binary targets
                rec.target_val = 1.0;
                rec.current_val = 0.0;
                rec.status = "committed";
                
                if (setup_period_ == "quarterly") {
                    rec.period_identifier = date_ctx_.quarter;
                } else if (setup_period_ == "monthly") {
                    rec.period_identifier = date_ctx_.month;
                    if (!quarterly_objs_.empty()) {
                        rec.parent_id = quarterly_objs_[static_cast<size_t>(selected_parent_idx_)].id;
                    }
                } else if (setup_period_ == "weekly") {
                    rec.period_identifier = date_ctx_.week;
                    if (!monthly_objs_.empty()) {
                        rec.parent_id = monthly_objs_[static_cast<size_t>(selected_parent_idx_)].id;
                    }
                }

                repo_->add_objective(rec);
                new_title_buf_[0] = '\0';
                selected_parent_idx_ = 0;
                load_all_data();
            }

            ImGui::Columns(1);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Finish button
            bool can_finish = !current_setup_list.empty();
            if (!can_finish) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(ICON_MD_CHECK " Finish Setup & Unlock", ImVec2(-1, 45.0f))) {
                refresh_state();
            }
            if (!can_finish) {
                ImGui::EndDisabled();
            }
        }

        // Morning Setup: Staging & Commit State
        void render_morning_staging() {
            render_context_bar();

            ImGui::TextColored(colors[0], ICON_MD_PLAY_ARROW " Morning Setup - Today's Staging Area (%s)", date_ctx_.date.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Columns(2, "staging_cols", true);

            // Left Column: Active Daily Stage
            ImGui::Text("Today's Daily Stage:");
            ImGui::Separator();

            if (staging_objs_.empty()) {
                ImGui::TextDisabled("No daily objectives staged yet.\nPull objectives from weekly targets on the right.");
            } else {
                for (size_t i = 0; i < staging_objs_.size(); ++i) {
                    auto& item = staging_objs_[i];
                    std::string type_label = "";
                    if (item.type == "binary") type_label = "[Binary]";
                    else if (item.type == "volumetric") type_label = std::format("[Volumetric: {}]", item.target_val);
                    else if (item.type == "constraint") type_label = std::format("[Constraint Limit: {}]", item.target_val);

                    ImGui::BulletText("%s: %s", type_label.c_str(), item.title.c_str());
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::SmallButton(std::format(ICON_MD_DELETE "##del_stage_{}", i).c_str())) {
                        staging_objs_.erase(staging_objs_.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
            }

            // Guardrail message
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(staging_objs_.size() >= 5 ? colors[3] : colors[2], 
                ICON_MD_INFO " Guardrail: %d / 5 Active Daily Items", static_cast<int>(staging_objs_.size()));
            if (staging_objs_.size() >= 5) {
                ImGui::TextColored(colors[3], "Overcommitted! Remove items before committing.");
            }

            ImGui::NextColumn();

            // Right Column: Weekly Objectives (The Anchor)
            ImGui::Text("Weekly Objectives (Derive items):");
            ImGui::Separator();

            for (const auto& w_obj : weekly_objs_) {
                ImGui::PushID(w_obj.id);
                ImGui::TextWrapped("%s", w_obj.title.c_str());
                
                // Pull Button
                bool is_staged_full = staging_objs_.size() >= 5;
                if (is_staged_full) ImGui::BeginDisabled();
                
                if (ImGui::Button(ICON_MD_ADD " Pull daily item")) {
                    ImGui::OpenPopup("Pull Daily Item Popup");
                    new_title_buf_[0] = '\0';
                    selected_type_idx_ = 0;
                    input_target_val_ = 1.0f;
                }
                
                if (is_staged_full) ImGui::EndDisabled();

                // Pull popup
                if (ImGui::BeginPopup("Pull Daily Item Popup")) {
                    ImGui::Text("Derive measurable daily objective:");
                    ImGui::Separator();
                    ImGui::InputTextWithHint("##daily_title", "Enter daily action title", new_title_buf_, sizeof(new_title_buf_));
                    
                    ImGui::Text("Metric Type:");
                    const char* types[] = {"Binary (Pass/Fail)", "Volumetric (Counter)", "Constraint (Limit)"};
                    ImGui::Combo("##type", &selected_type_idx_, types, 3);

                    if (selected_type_idx_ > 0) {
                        ImGui::Text("Target Value:");
                        ImGui::InputFloat("##target_val", &input_target_val_, 1.0f, 5.0f, "%.0f");
                        if (input_target_val_ < 1.0f) input_target_val_ = 1.0f;
                    }

                    if (ImGui::Button("Add Staged Goal") && new_title_buf_[0] != '\0') {
                        models::productivity::objective_record daily;
                        daily.parent_id = w_obj.id;
                        daily.period = "daily";
                        daily.period_identifier = date_ctx_.date;
                        daily.title = new_title_buf_;
                        daily.type = (selected_type_idx_ == 0) ? "binary" : 
                                     ((selected_type_idx_ == 1) ? "volumetric" : "constraint");
                        daily.target_val = (selected_type_idx_ == 0) ? 1.0 : static_cast<double>(input_target_val_);
                        daily.current_val = 0.0;
                        daily.status = "pending";
                        
                        staging_objs_.push_back(daily);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
                ImGui::Separator();
                ImGui::Spacing();
            }

            ImGui::Columns(1);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Commit Day Button
            bool commit_disabled = staging_objs_.empty() || staging_objs_.size() > 5;
            if (commit_disabled) ImGui::BeginDisabled();
            
            if (ImGui::Button(ICON_MD_LOCK " Lock In & Commit Today", ImVec2(-1, 45.0f))) {
                // Clear any old database pending daily items
                for (const auto& item : daily_objs_) {
                    repo_->delete_objective(item.id);
                }
                // Save staged items as committed
                for (auto& item : staging_objs_) {
                    item.status = "committed";
                    repo_->add_objective(item);
                }
                repo_->initialize_day_ledger(date_ctx_.date);
                refresh_state();
            }
            
            if (commit_disabled) ImGui::EndDisabled();
        }

        // Active Day Mode: Standard Workspace
        void render_normal_active() {
            render_context_bar();

            ImGui::Columns(2, "active_cols", true);

            // Left Column: Active committed Daily commitments
            ImGui::TextColored(colors[2], ICON_MD_PLAY_ARROW " Active Commitments:");
            ImGui::Separator();

            for (auto& item : daily_objs_) {
                ImGui::PushID(item.id);
                
                if (item.type == "binary") {
                    bool completed = item.current_val >= 1.0;
                    if (ImGui::Checkbox(item.title.c_str(), &completed)) {
                        item.current_val = completed ? 1.0 : 0.0;
                        repo_->update_objective(item);
                    }
                } 
                else if (item.type == "volumetric") {
                    ImGui::Text("%s", item.title.c_str());
                    
                    // [-] X / Y [+] counter controls
                    if (ImGui::Button(ICON_MD_REMOVE)) {
                        if (item.current_val > 0.0) {
                            item.current_val -= 1.0;
                            repo_->update_objective(item);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("  %.0f / %.0f  ", item.current_val, item.target_val);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_ADD)) {
                        item.current_val += 1.0;
                        repo_->update_objective(item);
                    }
                }
                else if (item.type == "constraint") {
                    bool broken = item.current_val > item.target_val;
                    if (broken) {
                        ImGui::TextColored(colors[3], ICON_MD_WARNING " [LIMIT BROKEN] %s", item.title.c_str());
                    } else {
                        ImGui::Text("%s", item.title.c_str());
                    }

                    // Increment / Decrement
                    if (ImGui::Button(ICON_MD_REMOVE)) {
                        if (item.current_val > 0.0) {
                            item.current_val -= 1.0;
                            repo_->update_objective(item);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("  Current: %.0f (Limit: %.0f)  ", item.current_val, item.target_val);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_ADD)) {
                        item.current_val += 1.0;
                        repo_->update_objective(item);
                    }
                }

                ImGui::PopID();
                ImGui::Separator();
                ImGui::Spacing();
            }

            ImGui::NextColumn();

            // Right Column: Weekly Objectives (Read-only anchor)
            ImGui::TextColored(colors[0], ICON_MD_ANCHOR " Weekly Anchors (Read-Only):");
            ImGui::Separator();

            for (const auto& w_obj : weekly_objs_) {
                ImGui::TextWrapped("%s", w_obj.title.c_str());
                double prog = get_objective_progress(w_obj);
                
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[0]);
                ImGui::ProgressBar(static_cast<float>(prog), ImVec2(-1.0f, 6.0f), "");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            ImGui::Columns(1);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Close Day Button
            if (ImGui::Button(ICON_MD_CHECK_CIRCLE " Close Day & Review Ledger (Ctrl+E)", ImVec2(-1, 45.0f))) {
                state_ = ui_state::evening_ledger;
                initialize_evening_ledger();
            }
        }

        // Evening Closing Ledger
        void render_evening_ledger() {
            ImGui::TextColored(colors[0], ICON_MD_BOOK " Evening Closing Ledger (%s)", date_ctx_.date.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Input final metrics and choose rollover actions for incomplete items:");
            ImGui::Spacing();

            for (auto& item : daily_objs_) {
                ImGui::PushID(item.id);
                double prog = get_objective_progress(item);
                bool completed = prog >= 1.0;

                ImGui::BeginGroup();
                if (completed) {
                    ImGui::TextColored(colors[2], ICON_MD_CHECK_CIRCLE " %s (SUCCESS)", item.title.c_str());
                } else {
                    ImGui::TextColored(colors[3], ICON_MD_CANCEL " %s (INCOMPLETE)", item.title.c_str());
                }

                // Adjust inputs inline in the ledger as well
                if (item.type == "volumetric") {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
                    if (ImGui::Button(ICON_MD_REMOVE)) {
                        if (item.current_val > 0.0) {
                            item.current_val -= 1.0;
                            repo_->update_objective(item);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("%.0f/%.0f", item.current_val, item.target_val);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_ADD)) {
                        item.current_val += 1.0;
                        repo_->update_objective(item);
                    }
                } else if (item.type == "constraint") {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
                    if (ImGui::Button(ICON_MD_REMOVE)) {
                        if (item.current_val > 0.0) {
                            item.current_val -= 1.0;
                            repo_->update_objective(item);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("%.0f/%.0f", item.current_val, item.target_val);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_ADD)) {
                        item.current_val += 1.0;
                        repo_->update_objective(item);
                    }
                }

                if (!completed) {
                    ImGui::Indent();
                    std::string& opt = evening_rollovers_[item.id];
                    
                    if (ImGui::RadioButton("Push to Tomorrow's Staging", opt == "push")) {
                        opt = "push";
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Let It Drop / Archive", opt == "drop")) {
                        opt = "drop";
                    }
                    ImGui::Unindent();
                }

                ImGui::EndGroup();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PopID();
            }

            ImGui::Spacing();

            // Cancel / Go Back
            if (ImGui::Button("Cancel", ImVec2(120.0f, 40.0f))) {
                state_ = ui_state::normal_active;
                load_all_data();
            }
            ImGui::SameLine();

            // Close Day Confirm
            if (ImGui::Button(ICON_MD_DONE_ALL " CLOSE DAY", ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
                std::vector<std::pair<int, std::string>> rollover_vec;
                for (const auto& [k, v] : evening_rollovers_) {
                    rollover_vec.push_back({k, v});
                }
                repo_->close_day(date_ctx_.date, rollover_vec);
                refresh_state();
            }
        }

        // Today Completed view
        void render_day_completed() {
            render_context_bar();

            ImGui::Spacing();
            ImGui::TextColored(colors[2], ICON_MD_CHECK_CIRCLE " TODAY'S LEDGER IS CLOSED & LOCKED");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped("You successfully locked and closed the accountability ledger for today (%s). Enjoy the rest of your day!", date_ctx_.date.c_str());
            ImGui::Spacing();

            ImGui::Text("Today's Performance:");
            ImGui::Separator();

            for (const auto& item : daily_objs_) {
                bool is_success = false;
                if (item.type == "binary") {
                    is_success = (item.current_val >= 1.0);
                } else if (item.type == "volumetric") {
                    is_success = (item.current_val >= item.target_val);
                } else if (item.type == "constraint") {
                    is_success = (item.current_val <= item.target_val);
                }

                if (is_success) {
                    ImGui::TextColored(colors[2], ICON_MD_CHECK_CIRCLE " %s - %.0f / %.0f (SUCCESS)", item.title.c_str(), item.current_val, item.target_val);
                } else {
                    ImGui::TextColored(colors[3], ICON_MD_CANCEL " %s - %.0f / %.0f (%s)", item.title.c_str(), item.current_val, item.target_val, item.status.c_str());
                }
            }
        }
    };
}
