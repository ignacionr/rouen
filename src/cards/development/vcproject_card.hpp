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
#include "../../helpers/vc_toolchain.hpp"
#include "../../helpers/syntax_checker.hpp"
#include "../../helpers/conventional_commit.hpp"

namespace rouen::cards {

    struct vcproject_card : public card {
        explicit vcproject_card(std::string_view path);

        [[nodiscard]] std::string get_uri() const override;
        bool render() override;

        void read_project();
        bool run_msbuild_action(const std::string& action, const std::string& explanation);
        void cancel_running_action();
        void handle_action(std::string_view action_json) override;

        // Code Workflow Actions
        void check_syntax();
        void triage_build_error_with_ai(const rouen::helpers::Diagnostic& diag);
        void generate_conventional_commit();
        void commit_with_conventional_message();

        // Stack & Target Configuration
        void select_stack(size_t index);
        void set_configuration(const std::string& config);
        void set_platform(const std::string& platform);

    private:
        std::string path_;
        std::string project_dir_;
        rouen::helpers::VCProjectInfo proj_info_;
        std::vector<rouen::helpers::VSInstallation> installations_;
        int selected_stack_idx_ = 0;
        int selected_config_idx_ = 0;
        int selected_platform_idx_ = 0;

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

        // Workflow Integration State
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
