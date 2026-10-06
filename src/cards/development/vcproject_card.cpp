#include "vcproject_card.hpp"

#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <tlhelp32.h>
#endif

#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/persona_manager.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/syntax_checker.hpp"
#include "../../helpers/conventional_commit.hpp"
#include "../../../external/IconsMaterialDesign.h"
#include "../../registrar.hpp"

namespace rouen::cards {

    vcproject_card::vcproject_card(std::string_view path)
        : path_(path), start_time_(std::chrono::steady_clock::now()) {
        // Visual Studio theme colors: Purple / Violet
        colors[0] = {0.55f, 0.35f, 0.75f, 1.0f};  // Primary - VS Purple
        colors[1] = {0.70f, 0.50f, 0.90f, 0.7f};  // Secondary - Light Violet

        get_color(2, {0.85f, 0.35f, 0.35f, 1.0f}); // Error - Red
        get_color(3, {0.35f, 0.85f, 0.45f, 1.0f}); // Success - Green
        get_color(4, {0.90f, 0.75f, 0.35f, 1.0f}); // Warning - Yellow

        width = 560.0f;

        // Resolve file vs directory path
        std::filesystem::path p(path_);
        if (std::filesystem::is_directory(p)) {
            project_dir_ = p.string();
            // Look for .vcxproj or .sln in directory
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                if (entry.path().extension() == ".vcxproj") {
                    path_ = entry.path().string();
                    break;
                }
            }
            if (path_ == p.string()) {
                // If no .vcxproj found, check for .sln
                for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                    if (entry.path().extension() == ".sln") {
                        path_ = entry.path().string();
                        break;
                    }
                }
            }
        } else {
            project_dir_ = p.parent_path().string();
        }

        name(std::format("VC Project: {}", std::filesystem::path(path_).filename().string()));

        // Discover installed Visual Studio toolchains
        installations_ = rouen::helpers::VCToolchainService::instance().get_installations();

        // Read and parse project metadata
        read_project();
    }

    std::string vcproject_card::get_uri() const {
        return std::format("vcxproj:{}", path_);
    }

    void vcproject_card::read_project() {
        try {
            proj_info_ = rouen::helpers::VCToolchainService::parse_project_file(path_);
            if (!proj_info_.is_valid) {
                error_message_ = "Could not open or parse Visual C++ project file";
                return;
            }

            // Set configuration indices (prefer Release and x64)
            for (size_t i = 0; i < proj_info_.configurations.size(); ++i) {
                if (proj_info_.configurations[i] == "Release") {
                    selected_config_idx_ = static_cast<int>(i);
                    break;
                }
            }
            for (size_t i = 0; i < proj_info_.platforms.size(); ++i) {
                if (proj_info_.platforms[i] == "x64") {
                    selected_platform_idx_ = static_cast<int>(i);
                    break;
                }
            }

            // If a VS installation is available, enrich with MSBuild metadata
            if (!installations_.empty() && selected_stack_idx_ >= 0 && static_cast<size_t>(selected_stack_idx_) < installations_.size()) {
                std::string cfg = (selected_config_idx_ >= 0 && static_cast<size_t>(selected_config_idx_) < proj_info_.configurations.size())
                    ? proj_info_.configurations[static_cast<size_t>(selected_config_idx_)] : "Release";
                std::string plat = (selected_platform_idx_ >= 0 && static_cast<size_t>(selected_platform_idx_) < proj_info_.platforms.size())
                    ? proj_info_.platforms[static_cast<size_t>(selected_platform_idx_)] : "x64";
                rouen::helpers::VCToolchainService::enrich_project_info_with_msbuild(
                    proj_info_, path_, installations_[static_cast<size_t>(selected_stack_idx_)], cfg, plat);
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Error reading project: {}", e.what());
        }
    }

    void vcproject_card::select_stack(size_t index) {
        if (index < installations_.size()) {
            selected_stack_idx_ = static_cast<int>(index);
            // Re-enrich with the newly selected stack
            if (!proj_info_.configurations.empty() && !proj_info_.platforms.empty()) {
                std::string cfg = (selected_config_idx_ >= 0 && static_cast<size_t>(selected_config_idx_) < proj_info_.configurations.size())
                    ? proj_info_.configurations[static_cast<size_t>(selected_config_idx_)] : proj_info_.configurations[0];
                std::string plat = (selected_platform_idx_ >= 0 && static_cast<size_t>(selected_platform_idx_) < proj_info_.platforms.size())
                    ? proj_info_.platforms[static_cast<size_t>(selected_platform_idx_)] : proj_info_.platforms[0];
                rouen::helpers::VCToolchainService::enrich_project_info_with_msbuild(
                    proj_info_, path_, installations_[static_cast<size_t>(selected_stack_idx_)], cfg, plat);
            }
        }
    }

    void vcproject_card::set_configuration(const std::string& config) {
        for (size_t i = 0; i < proj_info_.configurations.size(); ++i) {
            if (proj_info_.configurations[i] == config) {
                selected_config_idx_ = static_cast<int>(i);
                break;
            }
        }
    }

    void vcproject_card::set_platform(const std::string& platform) {
        for (size_t i = 0; i < proj_info_.platforms.size(); ++i) {
            if (proj_info_.platforms[i] == platform) {
                selected_platform_idx_ = static_cast<int>(i);
                break;
            }
        }
    }

    bool vcproject_card::run_msbuild_action(const std::string& action, const std::string& explanation) {
        if (cmd_running_) {
            return false;
        }

        std::string cfg = (!proj_info_.configurations.empty() && selected_config_idx_ >= 0 && static_cast<size_t>(selected_config_idx_) < proj_info_.configurations.size())
            ? proj_info_.configurations[static_cast<size_t>(selected_config_idx_)] : "Release";
        std::string plat = (!proj_info_.platforms.empty() && selected_platform_idx_ >= 0 && static_cast<size_t>(selected_platform_idx_) < proj_info_.platforms.size())
            ? proj_info_.platforms[static_cast<size_t>(selected_platform_idx_)] : "x64";

        std::string cmd;
        if (action == "open_dir") {
            cmd = platform::open_file(project_dir_);
        } else if (action == "run") {
            // Find executable
            std::string exe_path = proj_info_.target_path;
            if (exe_path.empty() || !std::filesystem::exists(exe_path)) {
                // Check default locations
                std::vector<std::filesystem::path> exe_candidates = {
                    std::filesystem::path(project_dir_) / plat / cfg / (proj_info_.project_name + ".exe"),
                    std::filesystem::path(project_dir_) / cfg / (proj_info_.project_name + ".exe"),
                    std::filesystem::path(project_dir_) / "bin" / plat / cfg / (proj_info_.project_name + ".exe")
                };
                for (const auto& c : exe_candidates) {
                    if (std::filesystem::exists(c)) {
                        exe_path = c.string();
                        break;
                    }
                }
            }

            if (exe_path.empty() || !std::filesystem::exists(exe_path)) {
                last_output_ = std::format("Error: Target executable not found. Please build the project first.");
                return false;
            }

            cmd = std::format("\"{}\"", exe_path);
        } else {
            rouen::helpers::VSInstallation vs;
            if (!installations_.empty() && selected_stack_idx_ >= 0 && static_cast<size_t>(selected_stack_idx_) < installations_.size()) {
                vs = installations_[static_cast<size_t>(selected_stack_idx_)];
            } else {
                vs.msbuild_path = "MSBuild.exe";
            }

            std::string target_file;
            if (action == "check_syntax") {
                target_file = syntax_target_file_;
            }

            cmd = rouen::helpers::VCToolchainService::build_command(vs, path_, action, cfg, plat, target_file);
        }

        if (cmd.empty()) {
            return false;
        }

        process_pid_ = 0;
        last_output_.clear();
        build_diagnostics_.clear();
        triage_ai_feedback_.clear();
        start_time_ = std::chrono::steady_clock::now();

        auto output_func = std::make_shared<std::function<void(std::string)>>(
            [this](std::string output) {
                if (output.find("<PROCESS_COMPLETED>") != std::string::npos) {
                    auto marker_pos = output.find("<PROCESS_COMPLETED>");
                    if (marker_pos != std::string::npos) {
                        output = output.substr(0, marker_pos);
                    }
                    this->last_output_ = output;
                    this->cmd_running_ = false;
                    this->process_pid_ = 0;
                    this->build_diagnostics_ = rouen::helpers::SyntaxChecker::parse_compiler_output(output, this->path_);
                    if (!this->build_diagnostics_.empty()) {
                        this->show_build_triage_ = true;
                    }
                    return;
                }

                this->last_output_ = output;
                this->cmd_running_ = true;

                if (this->process_pid_ == 0) {
                    auto pid_pos = output.find("PID:");
                    if (pid_pos != std::string::npos) {
                        auto pid_end = output.find('\n', pid_pos);
                        if (pid_end != std::string::npos) {
                            auto pid_str = output.substr(pid_pos + 4, pid_end - (pid_pos + 4));
                            try {
                                this->process_pid_ = std::stoi(pid_str);
                            } catch (...) {}
                        }
                    }
                }

                if (output.find("Build succeeded.") != std::string::npos ||
                    output.find("Build FAILED.") != std::string::npos ||
                    output.find("Done Building Project") != std::string::npos ||
                    output.find("Process exited with code:") != std::string::npos) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    this->cmd_running_ = false;
                    this->process_pid_ = 0;
                    this->build_diagnostics_ = rouen::helpers::SyntaxChecker::parse_compiler_output(output, this->path_);
                    if (!this->build_diagnostics_.empty()) {
                        this->show_build_triage_ = true;
                    }
                }
            });

        if (action != "open_dir") {
            cmd = std::format("echo PID:$$ && {}", cmd);
        }

        "run_command"_sfn2(cmd, output_func);
        cmd_running_ = true;
        last_cmd_ = cmd;
        last_action_ = explanation;

        return true;
    }

    void vcproject_card::cancel_running_action() {
        if (!cmd_running_) return;

        if (process_pid_ > 0) {
#ifdef _WIN32
            std::string kill_cmd = std::format("taskkill /F /T /PID {}", process_pid_);
#else
            std::string const kill_cmd = std::format("kill -TERM -{}", process_pid_);
#endif
            auto output_func = std::make_shared<std::function<void(std::string)>>(
                [this](const std::string& output) {
                    this->last_output_ += "\n\n[ACTION CANCELLED BY USER]\n\n" + output;
                }
            );
            "run_command"_sfn2(kill_cmd, output_func);
        }

        cmd_running_ = false;
        process_pid_ = 0;
    }

    void vcproject_card::check_syntax() {
        if (is_checking_syntax_) return;

        std::string target_file = syntax_target_file_;

        // 1. Check active editor file
        if (target_file.empty()) {
            try {
                auto get_active = registrar::get<std::function<std::string()>>("editor_get_active_file");
                if (get_active && *get_active) {
                    target_file = (*get_active)();
                }
            } catch (...) {}
        }

        // 2. Fall back to first ClCompile item in project
        if (target_file.empty() && !proj_info_.source_files.empty()) {
            target_file = proj_info_.source_files.front();
            if (!std::filesystem::path(target_file).is_absolute()) {
                target_file = (std::filesystem::path(project_dir_) / target_file).string();
            }
        }

        if (target_file.empty()) {
            target_file = path_;
        }

        std::strncpy(syntax_target_file_, target_file.c_str(), sizeof(syntax_target_file_) - 1);
        syntax_target_file_[sizeof(syntax_target_file_) - 1] = '\0';

        is_checking_syntax_ = true;
        syntax_result_.reset();

        std::string proj_dir = project_dir_;
        std::thread([this, target_file, proj_dir]() {
            auto result = rouen::helpers::SyntaxChecker::instance().check_file(target_file, proj_dir);
            this->syntax_result_ = result;
            this->is_checking_syntax_ = false;
        }).detach();
    }

    void vcproject_card::triage_build_error_with_ai(const rouen::helpers::Diagnostic& diag) {
        rouen::helpers::PersonaManager::instance().select_persona_by_name("Code & Git Architect");

        std::string snippet;
        std::filesystem::path file_p = diag.file;
        if (!file_p.is_absolute()) {
            file_p = std::filesystem::path(project_dir_) / file_p;
        }

        if (std::filesystem::exists(file_p)) {
            try {
                std::ifstream file(file_p);
                std::string line;
                std::vector<std::string> lines;
                while (std::getline(file, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    lines.push_back(line);
                }
                int start_l = std::max(1, diag.line - 8);
                int end_l = std::min(static_cast<int>(lines.size()), diag.line + 8);
                for (int i = start_l; i <= end_l; ++i) {
                    snippet += std::format("{:4d} | {}\n", i, lines[static_cast<size_t>(i - 1)]);
                }
            } catch (...) {}
        }

        std::string prompt = std::format(
            "Please investigate and resolve this build error encountered during MSBuild/MSVC compilation:\n\n"
            "- **Project**: `{}`\n"
            "- **Failed Action**: `{}`\n"
            "- **Command**: `{}`\n"
            "- **Target File**: `{}`\n"
            "- **Line**: {}, **Column**: {}\n"
            "- **Severity**: `{}`\n"
            "- **Compiler Diagnostic**: `{}`\n\n"
            "**Code Context (Lines around {})**:\n"
            "```cpp\n"
            "{}"
            "```\n\n"
            "Please inspect the issue, explain the root cause concisely, and apply the correction using `code_apply_patch`.\n"
            "After applying the patch, verify the fix using `code_check_syntax`.",
            proj_info_.project_name.empty() ? "VC Project" : proj_info_.project_name,
            last_action_,
            last_cmd_,
            diag.file,
            diag.line,
            diag.column,
            diag.severity,
            diag.message,
            diag.line,
            snippet.empty() ? "(Code context could not be read from disk)" : snippet
        );

        auto send_fn = registrar::get<std::function<void(const std::string&)>>("ai_chat_send_message");
        if (send_fn && *send_fn) {
            (*send_fn)(prompt);
        } else {
            "create_card"_sfn(std::format("ai_chat:{}", ::helpers::StringHelper::url_encode(prompt)));
        }

        triage_ai_feedback_ = std::format("Dispatched {} line {} error to Code & Git Architect", 
            std::filesystem::path(diag.file).filename().string(), diag.line);
    }

    void vcproject_card::generate_conventional_commit() {
        if (is_generating_commit_) return;

        is_generating_commit_ = true;
        show_commit_dialog_ = true;
        commit_status_feedback_.clear();

        std::string project_dir = project_dir_;
        std::thread([this, project_dir]() {
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string status = ::ProcessHelper::executeCommandInDirectory(project_dir, git_path + " status --short");
            std::string diff = ::ProcessHelper::executeCommandInDirectory(project_dir, git_path + " diff --cached");
            if (diff.empty()) {
                diff = ::ProcessHelper::executeCommandInDirectory(project_dir, git_path + " diff");
            }

            this->git_status_preview_ = status;

            if (status.empty() && diff.empty()) {
                std::strncpy(this->commit_message_buf_, "chore: working directory clean, no changes detected", sizeof(this->commit_message_buf_) - 1);
                this->commit_message_buf_[sizeof(this->commit_message_buf_) - 1] = '\0';
                this->is_generating_commit_ = false;
                return;
            }

            std::string msg = rouen::helpers::ConventionalCommitGenerator::generate_commit_message(
                diff, status, this->proj_info_.project_name
            );

            if (msg.empty()) {
                msg = "chore: update visual studio project sources and configuration";
            }

            std::strncpy(this->commit_message_buf_, msg.c_str(), sizeof(this->commit_message_buf_) - 1);
            this->commit_message_buf_[sizeof(this->commit_message_buf_) - 1] = '\0';
            this->is_generating_commit_ = false;
        }).detach();
    }

    void vcproject_card::commit_with_conventional_message() {
        std::string msg = commit_message_buf_;
        if (msg.empty()) {
            commit_status_feedback_ = "Commit message cannot be empty.";
            return;
        }

        std::string git_path = CONFIG_SERVICE()->get_git_path();

        std::string staged_check = ::ProcessHelper::executeCommandInDirectory(project_dir_, git_path + " diff --cached --name-only");
        if (staged_check.empty()) {
            ::ProcessHelper::executeCommandInDirectory(project_dir_, git_path + " add -A");
        }

        auto temp_msg_path = std::filesystem::temp_directory_path() / "rouen_vc_commit_msg.txt";
        {
            std::ofstream out(temp_msg_path);
            out << msg;
        }

        std::string cmd = std::format("{} commit -F \"{}\"", git_path, temp_msg_path.string());
        std::string result = ::ProcessHelper::executeCommandInDirectory(project_dir_, cmd);
        std::filesystem::remove(temp_msg_path);

        commit_status_feedback_ = result.empty() ? "Committed successfully." : result;
        show_commit_dialog_ = false;
    }

    bool vcproject_card::render() {
        return render_window([this]() {
            // Header
            ImGui::TextColored(colors[0], "%s Visual Studio Project: %s", 
                ICON_MD_TERMINAL, proj_info_.project_name.empty() ? "Unknown" : proj_info_.project_name.c_str());

            if (!proj_info_.platform_toolset.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(colors[1], "[%s | %s]", proj_info_.platform_toolset.c_str(), proj_info_.language_standard.c_str());
            }

            // Visual Studio Stack Selection
            if (!installations_.empty()) {
                if (installations_.size() > 1) {
                    std::vector<std::string> stack_names;
                    for (const auto& inst : installations_) {
                        stack_names.push_back(std::format("{} ({})", inst.display_name, inst.toolset));
                    }
                    std::vector<const char*> stack_ptrs;
                    for (const auto& s : stack_names) stack_ptrs.push_back(s.c_str());

                    int prev_idx = selected_stack_idx_;
                    if (ImGui::Combo("VS Stack", &selected_stack_idx_, stack_ptrs.data(), static_cast<int>(stack_ptrs.size()))) {
                        if (selected_stack_idx_ != prev_idx) {
                            select_stack(static_cast<size_t>(selected_stack_idx_));
                        }
                    }
                } else {
                    ImGui::TextColored(colors[3], "%s %s", ICON_MD_CHECK, installations_[0].display_name.c_str());
                    if (!installations_[0].msbuild_path.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(MSBuild: %s)", std::filesystem::path(installations_[0].msbuild_path).filename().string().c_str());
                    }
                }
            } else {
                ImGui::TextColored(colors[4], "%s No Visual Studio installation detected via vswhere. Using system MSBuild.", ICON_MD_INFO);
            }

            // Configuration & Platform Dropdowns
            if (!proj_info_.configurations.empty() || !proj_info_.platforms.empty()) {
                std::vector<const char*> cfg_ptrs;
                for (const auto& c : proj_info_.configurations) cfg_ptrs.push_back(c.c_str());
                std::vector<const char*> plat_ptrs;
                for (const auto& p : proj_info_.platforms) plat_ptrs.push_back(p.c_str());

                ImGui::SetNextItemWidth(140.0f);
                ImGui::Combo("Config", &selected_config_idx_, cfg_ptrs.data(), static_cast<int>(cfg_ptrs.size()));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                ImGui::Combo("Platform", &selected_platform_idx_, plat_ptrs.data(), static_cast<int>(plat_ptrs.size()));
            }

            ImGui::Separator();
            ImGui::TextColored(colors[0], "Actions");

            struct DisabledGuard {
                bool disabled;
                explicit DisabledGuard(bool is_disabled) : disabled(is_disabled) {
                    if (disabled) ImGui::BeginDisabled();
                }
                ~DisabledGuard() {
                    if (disabled) ImGui::EndDisabled();
                }
            };

            bool const current_cmd_running = cmd_running_;
            {
                DisabledGuard const disabled_guard(current_cmd_running);

                if (ImGui::Button("Build")) {
                    run_msbuild_action("build", "Building Visual C++ project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Rebuild")) {
                    run_msbuild_action("rebuild", "Rebuilding Visual C++ project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Clean")) {
                    run_msbuild_action("clean", "Cleaning Visual C++ project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Run")) {
                    run_msbuild_action("run", "Executing target binary");
                }
                ImGui::SameLine();

                if (ImGui::Button("Open Folder")) {
                    run_msbuild_action("open_dir", "Opening project directory");
                }

                ImGui::Separator();
                ImGui::TextColored(colors[0], "Code Workflow");

                if (ImGui::Button("Check Syntax Only")) {
                    check_syntax();
                }
                ImGui::SameLine();

                if (ImGui::Button("Conventional Commit")) {
                    generate_conventional_commit();
                }
                ImGui::SameLine();

                if (ImGui::Button("Visual Diff")) {
                    "create_card"_sfn(std::format("diff:{}", project_dir_));
                }
            }

            if (current_cmd_running) {
                ImGui::Separator();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    cancel_running_action();
                }
                ImGui::SameLine();
                ImGui::TextColored(colors[2], "Terminate the running MSBuild process");
            }

            // Syntax check feedback
            if (is_checking_syntax_) {
                ImGui::TextColored(colors[4], "Checking syntax with MSVC... |");
            } else if (syntax_result_.has_value()) {
                if (syntax_result_->success) {
                    ImGui::TextColored(colors[3], ICON_MD_CHECK " Syntax Check Passed (0 errors, %zu warnings) for %s",
                        syntax_result_->warning_count,
                        std::filesystem::path(syntax_result_->file_path).filename().string().c_str());
                } else {
                    ImGui::TextColored(colors[2], ICON_MD_CLOSE " Syntax Check Failed (%zu errors, %zu warnings) for %s",
                        syntax_result_->error_count,
                        syntax_result_->warning_count,
                        std::filesystem::path(syntax_result_->file_path).filename().string().c_str());

                    if (ImGui::CollapsingHeader("Syntax Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (size_t i = 0; i < syntax_result_->diagnostics.size(); ++i) {
                            const auto& d = syntax_result_->diagnostics[i];
                            ImGui::PushID(static_cast<int>(i));
                            ImVec4 sev_col = (d.severity == "warning") ? colors[4] : colors[2];
                            ImGui::TextColored(sev_col, "[%s] %s:%d:%d: %s",
                                d.severity.c_str(),
                                std::filesystem::path(d.file).filename().string().c_str(),
                                d.line, d.column, d.message.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Open in Editor")) {
                                "edit"_sfn(d.file);
                                auto jump_fn = registrar::get<std::function<void(int)>>("editor_jump_to_line");
                                if (jump_fn && *jump_fn) (*jump_fn)(d.line);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton(ICON_MD_AUTO_AWESOME " Fix with AI")) {
                                triage_build_error_with_ai(d);
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }

            // Build Failure Triage Panel
            if (!build_diagnostics_.empty()) {
                ImGui::Separator();
                ImGui::TextColored(colors[2], ICON_MD_WARNING " Build Failure Triage (%zu issues detected)", build_diagnostics_.size());
                if (!triage_ai_feedback_.empty()) {
                    ImGui::TextColored(colors[3], "%s", triage_ai_feedback_.c_str());
                }

                ImGui::BeginChild("BuildTriageRegion", ImVec2(0, 140), true);
                for (size_t i = 0; i < build_diagnostics_.size(); ++i) {
                    const auto& d = build_diagnostics_[i];
                    ImGui::PushID(static_cast<int>(2000 + i));
                    ImVec4 sev_col = (d.severity == "warning") ? colors[4] : colors[2];
                    ImGui::TextColored(sev_col, "[%s] %s:%d:%d", d.severity.c_str(),
                        std::filesystem::path(d.file).filename().string().c_str(), d.line, d.column);
                    ImGui::TextWrapped("%s", d.message.c_str());

                    if (ImGui::SmallButton("Open in Editor")) {
                        std::string resolved = d.file;
                        if (!std::filesystem::path(resolved).is_absolute()) {
                            resolved = (std::filesystem::path(project_dir_) / resolved).string();
                        }
                        "edit"_sfn(resolved);
                        auto jump_fn = registrar::get<std::function<void(int)>>("editor_jump_to_line");
                        if (jump_fn && *jump_fn) (*jump_fn)(d.line);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_MD_AUTO_AWESOME " Investigate & Fix with AI")) {
                        triage_build_error_with_ai(d);
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }

            // Source Files List
            if (!proj_info_.source_files.empty() && ImGui::CollapsingHeader("Source Files", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (size_t i = 0; i < proj_info_.source_files.size(); ++i) {
                    const auto& src = proj_info_.source_files[i];
                    ImGui::PushID(static_cast<int>(3000 + i));
                    ImGui::BulletText("%s", src.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Edit")) {
                        std::string full_path = src;
                        if (!std::filesystem::path(full_path).is_absolute()) {
                            full_path = (std::filesystem::path(project_dir_) / full_path).string();
                        }
                        "edit"_sfn(full_path);
                    }
                    ImGui::PopID();
                }
            }

            // AI Conventional Commit Review Section
            if (show_commit_dialog_) {
                ImGui::Separator();
                ImGui::TextColored(colors[0], ICON_MD_SMART_TOY " AI Conventional Commit Review");
                if (is_generating_commit_) {
                    ImGui::TextColored(colors[4], "Analyzing git diff & synthesizing Conventional Commit... |");
                } else {
                    auto parsed = rouen::helpers::ConventionalCommitGenerator::parse_conventional_commit(commit_message_buf_);
                    ImGui::Text("Type: ");
                    ImGui::SameLine();
                    ImGui::TextColored(parsed.is_valid ? colors[3] : colors[4], "%s", 
                        parsed.type.empty() ? "(none)" : parsed.type.c_str());
                    if (!parsed.scope.empty()) {
                        ImGui::SameLine();
                        ImGui::Text("Scope: (%s)", parsed.scope.c_str());
                    }
                    if (parsed.is_breaking) {
                        ImGui::SameLine();
                        ImGui::TextColored(colors[2], "[BREAKING CHANGE]");
                    }

                    ImGui::InputTextMultiline("##VCCommitMsg", commit_message_buf_, sizeof(commit_message_buf_), ImVec2(-1, 90));

                    if (ImGui::Button(ICON_MD_SAVE " Stage & Commit")) {
                        commit_with_conventional_message();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_CONTENT_COPY " Copy")) {
                        ImGui::SetClipboardText(commit_message_buf_);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_REFRESH " Regenerate")) {
                        generate_conventional_commit();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_CANCEL " Cancel")) {
                        show_commit_dialog_ = false;
                    }
                }
                if (!commit_status_feedback_.empty()) {
                    ImGui::TextWrapped("%s", commit_status_feedback_.c_str());
                }
            }

            // Running state and timer
            if (cmd_running_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

                ImGui::TextColored(colors[4], "Running: %s (%lld seconds)", last_action_.c_str(), static_cast<long long>(elapsed));

                static int spinner_counter = 0;
                spinner_counter = (spinner_counter + 1) % 60;
                constexpr const char* spinner_chars = "|/-\\";
                ImGui::SameLine();
                ImGui::Text("%c", spinner_chars[(spinner_counter / 15) % 4]);

                requested_fps = 30;
            } else if (!last_action_.empty()) {
                ImGui::TextColored(colors[3], "Last action: %s", last_action_.c_str());
                requested_fps = 1;
            }

            ImGui::Separator();
            ImGui::TextColored(colors[0], "MSBuild Output");

            if (!error_message_.empty()) {
                ImGui::TextColored(colors[2], "%s", error_message_.c_str());
            }

            if (!last_output_.empty()) {
                ImGui::BeginChild("VCOutputRegion", ImVec2(0, 200), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextWrapped("%s", last_output_.c_str());

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndChild();
            }
        });
    }

    void vcproject_card::handle_action(std::string_view action_json) {
        std::string act(action_json);
        if (act.find("check_syntax") != std::string::npos) {
            check_syntax();
        } else if (act.find("build") != std::string::npos) {
            run_msbuild_action("build", "Building project");
        } else if (act.find("rebuild") != std::string::npos) {
            run_msbuild_action("rebuild", "Rebuilding project");
        } else if (act.find("clean") != std::string::npos) {
            run_msbuild_action("clean", "Cleaning project");
        } else if (act.find("run") != std::string::npos) {
            run_msbuild_action("run", "Executing binary");
        } else if (act.find("conventional_commit") != std::string::npos) {
            generate_conventional_commit();
        } else if (act.find("select_stack") != std::string::npos) {
            glz::json_t payload;
            if (auto err = glz::read_json(payload, act); !err) {
                if (payload.contains("index") && payload["index"].holds<double>()) {
                    select_stack(static_cast<size_t>(payload["index"].get<double>()));
                }
            }
        }
    }

} // namespace rouen::cards
