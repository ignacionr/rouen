#pragma once

// 1. Standard includes in alphabetic order
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/adaptive_process_profiles.hpp"
#include "../../helpers/auth_service.hpp"
#include "../../helpers/command_audit_log.hpp"
#include "../../helpers/piped_process.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"

// A card whose content is an Adaptive Card produced by an external
// process, rather than a static file/database record (adaptive_card.hpp)
// or a compiled-in plugin (adaptive_card_plugin_adapter.hpp). The
// locator is the process's full command line, launched with its stdin,
// stdout, and stderr piped (helpers::piped_process). Protocol:
//   - Each complete line the process writes to stdout is one compact
//     Adaptive Card JSON document, replacing the currently shown card.
//   - When the user activates Action.Submit or Action.Execute, the
//     resulting JSON payload is written to the process's stdin as one
//     line, so the process can compute and print the next card.
//   - Action.OpenUrl is handled by the host only (opened with the OS's
//     default handler) and is not sent to the process.
// This lets any executable in any language become a live Rouen card
// without linking against Rouen at all - contrast with the DLL-based
// plugin styles in plugin-sdk/rouen_plugin_api.hpp, which need a
// matching C++ toolchain/ABI. See docs/ADAPTIVE_PROCESS_CARDS.md.
namespace rouen::cards {

    class adaptive_process_card : public card {
    public:
        explicit adaptive_process_card(std::string_view locator) {
            colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
            colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
            width = 460.0f;
            command_line_ = std::string(locator);
            apply_title();
            start_process();
        }

        ~adaptive_process_card() override {
            process_.reset();
        }

        [[nodiscard]] std::string get_uri() const override {
            return command_line_.empty() ? "adaptive-process" : std::format("adaptive-process:{}", command_line_);
        }

        [[nodiscard]] bool matches_uri(std::string_view uri) const override {
            return uri == "adaptive-process" || uri.starts_with("adaptive-process:");
        }

        void handle_uri(std::string_view uri) override {
            static constexpr std::string_view prefix = "adaptive-process:";
            std::string target(uri);
            if (target.starts_with(prefix)) {
                target = target.substr(prefix.size());
            }
            if (target == command_line_) {
                return;
            }
            process_.reset();
            command_line_ = target;
            apply_title();
            reset_state();
            start_process();
        }

        bool render() override {
            return render_window([this]() {
                tick_background();
                render_status_line();
                ImGui::Separator();

                if (command_line_.empty()) {
                    ImGui::TextWrapped(
                        "No command configured. Use a URI like "
                        "adaptive-process:python my_card.py to launch one.");
                    return;
                }

                if (!error_.empty()) {
                    ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, "%s", error_.c_str());
                    ImGui::Separator();
                }

                if (bound_.body.empty() && bound_.actions.empty()) {
                    ImGui::TextColored(ImVec4{0.7f, 0.7f, 0.7f, 1.0f}, "Waiting for the process to print its first card...");
                } else {
                    renderer_.render(
                        bound_, input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](std::string const& url) { static_cast<void>(rouen::platform::open_url(url)); },
                            .on_submit = [this](std::string const& payload) { post_to_process(payload); }},
                        helpers::adaptive_cards::render_config{
                            .font_bold = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code = rouen::fonts::get_font(rouen::fonts::FontType::Mono)});
                }

