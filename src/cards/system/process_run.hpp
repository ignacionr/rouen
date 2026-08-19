#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <vector>
#include <cmath>
#include <format>
#include <algorithm>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../helpers/app_icon.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/vu_meter.hpp"
#include "../../models/productivity/process_definition.hpp"
#include "../../hosts/process_host.hpp"

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

        ~process_run() override {
            if (icon_texture_) TextureHelper::destroyTexture(icon_texture_);
        }

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
                }

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
    };

} // namespace rouen::cards
