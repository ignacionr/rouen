#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <vector>
#include <cmath>
#include <format>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../helpers/app_icon.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/vu_meter.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/directory_watch.hpp"
#include "../../models/productivity/process_definition.hpp"
#include "../../hosts/process_host.hpp"
#include "../../helpers/ui_automation_explorer.hpp"
#include "../development/fs-directory.hpp"

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include "../../helpers/process_helper.hpp"
#endif

namespace rouen::cards {

    // Monitor view for a single launch of a process_definition. Never owns the
    // underlying OS process (rouen::hosts::process_host does) so closing this
    // card never stops the process; only its Kill button does.
    struct process_run : public card {
        explicit process_run(std::string_view locator) {
            std::string loc(locator);
            auto colon = loc.find(':');
            std::string def_id_str = (colon == std::string::npos) ? loc : loc.substr(0, colon);
            if (colon != std::string::npos) {
                run_id_ = loc.substr(colon + 1);
            }

            try {
                definition_id_ = def_id_str.empty() ? -1 : std::stoll(def_id_str);
            } catch (const std::exception&) {
                definition_id_ = -1;
            }

            auto def = repo_.get_by_id(definition_id_);
            if (def) definition_name_ = def->name;

            colors[0] = {0.3f, 0.25f, 0.45f, 1.0f};
            colors[1] = {0.35f, 0.3f, 0.5f, 0.7f};
            name(definition_name_.empty() ? "Process" : definition_name_);
            width = 560.0f;
            requested_fps = 5;
        }

        private:
        static bool node_matches_query(const rouen::helpers::ui_element_node& node, const std::string& query) {
            if (query.empty()) return true;
            auto matches_ic = [](std::string_view haystack, std::string_view needle) {
                auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                return it != haystack.end();
            };
            return matches_ic(node.name, query) || matches_ic(node.role, query) ||
                   matches_ic(node.subrole, query) || matches_ic(node.id, query) ||
                   matches_ic(node.value, query) || matches_ic(node.description, query);
        }

        static bool subtree_matches_query(const rouen::helpers::ui_element_node& node, const std::string& query) {
            if (node_matches_query(node, query)) return true;
            for (const auto& child : node.children) {
                if (subtree_matches_query(child, query)) return true;
            }
            return false;
        }

        void render_ui_node_tree(const rouen::helpers::ui_element_node& node, const std::string& query, int depth) {
            if (!query.empty() && !subtree_matches_query(node, query)) {
                return;
            }

            ImGui::PushID(&node);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node.children.empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (selected_ui_node_ == &node) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!query.empty() && subtree_matches_query(node, query)) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            } else if (depth == 0) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            std::string label = std::format("[{}] {}", node.role.empty() ? "Element" : node.role,
                                            node.name.empty() ? "(unnamed)" : node.name);