                render_stderr_log();
            });
        }

        // Main-thread only: pulls the latest state written by the
        // background threads above and applies it to the ImGui-facing
        // members (bound_, error_, ...).
        void apply_pending_update() {
            std::string card_json;
            bool has_update = false;
            {
                std::lock_guard<std::mutex> const lock(mutex_);
                has_update = has_pending_update_;
                if (has_update) {
                    card_json = pending_card_json_;
                    has_pending_update_ = false;
                }
            }
            if (has_update) {
                rebuild_from_json(card_json);
            }
        }

        void tick_background() override {
            apply_pending_update();
            if (bound_.refreshIntervalMs > 0 && process_ && process_->spawn_succeeded()) {
                bool is_exited = false;
                {
                    std::lock_guard<std::mutex> const lock(mutex_);
                    is_exited = exited_;
                }
                if (!is_exited) {
                    auto const now = std::chrono::steady_clock::now();
                    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_time_).count();
                    if (elapsed_ms >= bound_.refreshIntervalMs) {
                        last_tick_time_ = now;
                        post_to_process(R"({"verb":"tick"})");
                    }
                }
            }
        }

        [[nodiscard]] helpers::adaptive_cards::card_document const& bound_document() const {
            return bound_;
        }

        [[nodiscard]] std::string const& last_error() const {
            return error_;
        }

        [[nodiscard]] bool has_pending_update() const {
            std::lock_guard<std::mutex> const lock(mutex_);
            return has_pending_update_;
        }

        [[nodiscard]] std::string get_adaptive_card_json() const override {
            auto* self = const_cast<adaptive_process_card*>(this);
            // If just launched and waiting for initial stdout line, wait briefly
            auto const start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
                bool ready = false;
                {
                    std::lock_guard<std::mutex> const lock(mutex_);
                    ready = !pending_card_json_.empty() || !last_valid_card_json_.empty() || exited_ || !process_ || !process_->spawn_succeeded();
                }
                if (ready) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            self->tick_background();
            std::lock_guard<std::mutex> const lock(mutex_);
            return !last_valid_card_json_.empty() ? last_valid_card_json_ : pending_card_json_;
        }

        void handle_action(std::string_view action_json) override {
            post_to_process(std::string(action_json));
        }

    private:
        void apply_title() {
            name(command_line_.empty() ? "Adaptive Process" : command_line_);
        }

        void reset_state() {
            bound_ = {};
            input_state_ = {};
            error_.clear();
            last_tick_time_ = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> const lock(mutex_);
            pending_card_json_.clear();
            last_valid_card_json_.clear();
            has_pending_update_ = false;
            stderr_lines_.clear();
            exited_ = false;
            exit_code_ = 0;
        }

        void start_process() {
            if (command_line_.empty()) {
                return;
            }
            auto const resolution = helpers::adaptive_process_profiles::instance().resolve(command_line_);
            if (!resolution.resolved_title.empty()) {
                name(resolution.resolved_title);
            }
            if (!resolution.allowed) {
                error_ = resolution.error_message;
                return;
            }
            process_ = std::make_unique<helpers::piped_process>(
                resolution.resolved_command,
                [this](std::string const& line) { on_stdout_line(line); },
                [this](std::string const& line) { on_stderr_line(line); },
                [this](int code) { on_process_exit(code); });
            if (!process_->spawn_succeeded()) {
                error_ = std::format("Failed to start process: {}", process_->spawn_error());
            }
        }

        void restart_process() {
            process_.reset();
            reset_state();
            start_process();
        }

        [[nodiscard]] std::string find_required_role_for_verb(std::string const& verb) const {
            if (verb.empty()) {
                return "";
            }
            for (auto const& act : bound_.actions) {
                if (act.verb == verb && !act.requiredRole.empty()) {
                    return act.requiredRole;
                }
            }
            auto search_elements = [&](auto const& self, std::vector<helpers::adaptive_cards::element> const& elems) -> std::string {
                for (auto const& el : elems) {
                    for (auto const& act : el.actions) {
                        if (act.verb == verb && !act.requiredRole.empty()) {
                            return act.requiredRole;
                        }
                    }
                    if (!el.items.empty()) {
                        auto r = self(self, el.items);
                        if (!r.empty()) return r;
                    }
                    if (!el.columns.empty()) {
                        auto r = self(self, el.columns);
                        if (!r.empty()) return r;
                    }
                }
                return "";
            };
            return search_elements(search_elements, bound_.body);
        }

        void post_to_process(std::string const& payload_json) {
            glz::json_t parsed_obj{};
            std::string verb;
            std::string operator_reason;
            if (!glz::read_json(parsed_obj, payload_json) && parsed_obj.is_object()) {
                auto const& obj = parsed_obj.get_object();
                if (auto it = obj.find("verb"); it != obj.end() && it->second.is_string()) {
                    verb = it->second.get<std::string>();
                }
                if (auto it = obj.find("operatorReason"); it != obj.end() && it->second.is_string()) {
                    operator_reason = it->second.get<std::string>();
                } else if (auto dit = obj.find("data"); dit != obj.end() && dit->second.is_object()) {
                    auto const& dobj = dit->second.get_object();
                    if (auto rit = dobj.find("operatorReason"); rit != dobj.end() && rit->second.is_string()) {
                        operator_reason = rit->second.get<std::string>();
                    }
                }
            }

            if (verb == "tick") {
                if (process_) {
                    std::string const sanitized = services::auth_service::sanitize_and_inject_operator(payload_json);
                    static_cast<void>(process_->write_line(sanitized));
                }
                return;
            }

            std::string const required_role = find_required_role_for_verb(verb);
            auto cfg = helpers::ConfigService::instance();
            std::string const require_auth_env = cfg->get_env("ROUEN_REQUIRE_OPERATOR_AUTH");
            bool const auth_required = !required_role.empty() || require_auth_env == "1" || require_auth_env == "true";
            auto const op = services::auth_service::current_effective_operator();
            std::string const source_node = cfg->get_env("ROUEN_MESH_CLIENT_ID").empty()
                ? "local"
                : cfg->get_env("ROUEN_MESH_CLIENT_ID");

            if (auth_required && !op.has_value()) {
                error_ = std::format("401 Unauthorized: Operator authentication required for action '{}'", verb.empty() ? "submit" : verb);
                helpers::command_audit_log::instance().record(
                    services::verified_operator_identity{},
                    source_node,
                    get_uri(),
                    verb.empty() ? "submit" : verb,
                    payload_json,
                    operator_reason,
                    "DENIED_UNAUTHENTICATED");
                return;
            }

            if (!required_role.empty() && op.has_value() && !op->has_role(required_role)) {
                error_ = std::format(
                    "403 Forbidden: Operator '{}' lacks required role '{}' for action '{}'",
                    op->email, required_role, verb.empty() ? "submit" : verb);
                helpers::command_audit_log::instance().record(
                    *op,
                    source_node,
                    get_uri(),
                    verb.empty() ? "submit" : verb,
                    payload_json,
                    operator_reason,
                    "DENIED_INSUFFICIENT_ROLE");
                return;
            }

            error_.clear();
            std::string const sanitized = services::auth_service::sanitize_and_inject_operator(payload_json);
            helpers::command_audit_log::instance().record(
                op.value_or(services::verified_operator_identity{}),
                source_node,
                get_uri(),
                verb.empty() ? "submit" : verb,
                sanitized,
                operator_reason,
                "EXECUTED");

            if (process_) {
                static_cast<void>(process_->write_line(sanitized));
            }
        }

        // Called from piped_process's background reader thread.
        void on_stdout_line(std::string const& line) {
            std::lock_guard<std::mutex> const lock(mutex_);
            pending_card_json_ = line;
            has_pending_update_ = true;
        }

        // Called from piped_process's background reader thread.
        void on_stderr_line(std::string const& line) {
            std::lock_guard<std::mutex> const lock(mutex_);
            stderr_lines_.push_back(line);
            if (stderr_lines_.size() > kMaxStderrLines) {
                stderr_lines_.pop_front();
            }
        }

        // Called from piped_process's background wait thread.
        void on_process_exit(int code) {
            std::lock_guard<std::mutex> const lock(mutex_);
            exited_ = true;
            exit_code_ = code;
        }


        void rebuild_from_json(std::string const& card_json) {
            try {
                bound_ = parser_.parse(card_json);
                input_state_ = {};
                error_.clear();
                last_tick_time_ = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> const lock(mutex_);
                last_valid_card_json_ = card_json;
            } catch (std::exception const& e) {
                error_ = std::format("Adaptive Process card error: {}", e.what());
            }
        }

        void render_status_line() {
            bool exited = false;
            int exit_code = 0;
            {
                std::lock_guard<std::mutex> const lock(mutex_);
                exited = exited_;
                exit_code = exit_code_;
            }
            if (command_line_.empty()) {
                return;
            }
            if (exited) {
                ImGui::TextColored(ImVec4{1.0f, 0.6f, 0.3f, 1.0f}, "Process exited (code %d)", exit_code);
                ImGui::SameLine();
            } else if (process_ && process_->spawn_succeeded()) {
                ImGui::TextColored(ImVec4{0.4f, 0.85f, 0.5f, 1.0f}, "Running");
                ImGui::SameLine();
            }
            if (ImGui::Button("Restart")) {
                restart_process();
            }
        }

        void render_stderr_log() {
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> const lock(mutex_);
                lines.assign(stderr_lines_.begin(), stderr_lines_.end());
            }
            if (lines.empty()) {
                return;
            }
            ImGui::Separator();
            if (ImGui::TreeNode("Process stderr")) {
                ImGui::BeginChild("AdaptiveProcessStderr", ImVec2(0.0f, 120.0f), true);
                for (auto const& line : lines) {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndChild();
                ImGui::TreePop();
            }
        }

        static constexpr size_t kMaxStderrLines = 200;

        std::string command_line_;
        std::unique_ptr<helpers::piped_process> process_;

        mutable std::mutex mutex_;
        std::string pending_card_json_;
        std::string last_valid_card_json_;
        bool has_pending_update_{false};
        std::deque<std::string> stderr_lines_;
        bool exited_{false};
        int exit_code_{0};
        std::chrono::steady_clock::time_point last_tick_time_{std::chrono::steady_clock::now()};

        helpers::adaptive_cards::parser parser_{};
        helpers::adaptive_cards::renderer renderer_{};
        helpers::adaptive_cards::renderer::input_state input_state_{};
        helpers::adaptive_cards::card_document bound_{};
        std::string error_{};
    };

} // namespace rouen::cards
