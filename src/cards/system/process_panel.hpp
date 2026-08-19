#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <format>
#include <filesystem>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../registrar.hpp"
#include "../../helpers/app_icon.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/productivity/process_definition.hpp"
#include "../../hosts/process_host.hpp"

namespace rouen::cards {

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
        }

        ~process_panel() override {
            for (auto& [id, tex] : icon_textures_) {
                if (tex) TextureHelper::destroyTexture(tex);
            }
        }

        bool render() override {
            return render_window([this]() {
                render_toolbar();
                ImGui::Separator();
                render_definitions_table();
                if (show_form_) render_form_modal();
                if (show_delete_confirm_) render_delete_confirm();
            });
        }

        std::string get_uri() const override { return "process-panel"; }

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
    };

} // namespace rouen::cards
