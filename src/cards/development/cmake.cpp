#include "cmake.hpp"

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
#else
#endif

#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/persona_manager.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/syntax_checker.hpp"
#include "../../helpers/conventional_commit.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

    cmake_card::cmake_card(std::string_view path)
        : path_(path), start_time_(std::chrono::steady_clock::now()) {
        // Set custom colors for CMake card
        colors[0] = {0.4f, 0.6f, 0.8f, 1.0f};  // Primary color - blue
        colors[1] = {0.6f, 0.8f, 1.0f, 0.7f};  // Secondary color - light blue

        // Additional colors
        get_color(2, {0.8f, 0.4f, 0.4f, 1.0f}); // Error color - red
        get_color(3, {0.4f, 0.8f, 0.4f, 1.0f}); // Success color - green
        get_color(4, {0.8f, 0.8f, 0.4f, 1.0f}); // Warning color - yellow

        name(std::format("CMake: {}", std::filesystem::path(path_).string()));
        width = 540.0f;

        // Default to 'build' subdirectory
        build_dir_ = std::filesystem::path(path_).parent_path() / "build";

        // Try to read the CMakeLists.txt file to extract project info
        read_cmake_file();
    }

    std::string cmake_card::get_uri() const {
        return std::format("cmake:{}", path_);
    }

    void cmake_card::read_cmake_file() {
        try {
            std::ifstream file(path_);
            if (!file.is_open()) {
                error_message_ = "Could not open CMakeLists.txt file";
                return;
            }

            std::string line;
            while (std::getline(file, line)) {
                // Extract project name
                if (auto pos = line.find("project("); pos != std::string::npos) {
                    size_t const start = pos + 8;
                    size_t const end = line.find(')', start);
                    if (end != std::string::npos) {
                        project_name_ = line.substr(start, end - start);
                        while (!project_name_.empty() && (project_name_.front() == ' ' || project_name_.front() == '"')) {
                            project_name_.erase(0, 1);
                        }
                        while (!project_name_.empty() && (project_name_.back() == ' ' || project_name_.back() == '"')) {
                            project_name_.pop_back();
                        }
                    }
                }

                // Extract version if available
                if (line.find("VERSION") != std::string::npos && project_version_.empty()) {
                    size_t const start = line.find("VERSION") + 7;
                    size_t end = line.find(')', start);
                    if (end == std::string::npos) {
                        end = line.size();
                    }
                    project_version_ = line.substr(start, end - start);
                    while (!project_version_.empty() && (project_version_.front() == ' ' || project_version_.front() == '"')) {
                        project_version_.erase(0, 1);
                    }
                    while (!project_version_.empty() && (project_version_.back() == ' ' || project_version_.back() == '"' || project_version_.back() == ')')) {
                        project_version_.pop_back();
                    }
                }

                // Add to list of targets
                if (line.find("add_executable(") != std::string::npos || 
                    line.find("add_library(") != std::string::npos) {
                    size_t const start = line.find('(') + 1;
                    size_t const end = line.find(' ', start);
                    if (end != std::string::npos) {
                        std::string target = line.substr(start, end - start);
                        while (!target.empty() && (target.front() == ' ' || target.front() == '"')) {
                            target.erase(0, 1);
                        }
                        while (!target.empty() && (target.back() == ' ' || target.back() == '"')) {
                            target.pop_back();
                        }
                        targets_.push_back(target);
                    }
                }
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Error reading CMakeLists.txt: {}", e.what());
        }
    }

    bool cmake_card::run_cmake_action(const std::string& action, const std::string& explanation) {
        if (cmd_running_) {
            return false;
        }

        std::string cmake_path = CONFIG_SERVICE()->get_cmake_path();
        std::string cmd;

        if (action == "configure") {
            if (!std::filesystem::exists(build_dir_)) {
                std::filesystem::create_directories(build_dir_);
            }
            cmd = std::format("cd {} && {} -B . -S {}", 
                build_dir_.string(), 
                cmake_path,
                std::filesystem::path(path_).parent_path().string());
        } else if (action == "build") {
            cmd = std::format("cd {} && {} --build .", build_dir_.string(), cmake_path);
        } else if (action == "clean") {
            cmd = std::format("cd {} && {} --build . --target clean", build_dir_.string(), cmake_path);
        } else if (action == "install") {
            cmd = std::format("cd {} && {} --install .", build_dir_.string(), cmake_path);
        } else if (action == "open_dir") {
            cmd = platform::open_file(build_dir_.string());
        } else if (action == "rebuild") {
            cmd = std::format("cd {} && {} --build . --target clean && {} --build .", 
                build_dir_.string(), cmake_path, cmake_path);
        } else {
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
                            } catch (...) {
                                // Ignore conversion errors
                            }
                        }
                    }
                }

                if (output.find("Process exited with code:") != std::string::npos ||
                    output.find("Process terminated by signal:") != std::string::npos ||
                    output.find("Built target") != std::string::npos || 
                    output.find("Build complete") != std::string::npos ||
                    output.find("Configuring done") != std::string::npos ||
                    output.find("Installing") != std::string::npos ||
                    output.find("Error") != std::string::npos ||
                    output.find("Failed") != std::string::npos) {
                    
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

    void cmake_card::cancel_running_action() {
        if (!cmd_running_) {
            return;
        }

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

    void cmake_card::check_syntax() {
        if (is_checking_syntax_) return;

        std::string project_dir = std::filesystem::path(path_).parent_path().string();
        std::string target_file = syntax_target_file_;

        // If target file not manually entered, check active editor file
        if (target_file.empty()) {
            try {
                auto get_active = registrar::get<std::function<std::string()>>("editor_get_active_file");
                if (get_active && *get_active) {
                    target_file = (*get_active)();
                }
            } catch (...) {}
        }

        // If still empty, search for primary source file in project directory
        if (target_file.empty()) {
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(project_dir)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".cppm") {
                            target_file = entry.path().string();
                            break;
                        }
                    }
                }
            } catch (...) {}
        }

        if (target_file.empty()) {
            target_file = path_; // fallback to CMakeLists.txt
        }

        std::strncpy(syntax_target_file_, target_file.c_str(), sizeof(syntax_target_file_) - 1);
        syntax_target_file_[sizeof(syntax_target_file_) - 1] = '\0';

        is_checking_syntax_ = true;
        syntax_result_.reset();

        std::thread([this, target_file, project_dir]() {
            auto result = rouen::helpers::SyntaxChecker::instance().check_file(target_file, project_dir);
            this->syntax_result_ = result;
            this->is_checking_syntax_ = false;
        }).detach();
    }

    void cmake_card::triage_build_error_with_ai(const rouen::helpers::Diagnostic& diag) {
        // 1. Switch persona to Code & Git Architect
        rouen::helpers::PersonaManager::instance().select_persona_by_name("Code & Git Architect");

        // 2. Read context around the line if file exists
        std::string snippet;
        std::filesystem::path file_p = diag.file;
        if (!file_p.is_absolute()) {
            file_p = std::filesystem::path(path_).parent_path() / file_p;
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

        // 3. Synthesize structured diagnostic prompt
        std::string prompt = std::format(
            "Please investigate and resolve this build error encountered during CMake/Ninja compilation:\n\n"
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
            project_name_.empty() ? "CMake Project" : project_name_,
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

        // 4. Send to AI chat
        auto send_fn = registrar::get<std::function<void(const std::string&)>>("ai_chat_send_message");
        if (send_fn && *send_fn) {
            (*send_fn)(prompt);
        } else {
            "create_card"_sfn(std::format("ai_chat:{}", ::helpers::StringHelper::url_encode(prompt)));
        }

        triage_ai_feedback_ = std::format("Dispatched {} line {} error to Code & Git Architect", 
            std::filesystem::path(diag.file).filename().string(), diag.line);
    }

    void cmake_card::generate_conventional_commit() {
        if (is_generating_commit_) return;

        is_generating_commit_ = true;
        show_commit_dialog_ = true;
        commit_status_feedback_.clear();
        std::string project_dir = std::filesystem::path(path_).parent_path().string();

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
                diff, status, this->project_name_
            );

            if (msg.empty()) {
                msg = "chore: update project sources and build configuration";
            }

            std::strncpy(this->commit_message_buf_, msg.c_str(), sizeof(this->commit_message_buf_) - 1);
            this->commit_message_buf_[sizeof(this->commit_message_buf_) - 1] = '\0';
            this->is_generating_commit_ = false;
        }).detach();
    }

    void cmake_card::commit_with_conventional_message() {
        std::string msg = commit_message_buf_;
        if (msg.empty()) {
            commit_status_feedback_ = "Commit message cannot be empty.";
            return;
        }

        std::string project_dir = std::filesystem::path(path_).parent_path().string();
        std::string git_path = CONFIG_SERVICE()->get_git_path();

        // Stage changes if none are staged yet
        std::string staged_check = ::ProcessHelper::executeCommandInDirectory(project_dir, git_path + " diff --cached --name-only");
        if (staged_check.empty()) {
            ::ProcessHelper::executeCommandInDirectory(project_dir, git_path + " add -A");
        }

        // Write commit message to temporary file to avoid shell escaping issues
        auto temp_msg_path = std::filesystem::temp_directory_path() / "rouen_commit_msg.txt";
        {
            std::ofstream out(temp_msg_path);
            out << msg;
        }

        std::string cmd = std::format("{} commit -F \"{}\"", git_path, temp_msg_path.string());
        std::string result = ::ProcessHelper::executeCommandInDirectory(project_dir, cmd);
        std::filesystem::remove(temp_msg_path);

        commit_status_feedback_ = result.empty() ? "Committed successfully." : result;
        show_commit_dialog_ = false;
    }

    bool cmake_card::render() {
        return render_window([this]() {
            ImGui::TextColored(colors[0], "CMake Project: %s", 
                project_name_.empty() ? "Unknown" : project_name_.c_str());

            if (!project_version_.empty()) {
                ImGui::SameLine();
                ImGui::Text("v%s", project_version_.c_str());
            }

            char build_dir_buf[512];
            std::strncpy(build_dir_buf, build_dir_.string().c_str(), sizeof(build_dir_buf) - 1);
            build_dir_buf[sizeof(build_dir_buf) - 1] = '\0';

            if (ImGui::InputText("Build Dir", build_dir_buf, sizeof(build_dir_buf))) {
                build_dir_ = build_dir_buf;
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

                if (ImGui::Button("Configure")) {
                    run_cmake_action("configure", "Configuring CMake project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Build")) {
                    run_cmake_action("build", "Building CMake project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Clean")) {
                    run_cmake_action("clean", "Cleaning CMake project");
                }

                if (ImGui::Button("Rebuild")) {
                    run_cmake_action("rebuild", "Rebuilding CMake project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Install")) {
                    run_cmake_action("install", "Installing CMake project");
                }
                ImGui::SameLine();

                if (ImGui::Button("Open Build Dir")) {
                    run_cmake_action("open_dir", "Opening build directory");
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
                    std::string project_dir = std::filesystem::path(path_).parent_path().string();
                    "create_card"_sfn(std::format("diff:{}", project_dir));
                }
            }

            if (current_cmd_running) {
                ImGui::Separator();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    cancel_running_action();
                }
                ImGui::SameLine();
                ImGui::TextColored(colors[2], "Terminate the running process");
            }

            // Syntax check feedback
            if (is_checking_syntax_) {
                ImGui::TextColored(colors[4], "Checking syntax... |");
            } else if (syntax_result_.has_value()) {
                if (syntax_result_->success) {
                    ImGui::TextColored(colors[3], "✓ Syntax Check Passed (0 errors, %zu warnings) for %s",
                        syntax_result_->warning_count,
                        std::filesystem::path(syntax_result_->file_path).filename().string().c_str());
                } else {
                    ImGui::TextColored(colors[2], "✗ Syntax Check Failed (%zu errors, %zu warnings) for %s",
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
                            if (ImGui::SmallButton("Fix with AI")) {
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
                ImGui::TextColored(colors[2], "⚡ Build Failure Triage (%zu issues detected)", build_diagnostics_.size());
                if (!triage_ai_feedback_.empty()) {
                    ImGui::TextColored(colors[3], "%s", triage_ai_feedback_.c_str());
                }

                ImGui::BeginChild("BuildTriageRegion", ImVec2(0, 140), true);
                for (size_t i = 0; i < build_diagnostics_.size(); ++i) {
                    const auto& d = build_diagnostics_[i];
                    ImGui::PushID(static_cast<int>(1000 + i));
                    ImVec4 sev_col = (d.severity == "warning") ? colors[4] : colors[2];
                    ImGui::TextColored(sev_col, "[%s] %s:%d:%d", d.severity.c_str(),
                        std::filesystem::path(d.file).filename().string().c_str(), d.line, d.column);
                    ImGui::TextWrapped("%s", d.message.c_str());

                    if (ImGui::SmallButton("Open in Editor")) {
                        std::string resolved = d.file;
                        if (!std::filesystem::path(resolved).is_absolute()) {
                            resolved = (std::filesystem::path(path_).parent_path() / resolved).string();
                        }
                        "edit"_sfn(resolved);
                        auto jump_fn = registrar::get<std::function<void(int)>>("editor_jump_to_line");
                        if (jump_fn && *jump_fn) (*jump_fn)(d.line);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("⚡ Investigate & Fix with AI")) {
                        triage_build_error_with_ai(d);
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }

            // AI Conventional Commit Review Section
            if (show_commit_dialog_) {
                ImGui::Separator();
                ImGui::TextColored(colors[0], "🤖 AI Conventional Commit Review");
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

                    ImGui::InputTextMultiline("##CommitMsg", commit_message_buf_, sizeof(commit_message_buf_), ImVec2(-1, 90));

                    if (ImGui::Button("💾 Stage & Commit")) {
                        commit_with_conventional_message();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("📋 Copy")) {
                        ImGui::SetClipboardText(commit_message_buf_);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("↺ Regenerate")) {
                        generate_conventional_commit();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("✗ Cancel")) {
                        show_commit_dialog_ = false;
                    }
                }
                if (!commit_status_feedback_.empty()) {
                    ImGui::TextWrapped("%s", commit_status_feedback_.c_str());
                }
            }

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

            if (!targets_.empty()) {
                ImGui::TextColored(colors[0], "Targets");
                for (const auto& target : targets_) {
                    ImGui::BulletText("%s", target.c_str());
                }
                ImGui::Separator();
            }

            ImGui::TextColored(colors[0], "Output");

            if (!error_message_.empty()) {
                ImGui::TextColored(colors[2], "%s", error_message_.c_str());
            }

            if (!last_output_.empty()) {
                ImGui::BeginChild("ScrollingRegion", ImVec2(0, 200), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextWrapped("%s", last_output_.c_str());

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndChild();
            }
        });
    }

} // namespace rouen::cards