            bool is_open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                selected_ui_node_ = &node;
            }

            if (ImGui::BeginPopupContextItem()) {
                selected_ui_node_ = &node;
                std::vector<const rouen::helpers::ui_element_node*> path;
                if (ui_tree_result_) {
                    path = rouen::helpers::ui_automation_explorer::find_path_to_node(ui_tree_result_->root, &node);
                }

                if (ImGui::MenuItem("Copy Tree Path")) {
                    std::string p = rouen::helpers::ui_automation_explorer::format_tree_path(path);
                    ImGui::SetClipboardText(p.c_str());
                }
                if (ImGui::MenuItem("Copy Full Properties")) {
                    std::string fp = rouen::helpers::ui_automation_explorer::format_element_properties(node);
                    ImGui::SetClipboardText(fp.c_str());
                }
                if (ImGui::MenuItem("Copy Path & Properties")) {
                    std::string fi = rouen::helpers::ui_automation_explorer::format_full_element_info(node, path);
                    ImGui::SetClipboardText(fi.c_str());
                }
                ImGui::Separator();
                if (!node.value.empty() && ImGui::MenuItem("Copy Value")) {
                    ImGui::SetClipboardText(node.value.c_str());
                }
                if (!node.name.empty() && ImGui::MenuItem("Copy Name")) {
                    ImGui::SetClipboardText(node.name.c_str());
                }
                if (!node.id.empty() && ImGui::MenuItem("Copy ID")) {
                    ImGui::SetClipboardText(node.id.c_str());
                }
                ImGui::EndPopup();
            }

            if (is_open && !node.children.empty()) {
                for (const auto& child : node.children) {
                    render_ui_node_tree(child, query, depth + 1);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        void render_selected_ui_node_details() {
            if (!selected_ui_node_) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select an element from the tree to view properties.");
                return;
            }

            const auto& node = *selected_ui_node_;
            std::vector<const rouen::helpers::ui_element_node*> path;
            if (ui_tree_result_) {
                path = rouen::helpers::ui_automation_explorer::find_path_to_node(ui_tree_result_->root, &node);
            }
            std::string single_line_path = rouen::helpers::ui_automation_explorer::format_tree_path(path);
            std::string full_props = rouen::helpers::ui_automation_explorer::format_element_properties(node);
            std::string full_info = rouen::helpers::ui_automation_explorer::format_full_element_info(node, path);

            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", node.role.c_str());
            if (!node.subrole.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", node.subrole.c_str());
            }

            // Top action buttons for quick clipboard copy
            if (ImGui::SmallButton("Copy Path")) {
                ImGui::SetClipboardText(single_line_path.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy full tree path from root element to clipboard");

            ImGui::SameLine();
            if (ImGui::SmallButton("Copy Props")) {
                ImGui::SetClipboardText(full_props.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy all element properties and attributes to clipboard");

            ImGui::SameLine();
            if (ImGui::SmallButton("Copy All")) {
                ImGui::SetClipboardText(full_info.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy both full tree path and properties to clipboard");

            if (!single_line_path.empty()) {
                ImGui::TextWrapped("Path: %s", single_line_path.c_str());
            }

            ImGui::Separator();

            if (ImGui::BeginTable("selected_prop_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                auto draw_prop = [](const char* name, const std::string& val) {
                    if (val.empty()) return;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", val.c_str());
                };

                draw_prop("Name", node.name);
                draw_prop("Role", node.role);
                draw_prop("Subrole", node.subrole);
                
                if (!node.value.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Value");
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", node.value.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy##val_copy")) {
                        ImGui::SetClipboardText(node.value.c_str());
                    }
                }

                draw_prop("ID", node.id);
                draw_prop("Description", node.description);

                if (node.width > 0.0f || node.height > 0.0f) {
                    draw_prop("Bounds", std::format("X:{:.0f} Y:{:.0f} W:{:.0f} H:{:.0f}",
                                                    node.x, node.y, node.width, node.height));
                }

                draw_prop("Enabled", node.enabled ? "true" : "false");
                draw_prop("Focused", node.focused ? "true" : "false");
                draw_prop("Children", std::to_string(node.children.size()));

                for (const auto& attr : node.attributes) {
                    if (attr.name == "Value") continue; // already displayed above
                    draw_prop(attr.name.c_str(), attr.value);
                }

                ImGui::EndTable();
            }
        }

        void render_ui_automation(const rouen::hosts::process_run_snapshot& snap) {
            ImGui::TextUnformatted("UI Automation Explorer (AXUIElement / IUIAutomation):");

            bool trusted = rouen::helpers::ui_automation_explorer::check_accessibility_permissions(false);

            if (ImGui::Button("Inspect UI Tree")) {
                ui_tree_result_ = rouen::helpers::ui_automation_explorer::inspect_process(snap.pid, ui_max_depth_);
                selected_ui_node_ = nullptr;
                requested_control_value_.reset();
                value_request_attempted_ = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Tree")) {
                ui_tree_result_.reset();
                selected_ui_node_ = nullptr;
                requested_control_value_.reset();
                value_request_attempted_ = false;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputInt("Max Depth", &ui_max_depth_)) {
                ui_max_depth_ = std::clamp(ui_max_depth_, 1, 15);
            }

            if (!trusted) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "[Permission Warning]");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("macOS Accessibility permission is required for AXUIElement tree inspection.");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Request Access")) {
                    rouen::helpers::ui_automation_explorer::check_accessibility_permissions(true);
                }
            }

            // Direct Request Control Text/Value bar
            ImGui::Spacing();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##req_val_input", "Control ID or Name to request value...", ui_request_query_buf_, sizeof(ui_request_query_buf_));
            ImGui::SameLine();
            if (ImGui::Button("Request Value")) {
                value_request_attempted_ = true;
                requested_control_value_ = rouen::helpers::ui_automation_explorer::request_control_value(snap.pid, ui_request_query_buf_);
            }
            if (value_request_attempted_) {
                ImGui::SameLine();
                if (requested_control_value_) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Result: \"%s\"", requested_control_value_->c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy Result")) {
                        ImGui::SetClipboardText(requested_control_value_->c_str());
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No control or text found.");
                }
            }

            if (!ui_tree_result_) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "Click 'Inspect UI Tree' to scan the application's UI hierarchy and edit boxes.");
                return;
            }

            const auto& res = *ui_tree_result_;
            if (res.permission_denied) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", res.error_message.c_str());
                return;
            }

            if (!res.success) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Error: %s", res.error_message.c_str());
                return;
            }

            ImGui::RadioButton("Hierarchy Tree View", &ui_explorer_mode_, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Extracted Edit Boxes & Values", &ui_explorer_mode_, 1);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##ui_search", "Filter elements...", ui_search_buf_, sizeof(ui_search_buf_));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Nodes: %zu", res.total_node_count);

            std::string query(ui_search_buf_);

            if (ui_explorer_mode_ == 0) {
                if (ImGui::BeginTable("ui_explorer_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("UI Element Tree", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                    ImGui::TableSetupColumn("Element Properties", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                    ImGui::TableHeadersRow();

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::BeginChild("UITreeScroll", ImVec2(0, 220.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
                    render_ui_node_tree(res.root, query, 0);
                    ImGui::EndChild();

                    ImGui::TableNextColumn();
                    ImGui::BeginChild("UINodeDetailsScroll", ImVec2(0, 220.0f), false);
                    render_selected_ui_node_details();
                    ImGui::EndChild();

                    ImGui::EndTable();
                }
            } else {
                auto extracted = res.extract_values(true);
                
                if (!query.empty()) {
                    auto matches_ic = [](std::string_view haystack, std::string_view needle) {
                        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                        return it != haystack.end();
                    };
                    std::erase_if(extracted, [&](const auto& item) {
                        return !matches_ic(item.name, query) && !matches_ic(item.role, query) &&
                               !matches_ic(item.id, query) && !matches_ic(item.value, query) &&
                               !matches_ic(item.description, query) && !matches_ic(item.path, query);
                    });
                }

                if (ImGui::Button("Copy All Values")) {
                    std::string copy_buf;
                    for (const auto& item : extracted) {
                        copy_buf += std::format("[{}] {} (ID: {}): {}\n", item.role, item.name, item.id, item.value);
                    }
                    ImGui::SetClipboardText(copy_buf.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Copy All (Paths & Values)")) {
                    std::string copy_buf;
                    for (const auto& item : extracted) {
                        copy_buf += std::format("Path: {}\nRole: {} | Name: {}\nValue: {}\nID: {}\n---\n",
                                                item.path, item.role, item.name, item.value, item.id);
                    }
                    ImGui::SetClipboardText(copy_buf.c_str());
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Found %zu controls", extracted.size());

                if (ImGui::BeginTable("extracted_values_table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Name / Label", ImGuiTableColumnFlags_WidthStretch, 0.25f);
                    ImGui::TableSetupColumn("Text / Value", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                    ImGui::TableSetupColumn("Tree Path", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();

                    ImGui::BeginChild("ExtractedScroll", ImVec2(0, 200.0f), false);
                    for (size_t idx = 0; idx < extracted.size(); ++idx) {
                        const auto& item = extracted[idx];
                        ImGui::PushID(static_cast<int>(idx));
                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(item.role.c_str());

                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("%s", item.name.empty() ? "(unnamed)" : item.name.c_str());

                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", item.value.empty() ? "(empty)" : item.value.c_str());

                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", item.path.c_str());
                        if (ImGui::IsItemHovered() && !item.path.empty()) {
                            ImGui::SetTooltip("%s", item.path.c_str());
                        }

                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", item.id.c_str());

                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("Val")) {
                            ImGui::SetClipboardText(item.value.c_str());
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy Value");

                        ImGui::SameLine();
                        if (ImGui::SmallButton("Path")) {
                            ImGui::SetClipboardText(item.path.c_str());
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy Tree Path");

                        if (ImGui::BeginPopupContextItem("item_ctx")) {
                            if (ImGui::MenuItem("Copy Text / Value")) {
                                ImGui::SetClipboardText(item.value.c_str());
                            }
                            if (ImGui::MenuItem("Copy Tree Path")) {
                                ImGui::SetClipboardText(item.path.c_str());
                            }
                            if (ImGui::MenuItem("Copy Full Item Details")) {
                                std::string info = std::format("Tree Path: {}\nRole: {}\nSubrole: {}\nName: {}\nValue: {}\nID: {}\nDescription: {}\n",
                                                               item.path, item.role, item.subrole, item.name, item.value, item.id, item.description);
                                ImGui::SetClipboardText(info.c_str());
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndChild();

                    ImGui::EndTable();
                }
            }
        }

    public:
        bool render() override {
            return render_window([this]() {
                render_header();
                ImGui::Separator();

                auto snap = run_id_.empty()
                    ? std::nullopt
                    : rouen::hosts::process_host::instance().snapshot(run_id_);
                if (snap) {
                    render_meters(*snap);
                    ImGui::Separator();
                    render_tcp_connections(*snap);
                    ImGui::Separator();
                    if (snap->state == rouen::hosts::process_run_state::running) {
                        render_ui_automation(*snap);
                        ImGui::Separator();
                    }
                }

                render_file_changes();
                ImGui::Separator();

                render_stderr();
            });
        }

        std::string get_uri() const override {
            return std::format("process-run:{}", definition_id_);
        }

        // Every Run/Show click must always create a fresh card bound to a specific
        // run_id rather than silently refocusing an existing monitor card.
        bool matches_uri(std::string_view) const override { return false; }

    private:
        // Tracks an ever-growing "nice" ceiling (1/2/5 x 10^n) for a VU meter so the
        // needle sweeps a readable range instead of pinning at max the first time a
        // value spikes. Never shrinks back down within this card's lifetime, so the
        // scale stays stable to read at a glance.
        struct auto_scale_meter {
            float ceiling{1.0f};
            float peak{0.0f};

            float normalize(double value) {
                peak = std::max(peak, static_cast<float>(value));
                float target = std::max(peak * 1.15f, 1.0f);
                if (target > ceiling) ceiling = nice_ceiling(target);
                return ceiling > 0.0f ? std::clamp(static_cast<float>(value) / ceiling, 0.0f, 1.0f) : 0.0f;
            }

            float watermark_norm() const {
                return ceiling > 0.0f ? std::clamp(peak / ceiling, 0.0f, 1.0f) : 0.0f;
            }

            static float nice_ceiling(float v) {
                if (v <= 1.0f) return 1.0f;
                float mag = std::pow(10.0f, std::floor(std::log10(v)));
                float norm = v / mag;
                float nice = (norm <= 1.0f) ? 1.0f : (norm <= 2.0f) ? 2.0f : (norm <= 5.0f) ? 5.0f : 10.0f;
                return nice * mag;
            }
        };

        static std::string format_count(double v) {
            return std::format("{}", static_cast<long long>(std::llround(v)));
        }

        static std::string format_bytes(double v) {
            const char* unit = "B";
            if (v >= 1024.0 * 1024.0 * 1024.0) { v /= (1024.0 * 1024.0 * 1024.0); unit = "GB"; }
            else if (v >= 1024.0 * 1024.0) { v /= (1024.0 * 1024.0); unit = "MB"; }
            else if (v >= 1024.0) { v /= 1024.0; unit = "KB"; }
            return std::format("{:.1f} {}", v, unit);
        }

        static std::vector<rouen::helpers::vu_meter::VUMeterTick> build_ticks(
            float ceiling, const std::function<std::string(double)>& fmt) {
            return {
                { 0.0f, fmt(0.0), true },
                { 0.5f, fmt(static_cast<double>(ceiling) * 0.5), true },
                { 1.0f, fmt(static_cast<double>(ceiling)), true },
            };
        }

        static void render_meter(const char* title, double value, auto_scale_meter& meter,
                                 const std::function<std::string(double)>& fmt) {
            float normalized = meter.normalize(value);

            rouen::helpers::vu_meter::VUMeterConfig config;
            config.scale_type = rouen::helpers::vu_meter::VUMeterScaleType::Custom;
            config.custom_ticks = build_ticks(meter.ceiling, fmt);
            config.title = title;
            config.show_titles = true;
            config.style.theme = rouen::helpers::vu_meter::VUMeterTheme::ModernDark;

            rouen::helpers::vu_meter::render_analog_dial(
                ImVec2(110.0f, 84.0f), normalized, meter.watermark_norm(), fmt(value), config);
        }

        void render_meters(const rouen::hosts::process_run_snapshot& snap) {
            render_meter("THREADS", static_cast<double>(snap.stats.thread_count.value_or(0)),
                        thread_meter_, format_count);
            ImGui::SameLine();
            render_meter("SUBPROC", static_cast<double>(snap.stats.subprocess_count.value_or(0)),
                        subprocess_meter_, format_count);
            ImGui::SameLine();
            render_meter("HANDLES", static_cast<double>(snap.stats.open_handle_count.value_or(0)),
                        handle_meter_, format_count);
            ImGui::SameLine();
            render_meter("MEMORY", static_cast<double>(snap.stats.memory_bytes.value_or(0)),
                        memory_meter_, format_bytes);
        }

        void render_tcp_connections(const rouen::hosts::process_run_snapshot& snap) {
            ImGui::TextUnformatted("TCP Connections:");
            ImGui::BeginChild("TcpRegion", ImVec2(0, 120.0f), true);
            const auto& conns = snap.stats.tcp_connections;
            if (conns.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No active TCP connections");
            } else if (ImGui::BeginTable("tcp_table", 4,
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Local", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Remote", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                for (const auto& c : conns) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImVec4 dir_color = (c.direction == "Inbound") ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f)
                                       : (c.direction == "Outbound") ? ImVec4(0.4f, 0.65f, 0.95f, 1.0f)
                                       : ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
                    ImGui::TextColored(dir_color, "%s", c.direction.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%s:%d", c.local_address.c_str(), c.local_port);

                    ImGui::TableNextColumn();
                    if (!c.remote_address.empty()) {
                        ImGui::Text("%s:%d", c.remote_address.c_str(), c.remote_port);
                    } else {
                        ImGui::TextDisabled("--");
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(c.state.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        }

        void load_icon(const rouen::models::productivity::process_definition& def) {
            icon_loaded_ = true;
            int w = 0, h = 0;
            icon_texture_ = def.icon_source.empty()
                ? rouen::helpers::extract_icon_texture(TextureHelper::g_gpu_device, def.executable_path, w, h)
                : TextureHelper::loadTextureFromFile(TextureHelper::g_gpu_device, def.icon_source.c_str(), w, h);
        }

        // Brings the process's main window to the foreground, the same end result as
        // Alt+Tabbing to it. Best-effort: does nothing if the process has no visible
        // window (e.g. a headless CLI tool) or the platform has no equivalent.
        static void bring_to_foreground(long pid) {
#if defined(_WIN32)
            struct EnumData { DWORD pid; HWND best; };
            EnumData data{ static_cast<DWORD>(pid), nullptr };

            EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
                auto* d = reinterpret_cast<EnumData*>(lparam);
                DWORD wnd_pid = 0;
                GetWindowThreadProcessId(hwnd, &wnd_pid);
                if (wnd_pid != d->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
                    return TRUE; // keep enumerating
                }
                d->best = hwnd;
                return FALSE; // found a top-level visible window owned by this pid
            }, reinterpret_cast<LPARAM>(&data));

            if (!data.best) return;
            if (IsIconic(data.best)) ShowWindow(data.best, SW_RESTORE);

            // SetForegroundWindow is normally blocked unless our thread shares the
            // foreground thread's input queue; briefly attach so the OS allows the
            // focus steal, the same mechanism Alt+Tab relies on.
            HWND foreground_hwnd = GetForegroundWindow();
            DWORD foreground_pid = 0;
            DWORD foreground_thread = foreground_hwnd ? GetWindowThreadProcessId(foreground_hwnd, &foreground_pid) : 0;
            DWORD this_thread = GetCurrentThreadId();
            bool attached = false;
            if (foreground_thread && foreground_thread != this_thread) {
                attached = AttachThreadInput(this_thread, foreground_thread, TRUE) != 0;
            }
            SetForegroundWindow(data.best);
            if (attached) AttachThreadInput(this_thread, foreground_thread, FALSE);
#elif defined(__APPLE__)
            std::string cmd = std::format(
                "osascript -e 'tell application \"System Events\" to set frontmost of "
                "(first process whose unix id is {}) to true' 2>/dev/null", pid);
            ProcessHelper::executeCommand(cmd);
#else
            (void)pid;
#endif
        }

        void render_header() {
            auto def = repo_.get_by_id(definition_id_);
            if (def && !icon_loaded_) load_icon(*def);

            if (icon_texture_) {
                ImGui::Image(rouen::helpers::texture_id_cast(icon_texture_), ImVec2(32, 32));
                ImGui::SameLine();
            }

            if (def) {
                ImGui::TextWrapped("%s", def->executable_path.c_str());
                if (!def->working_directory.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "in %s", def->working_directory.c_str());
                }
                working_directory_ = resolve_env_variables(def->working_directory);
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Process definition no longer exists.");
                return;
            }

            auto snap = run_id_.empty()
                ? std::nullopt
                : rouen::hosts::process_host::instance().snapshot(run_id_);

            if (!snap) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not running");
                if (ImGui::Button("Start")) {
                    run_id_ = rouen::hosts::process_host::instance().start(*def);
                }
                return;
            }

            switch (snap->state) {
                case rouen::hosts::process_run_state::running:
                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.3f, 1.0f), "Running (PID %ld)", snap->pid);
                    if (ImGui::Button("Kill")) {
                        rouen::hosts::process_host::instance().kill(run_id_);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Show Window")) {
                        bring_to_foreground(snap->pid);
                    }
                    break;
                case rouen::hosts::process_run_state::exited:
                    ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.3f, 1.0f), "Exited (code %d)",
                                       snap->exit_code.value_or(-1));
                    if (ImGui::Button("Start Again")) {
                        run_id_ = rouen::hosts::process_host::instance().start(*def);
                    }
                    break;
                case rouen::hosts::process_run_state::failed_to_start:
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Failed to start: %s",
                                       snap->start_error.c_str());
                    if (ImGui::Button("Retry")) {
                        run_id_ = rouen::hosts::process_host::instance().start(*def);
                    }
                    break;
            }
        }

        static std::string current_time_label() {
            auto now = std::chrono::system_clock::now();
            std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm{};
#if defined(_WIN32)
            localtime_s(&now_tm, &now_time_t);
#else
            localtime_r(&now_time_t, &now_tm);
#endif
            char buf[16];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", &now_tm);
            return buf;
        }

        void record_file_change(const std::string& path) {
            std::erase_if(changed_files_, [&](const auto& e) { return e.path == path; });
            changed_files_.insert(changed_files_.begin(), {path, current_time_label()});
            if (changed_files_.size() > 300) changed_files_.resize(300);
        }

        void render_file_changes() {
            if (ImGui::Checkbox("Track File Changes", &track_file_changes_)) {
                if (track_file_changes_ && !working_directory_.empty()) {
                    watcher_ = std::make_unique<rouen::helpers::directory_watch>(working_directory_);
                } else {
                    watcher_.reset();
                }
            }

            if (!track_file_changes_) return;
            if (!watcher_ && !working_directory_.empty()) {
                watcher_ = std::make_unique<rouen::helpers::directory_watch>(working_directory_);
            }

            if (watcher_ && !watcher_->active()) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                                   "Could not watch \"%s\" (missing, inaccessible, or bad path)",
                                   working_directory_.c_str());
                return;
            }

            if (watcher_) {
                for (const auto& path : watcher_->drain_changes()) {
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(path, ec)) record_file_change(path);
                }
            }

            ImGui::BeginChild("FileChangesRegion", ImVec2(0, 100.0f), true);
            if (changed_files_.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file changes detected yet");
            } else {
                for (size_t i = 0; i < changed_files_.size(); ++i) {
                    const auto& entry = changed_files_[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::SmallButton("Open")) {
                        rouen::platform::open_url(entry.path);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", entry.changed_at.c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(entry.path.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        void render_stderr() {
            ImGui::TextUnformatted("stderr:");
            ImGui::BeginChild("StderrRegion", ImVec2(0, -1), true);
            if (!run_id_.empty()) {
                auto snap = rouen::hosts::process_host::instance().snapshot(run_id_);
                if (snap) {
                    for (const auto& line : snap->stderr_lines) {
                        ImGui::TextUnformatted(line.c_str());
                    }
                    if (snap->state == rouen::hosts::process_run_state::running) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                }
            }
            ImGui::EndChild();
        }

        int64_t definition_id_{-1};
        std::string run_id_;
        std::string definition_name_;
        rouen::models::productivity::process_definition_repository repo_;
        RouenGPUTexture* icon_texture_{nullptr};
        bool icon_loaded_{false};

        auto_scale_meter thread_meter_;
        auto_scale_meter subprocess_meter_;
        auto_scale_meter handle_meter_;
        auto_scale_meter memory_meter_;

        std::string working_directory_;
        bool track_file_changes_{false};
        std::unique_ptr<rouen::helpers::directory_watch> watcher_;

        struct changed_file_entry {
            std::string path;
            std::string changed_at;
        };
        std::vector<changed_file_entry> changed_files_;

        std::optional<rouen::helpers::ui_automation_result> ui_tree_result_;
        const rouen::helpers::ui_element_node* selected_ui_node_{nullptr};
        char ui_search_buf_[128]{};
        int ui_max_depth_{6};
        int ui_explorer_mode_{0}; // 0 = Tree View, 1 = Extracted Values
        char ui_request_query_buf_[128]{};
        std::optional<std::string> requested_control_value_;
        bool value_request_attempted_{false};
    };

} // namespace rouen::cards
