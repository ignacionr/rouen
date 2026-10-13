#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

#include "../interface/card.hpp"
#include "../../helpers/syntax_checker.hpp"
#include "../../helpers/conventional_commit.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"

namespace rouen::cards {

    struct cmake_card : public card {
        explicit cmake_card(std::string_view path);

        [[nodiscard]] std::string get_uri() const override;
        bool render() override;
        bool render(rouen::ui::ui_context& ui) override;

        void read_cmake_file();
        bool run_cmake_action(const std::string& action, const std::string& explanation);
        void cancel_running_action();
        
        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override;

        /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
        void handle_action(std::string_view action_json) override;

        [[nodiscard]] const std::string& get_build_type() const noexcept { return build_type_; }
        [[nodiscard]] const std::string& get_selected_target() const noexcept { return selected_target_; }
        [[nodiscard]] bool is_building() const noexcept { return cmd_running_; }
        [[nodiscard]] bool is_cmd_running() const noexcept { return cmd_running_; }
        [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
        [[nodiscard]] const std::string& get_last_command() const noexcept { return last_cmd_; }
        [[nodiscard]] const std::string& get_last_output() const noexcept { return last_output_; }
        [[nodiscard]] bool is_checking_syntax() const noexcept { return is_checking_syntax_; }
        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }

        // Phase 7 Actions
        void check_syntax();
        void triage_build_error_with_ai(const rouen::helpers::Diagnostic& diag);
        void generate_conventional_commit();
        void commit_with_conventional_message();

    private:
        std::string path_;
        std::filesystem::path build_dir_;
        std::string project_name_;
        std::string project_version_;
        std::vector<std::string> targets_;
        std::string build_type_{"Release"};
        std::string selected_target_{"rouen"};

        // Adaptive Card presentation override state
        bool adaptive_view_mode{false};
        mutable helpers::adaptive_cards::parser adaptive_parser_{};
        mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
        mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
        mutable helpers::adaptive_cards::card_document adaptive_bound_{};
        std::string adaptive_error_{};
        std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

        std::string error_message_;
        std::string last_output_;
        std::string last_cmd_;
        std::string last_action_;
        bool cmd_running_ = false;
        std::chrono::steady_clock::time_point start_time_;
#ifdef _WIN32
        DWORD process_pid_ = 0;
#else
        pid_t process_pid_ = 0;
#endif

        // Phase 7 Workflow Integration State
        bool is_checking_syntax_ = false;
        std::optional<rouen::helpers::SyntaxCheckResult> syntax_result_;
        char syntax_target_file_[512] = "";

        std::vector<rouen::helpers::Diagnostic> build_diagnostics_;
        bool show_build_triage_ = true;
        std::string triage_ai_feedback_;

        bool show_commit_dialog_ = false;
        bool is_generating_commit_ = false;
        char commit_message_buf_[2048] = "";
        std::string commit_status_feedback_;
        std::string git_status_preview_;
    };

} // namespace rouen::cards
