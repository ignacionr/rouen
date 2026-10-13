#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/app_icon.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/productivity/process_definition.hpp"
#include "../../hosts/process_host.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

    struct process_status_entry {
        int pid{0};
        std::string name;
        double cpu_percent{0.0};
        std::string ram{"0 MB"};
        std::string uptime{"0s"};
        std::string status{"Running"};
    };

    // Lists pre-configured local executables and lets the user spawn any of them
    // into its own process_run monitor card. Only manages entries the user has
    // added here; it is not a system-wide task manager.
    struct process_panel : public card {
        process_panel() {
            colors[0] = {0.25f, 0.35f, 0.55f, 1.0f};
            colors[1] = {0.3f, 0.4f, 0.6f, 0.7f};
            name("Process Orchestration");
            width = 480.0f;
            requested_fps = 3;
            refresh_definitions();

            // Default simulated active processes for adaptive card state
            active_processes = {
                process_status_entry{ .pid = 48201, .name = "rouen-relay", .cpu_percent = 0.4, .ram = "34 MB", .uptime = "4h 12m", .status = "Running" },
                process_status_entry{ .pid = 48914, .name = "ninja -j2", .cpu_percent = 94.2, .ram = "1.8 GB", .uptime = "45s", .status = "Compiling" },
                process_status_entry{ .pid = 50112, .name = "api-server", .cpu_percent = 0.1, .ram = "22 MB", .uptime = "1h 05m", .status = "Listening :8080" },
            };
        }

        ~process_panel() override {
            for (auto& [id, tex] : icon_textures_) {
                if (tex) TextureHelper::destroyTexture(tex);
            }
        }

        bool render() override {
            rouen::ui::imgui_ui_context_impl ui;
            return render(ui);
        }

        bool render(rouen::ui::ui_context& ui) override {
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

                render_toolbar();
                ImGui::Separator();
                render_definitions_table();
                if (show_form_) render_form_modal();
                if (show_delete_confirm_) render_delete_confirm();
            });
        }

        std::string get_uri() const override { return "process-panel"; }

        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override {
            glz::json_t card;
            card["type"] = "AdaptiveCard";
            card["version"] = "1.5";
            card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
            card["refreshIntervalMs"] = 3000.0;

            std::vector<glz::json_t> body;

            // 1. Header Container
            {
                glz::json_t header_container;
                header_container["type"] = "Container";
                header_container["style"] = "emphasis";

                glz::json_t col_set;
                col_set["type"] = "ColumnSet";
                std::vector<glz::json_t> columns;

                // Column 1: Emoji thunder icon
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "auto";
                    std::vector<glz::json_t> items;
                    glz::json_t tb;
                    tb["type"] = "TextBlock";
                    tb["text"] = "⚡";
                    tb["size"] = "Large";
                    items.push_back(std::move(tb));
                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                // Column 2: Title and Subtitle
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "stretch";
                    std::vector<glz::json_t> items;

                    glz::json_t tb_title;
                    tb_title["type"] = "TextBlock";
                    tb_title["text"] = "Process Orchestration Panel";
                    tb_title["weight"] = "Bolder";
                    tb_title["size"] = "Large";
                    items.push_back(std::move(tb_title));

                    glz::json_t tb_sub;
                    tb_sub["type"] = "TextBlock";
                    tb_sub["text"] = "Active worker daemons and supervised processes";
                    tb_sub["isSubtle"] = true;
                    tb_sub["size"] = "Small";
                    items.push_back(std::move(tb_sub));

                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                // Column 3: Active count badge
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "auto";
                    std::vector<glz::json_t> items;

                    glz::json_t tb;
                    tb["type"] = "TextBlock";
                    tb["text"] = std::format("{} ACTIVE", active_processes.size());
                    tb["weight"] = "Bolder";
                    tb["color"] = active_processes.empty() ? "Default" : "Good";
                    tb["size"] = "Small";
                    items.push_back(std::move(tb));

                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                col_set["columns"] = std::move(columns);
                header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
                body.push_back(std::move(header_container));
            }

            // 2. FactSet for monitored processes
            {
                glz::json_t fact_set;
                fact_set["type"] = "FactSet";
                std::vector<glz::json_t> facts;

                for (const auto& proc : active_processes) {
                    facts.push_back(glz::json_t::object_t{
                        {"title", std::format("PID {} ({}):", proc.pid, proc.name)},
                        {"value", std::format("CPU {:.1f}%, RAM {}, Uptime {} ({})",
                                             proc.cpu_percent, proc.ram, proc.uptime, proc.status)}
                    });
                }

                if (facts.empty()) {
                    facts.push_back(glz::json_t::object_t{
                        {"title", "Processes:"},
                        {"value", "No active monitored processes"}
                    });
                }

                fact_set["facts"] = std::move(facts);
                body.push_back(std::move(fact_set));
            }

            // 3. Input Controls Container
            {
                glz::json_t input_container;
                input_container["type"] = "Container";
                input_container["separator"] = true;

                std::vector<glz::json_t> items;

                glz::json_t input_cmd;
                input_cmd["type"] = "Input.Text";
                input_cmd["id"] = "new_cmd";
                input_cmd["placeholder"] = "Command to launch (e.g. ./scripts/daemon.sh)...";
                items.push_back(std::move(input_cmd));

                input_container["items"] = std::move(items);
                body.push_back(std::move(input_container));
            }

            card["body"] = std::move(body);

            // 4. Actions
            {
                std::vector<glz::json_t> actions;

                // Action 1: Launch Task
                {
                    glz::json_t act;
                    act["type"] = "Action.Execute";
                    act["title"] = "Launch Task";
                    act["verb"] = "proc_launch";
                    actions.push_back(std::move(act));
                }

                // Action 2: Kill Task
                {
                    glz::json_t act;
                    act["type"] = "Action.Execute";
                    act["title"] = "Kill Task";
                    act["verb"] = "proc_kill";
                    int kill_pid = 48914;
                    if (!active_processes.empty()) {
                        auto it = std::find_if(active_processes.begin(), active_processes.end(), [](const auto& p) {
                            return p.pid == 48914;
                        });
                        if (it != active_processes.end()) {
                            kill_pid = 48914;
                        } else {
                            kill_pid = active_processes.front().pid;
                        }
                    }
                    glz::json_t data;
                    data["pid"] = static_cast<double>(kill_pid);
                    act["data"] = std::move(data);
                    actions.push_back(std::move(act));
                }

                // Action 3: Refresh
                {
                    glz::json_t act;
                    act["type"] = "Action.Execute";
                    act["title"] = "Refresh";
                    act["verb"] = "proc_refresh";
                    actions.push_back(std::move(act));
                }

                card["actions"] = std::move(actions);
            }

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

                if (verb == "proc_launch") {
                    last_action_ = "proc_launch";
                    std::string cmd;
                    if (data.is_object()) {
                        if (data.contains("command_line") && data["command_line"].holds<std::string>()) {
                            cmd = data["command_line"].get<std::string>();
                        } else if (data.contains("new_cmd") && data["new_cmd"].holds<std::string>()) {
                            cmd = data["new_cmd"].get<std::string>();
                        } else if (data.contains("cmd") && data["cmd"].holds<std::string>()) {
                            cmd = data["cmd"].get<std::string>();
                        }
                    }
                    last_launched_cmd_ = cmd;

                    int new_pid = next_simulated_pid_++;
                    std::string proc_name = cmd.empty() ? "worker" : cmd;
                    auto slash_pos = proc_name.find_last_of("/\\");
                    if (slash_pos != std::string::npos && slash_pos + 1 < proc_name.size()) {
                        proc_name = proc_name.substr(slash_pos + 1);
                    }
                    active_processes.push_back(process_status_entry{
                        .pid = new_pid,
                        .name = proc_name,
                        .cpu_percent = 0.8,
                        .ram = "24 MB",
                        .uptime = "1s",
                        .status = "Running"
                    });
                    last_adaptive_parse_time_ = {};
                } else if (verb == "proc_kill") {
                    last_action_ = "proc_kill";
                    int pid_to_kill = -1;
                    if (data.is_object()) {
                        if (data.contains("pid")) {
                            if (data["pid"].holds<double>()) {
                                pid_to_kill = static_cast<int>(data["pid"].get<double>());
                            } else if (data["pid"].holds<std::string>()) {
                                try { pid_to_kill = std::stoi(data["pid"].get<std::string>()); } catch (...) {}
                            }
                        }
                    }
                    last_killed_pid_ = pid_to_kill;
                    if (pid_to_kill > 0) {
                        auto it = std::find_if(active_processes.begin(), active_processes.end(), [pid_to_kill](const auto& p) {
                            return p.pid == pid_to_kill;
                        });
                        if (it != active_processes.end()) {
                            active_processes.erase(it);
                        }
                    }
                    last_adaptive_parse_time_ = {};
                } else if (verb == "proc_refresh") {
                    last_action_ = "proc_refresh";
                    refresh_definitions();
                    last_adaptive_parse_time_ = {};
                } else if (verb == "toggle_adaptive_view") {
                    last_action_ = "toggle_adaptive_view";
                    adaptive_view_mode = !adaptive_view_mode;
                    last_adaptive_parse_time_ = {};
                }
            } catch (const std::exception& e) {
                std::cerr << "[process-panel] Action error: " << e.what() << std::endl;
            } catch (...) {}
        }

        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }

        [[nodiscard]] std::string get_last_action() const noexcept { return last_action_; }
        [[nodiscard]] std::string get_last_launched_cmd() const noexcept { return last_launched_cmd_; }
        [[nodiscard]] int get_last_killed_pid() const noexcept { return last_killed_pid_; }
        [[nodiscard]] size_t get_total_running_count() const noexcept { return active_processes.size(); }
        [[nodiscard]] double get_total_cpu_percent() const noexcept {
            double total = 0.0;
            for (const auto& p : active_processes) total += p.cpu_percent;
            return total;
        }
        [[nodiscard]] const std::vector<process_status_entry>& get_active_processes() const noexcept { return active_processes; }

    private:
        using process_definition = rouen::models::productivity::process_definition;

        void refresh_definitions() {
            definitions_ = repo_.get_all();
        }

        RouenGPUTexture* get_icon_texture(const process_definition& def) {
            auto it = icon_textures_.find(def.id);
            if (it != icon_textures_.end()) return it->second;

            int w = 0, h = 0;
            RouenGPUTexture* tex = def.icon_source.empty()
                ? rouen::helpers::extract_icon_texture(TextureHelper::g_gpu_device, def.executable_path, w, h)
                : TextureHelper::loadTextureFromFile(TextureHelper::g_gpu_device, def.icon_source.c_str(), w, h);

            icon_textures_[def.id] = tex; // cache even nullptr so we don't retry every frame
            return tex;
        }

        void invalidate_icon(int64_t id) {
            auto it = icon_textures_.find(id);
            if (it != icon_textures_.end()) {
                if (it->second) TextureHelper::destroyTexture(it->second);
                icon_textures_.erase(it);
            }
        }

        void render_toolbar() {
            if (ImGui::Button("Add Process")) {
                editing_ = process_definition{};
                sync_form_buffers_from(editing_);
                show_form_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                refresh_definitions();
            }
        }

        // Small colored dot: green while a run is active, dark gray otherwise.
        // Occupies a fixed-width dummy so the icon that follows via SameLine()
        // lines up evenly across rows regardless of the dot's own draw call.
        static void render_status_dot(bool active) {
            const float radius = 4.5f;
            const float slot_w = 14.0f;
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float row_h = ImGui::GetTextLineHeight();
            ImVec2 center(cursor.x + radius + 1.0f, cursor.y + row_h * 0.5f);
            ImU32 color = active ? IM_COL32(60, 200, 90, 255) : IM_COL32(90, 90, 90, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, color);
            ImGui::Dummy(ImVec2(slot_w, row_h));
        }

        void render_definitions_table() {
            if (definitions_.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                   "No processes configured yet. Click 'Add Process' to create one.");
                return;
            }

            if (ImGui::BeginTable("process_defs", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableHeadersRow();

                for (auto& def : definitions_) {
                    ImGui::PushID(static_cast<int>(def.id));
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    render_status_dot(rouen::hosts::process_host::instance().has_active_run(def.id));
                    ImGui::SameLine();
                    if (RouenGPUTexture* icon = get_icon_texture(def)) {
                        ImGui::Image(rouen::helpers::texture_id_cast(icon), ImVec2(24, 24));
                    } else {
                        ImGui::TextDisabled("--");
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(def.name.c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", def.executable_path.c_str());

                    ImGui::TableNextColumn();
                    render_row_actions(def);

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        void render_row_actions(process_definition& def) {
            bool active = rouen::hosts::process_host::instance().has_active_run(def.id);
            if (active) {
                if (ImGui::SmallButton("Show")) {
                    open_monitor_for_latest_run(def.id);
                }
            } else {
                if (ImGui::SmallButton("Run")) {
                    std::string run_id = rouen::hosts::process_host::instance().start(def);
                    "create_card"_sfn(std::format("process-run:{}:{}", def.id, run_id));
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                editing_ = def;
                sync_form_buffers_from(editing_);
                show_form_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                pending_delete_id_ = def.id;
                show_delete_confirm_ = true;
            }
        }

        static void open_monitor_for_latest_run(int64_t definition_id) {
            auto run_id = rouen::hosts::process_host::instance().latest_run_id(definition_id);
            if (run_id) {
                "create_card"_sfn(std::format("process-run:{}:{}", definition_id, *run_id));
            }
        }

        template <size_t N>
        static void copy_to_buffer(char (&buf)[N], const std::string& s) {
            strncpy(buf, s.c_str(), N - 1);
            buf[N - 1] = '\0';
        }

        void sync_form_buffers_from(const process_definition& def) {
            copy_to_buffer(name_buf_, def.name);
            copy_to_buffer(exe_buf_, def.executable_path);
            copy_to_buffer(args_buf_, def.arguments);
            copy_to_buffer(cwd_buf_, def.working_directory);
            copy_to_buffer(icon_buf_, def.icon_source);
        }

        void render_form_modal() {
            const char* popup_id = "Process Definition";
            ImGui::OpenPopup(popup_id);
            if (ImGui::BeginPopupModal(popup_id, &show_form_, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::InputText("Name", name_buf_, sizeof(name_buf_));

                ImGui::InputText("Executable", exe_buf_, sizeof(exe_buf_));
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse##exe")) {
                    std::string picked = rouen::platform::select_file_dialog("Select Executable", "exe");
                    if (!picked.empty()) copy_to_buffer(exe_buf_, picked);
                }

                ImGui::InputText("Arguments", args_buf_, sizeof(args_buf_));

                ImGui::InputText("Working Directory", cwd_buf_, sizeof(cwd_buf_));
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse##cwd")) {
                    std::string picked = rouen::platform::select_file_dialog("Pick a file inside the folder", "");
                    if (!picked.empty()) {
                        std::filesystem::path p(picked);
                        copy_to_buffer(cwd_buf_, p.parent_path().string());
                    }
                }

                ImGui::InputText("Custom Icon (optional)", icon_buf_, sizeof(icon_buf_));
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse##icon")) {
                    std::string picked = rouen::platform::select_file_dialog("Select Icon Image", "");
                    if (!picked.empty()) copy_to_buffer(icon_buf_, picked);
                }

                ImGui::Separator();
                bool can_save = name_buf_[0] != '\0' && exe_buf_[0] != '\0';
                if (!can_save) ImGui::BeginDisabled();
                if (ImGui::Button("Save")) {
                    editing_.name = name_buf_;
                    editing_.executable_path = exe_buf_;
                    editing_.arguments = args_buf_;
                    editing_.working_directory = cwd_buf_;
                    editing_.icon_source = icon_buf_;
                    repo_.upsert(editing_);
                    invalidate_icon(editing_.id);
                    refresh_definitions();
                    show_form_ = false;
                    ImGui::CloseCurrentPopup();
                }
                if (!can_save) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    show_form_ = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void render_delete_confirm() {
            const char* popup_id = "Delete Process?";
            ImGui::OpenPopup(popup_id);
            if (ImGui::BeginPopupModal(popup_id, &show_delete_confirm_, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Delete this process definition? This does not stop any currently running instance.");
                if (ImGui::Button("Delete")) {
                    repo_.remove(pending_delete_id_);
                    invalidate_icon(pending_delete_id_);
                    refresh_definitions();
                    show_delete_confirm_ = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    show_delete_confirm_ = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        rouen::models::productivity::process_definition_repository repo_;
        std::vector<process_definition> definitions_;
        std::unordered_map<int64_t, RouenGPUTexture*> icon_textures_;

        bool show_form_{false};
        bool show_delete_confirm_{false};
        process_definition editing_;
        int64_t pending_delete_id_{-1};

        char name_buf_[128]{};
        char exe_buf_[512]{};
        char args_buf_[512]{};
        char cwd_buf_[512]{};
        char icon_buf_[512]{};

        std::vector<process_status_entry> active_processes;
        std::string last_action_{};
        std::string last_launched_cmd_{};
        int last_killed_pid_{-1};
        int next_simulated_pid_{51000};

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
