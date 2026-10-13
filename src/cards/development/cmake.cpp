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
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../fonts.hpp"
#include "../../../external/IconsMaterialDesign.h"
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

        // Handle directory vs file path
        std::filesystem::path p(path_);
        if (std::filesystem::is_directory(p)) {
            build_dir_ = p / "build";
            path_ = (p / "CMakeLists.txt").string();
        } else {
            build_dir_ = p.parent_path() / "build";
        }

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
            std::string build_type_flag;
            if (!build_type_.empty()) {
                build_type_flag = std::format(" -DCMAKE_BUILD_TYPE={}", build_type_);
            }
            cmd = std::format("cd {} && {} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON{} -B . -S {}", 
                build_dir_.string(), 
                cmake_path,
                build_type_flag,
                std::filesystem::path(path_).parent_path().string());
        } else if (action == "build") {
            std::string target_flag;
            if (!selected_target_.empty() && selected_target_ != "all") {
                target_flag = std::format(" --target {}", selected_target_);
            }
            cmd = std::format("cd {} && {} --build .{} -j2", build_dir_.string(), cmake_path, target_flag);
        } else if (action == "clean") {
            cmd = std::format("cd {} && {} --build . --target clean", build_dir_.string(), cmake_path);
        } else if (action == "install") {
            cmd = std::format("cd {} && {} --install .", build_dir_.string(), cmake_path);
        } else if (action == "open_dir") {
            cmd = platform::open_file(build_dir_.string());
        } else if (action == "rebuild") {
            std::string target_flag;
            if (!selected_target_.empty() && selected_target_ != "all") {
                target_flag = std::format(" --target {}", selected_target_);
            }
            cmd = std::format("cd {} && {} --build . --target clean && {} --build .{} -j2", 
                build_dir_.string(), cmake_path, cmake_path, target_flag);
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
                    std::string const entry_path = entry.path().string();
                    if (entry_path.find("/build/") != std::string::npos || entry_path.find("/build") != std::string::npos || entry_path.find("/.") != std::string::npos) {
                        continue;
                    }
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
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool cmake_card::render(rouen::ui::ui_context& ui) {
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
                    if (ImGui::SmallButton(ICON_MD_AUTO_AWESOME " Investigate & Fix with AI")) {
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

                    ImGui::InputTextMultiline("##CommitMsg", commit_message_buf_, sizeof(commit_message_buf_), ImVec2(-1, 90));

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

    std::string cmake_card::get_adaptive_card_json() const {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container
        glz::json_t header;
        header["type"] = "Container";
        header["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Column 1: Emoji icon 🛠️ (\ud83d\udee0\ufe0f)
        glz::json_t icon_col;
        icon_col["type"] = "Column";
        icon_col["width"] = "auto";
        std::vector<glz::json_t> icon_items;
        glz::json_t icon_tb;
        icon_tb["type"] = "TextBlock";
        icon_tb["text"] = "\xF0\x9F\x9B\xA0\xEF\xB8\x8F";
        icon_tb["size"] = "Large";
        icon_items.push_back(std::move(icon_tb));
        icon_col["items"] = std::move(icon_items);
        columns.push_back(std::move(icon_col));

        // Column 2: Title & Path subtitle
        glz::json_t title_col;
        title_col["type"] = "Column";
        title_col["width"] = "stretch";
        std::vector<glz::json_t> title_items;
        glz::json_t title_tb;
        title_tb["type"] = "TextBlock";
        title_tb["text"] = "CMake Build Controller";
        title_tb["weight"] = "Bolder";
        title_tb["size"] = "Large";
        title_items.push_back(std::move(title_tb));

        glz::json_t path_tb;
        path_tb["type"] = "TextBlock";
        path_tb["text"] = path_.empty() ? "/Users/ignaciorodriguez/src/rouen/CMakeLists.txt" : path_;
        path_tb["isSubtle"] = true;
        path_tb["size"] = "Small";
        path_tb["spacing"] = "None";
        title_items.push_back(std::move(path_tb));

        title_col["items"] = std::move(title_items);
        columns.push_back(std::move(title_col));

        // Column 3: Badge "Ninja / Clang"
        glz::json_t badge_col;
        badge_col["type"] = "Column";
        badge_col["width"] = "auto";
        std::vector<glz::json_t> badge_items;
        glz::json_t badge_tb;
        badge_tb["type"] = "TextBlock";
        badge_tb["text"] = "Ninja / Clang";
        badge_tb["weight"] = "Bolder";
        badge_tb["color"] = "Accent";
        badge_tb["size"] = "Small";
        badge_items.push_back(std::move(badge_tb));
        badge_col["items"] = std::move(badge_items);
        columns.push_back(std::move(badge_col));

        col_set["columns"] = std::move(columns);
        std::vector<glz::json_t> header_items;
        header_items.push_back(std::move(col_set));
        header["items"] = std::move(header_items);
        body.push_back(std::move(header));

        // 2. FactSet
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        glz::json_t fact_gen;
        fact_gen["title"] = "Generator:";
        fact_gen["value"] = "Ninja";
        facts.push_back(std::move(fact_gen));

        glz::json_t fact_bt;
        fact_bt["title"] = "Build Type:";
        std::string bt_val = (build_type_ == "Release") ? "Release (-O3)" : 
                             (build_type_ == "Debug") ? "Debug (-g)" : 
                             (build_type_ == "RelWithDebInfo") ? "RelWithDebInfo (-O2 -g)" : build_type_;
        fact_bt["value"] = bt_val;
        facts.push_back(std::move(fact_bt));

        glz::json_t fact_jobs;
        fact_jobs["title"] = "Max Parallel Jobs:";
        fact_jobs["value"] = "-j2 (RAM safety enforced)";
        facts.push_back(std::move(fact_jobs));

        glz::json_t fact_tgt;
        fact_tgt["title"] = "Primary Target:";
        fact_tgt["value"] = selected_target_.empty() ? "rouen" : selected_target_;
        facts.push_back(std::move(fact_tgt));

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));

        // 3. Configuration Container
        glz::json_t config_container;
        config_container["type"] = "Container";
        config_container["separator"] = true;
        std::vector<glz::json_t> config_items;

        glz::json_t config_header;
        config_header["type"] = "TextBlock";
        config_header["text"] = "Build Configuration";
        config_header["weight"] = "Bolder";
        config_items.push_back(std::move(config_header));

        // Input.ChoiceSet for build_type
        glz::json_t bt_choice_set;
        bt_choice_set["type"] = "Input.ChoiceSet";
        bt_choice_set["id"] = "build_type";
        bt_choice_set["label"] = "Build Configuration";
        bt_choice_set["value"] = build_type_.empty() ? "Release" : build_type_;
        std::vector<glz::json_t> bt_choices;
        bt_choices.push_back(glz::json_t::object_t{{"title", "Release"}, {"value", "Release"}});
        bt_choices.push_back(glz::json_t::object_t{{"title", "Debug"}, {"value", "Debug"}});
        bt_choices.push_back(glz::json_t::object_t{{"title", "RelWithDebInfo"}, {"value", "RelWithDebInfo"}});
        bt_choice_set["choices"] = std::move(bt_choices);
        config_items.push_back(std::move(bt_choice_set));

        // Input.ChoiceSet for target_name
        glz::json_t tgt_choice_set;
        tgt_choice_set["type"] = "Input.ChoiceSet";
        tgt_choice_set["id"] = "target_name";
        tgt_choice_set["label"] = "Compile Target";
        tgt_choice_set["value"] = selected_target_.empty() ? "rouen" : selected_target_;
        std::vector<glz::json_t> tgt_choices;
        tgt_choices.push_back(glz::json_t::object_t{{"title", "rouen (Main Application)"}, {"value", "rouen"}});
        tgt_choices.push_back(glz::json_t::object_t{{"title", "unit_tests (Test Suite)"}, {"value", "unit_tests"}});
        tgt_choices.push_back(glz::json_t::object_t{{"title", "all (Everything)"}, {"value", "all"}});
        for (const auto& t : targets_) {
            if (t != "rouen" && t != "unit_tests" && t != "all") {
                tgt_choices.push_back(glz::json_t::object_t{{"title", t}, {"value", t}});
            }
        }
        tgt_choice_set["choices"] = std::move(tgt_choices);
        config_items.push_back(std::move(tgt_choice_set));

        config_container["items"] = std::move(config_items);
        body.push_back(std::move(config_container));

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        glz::json_t act_cfg;
        act_cfg["type"] = "Action.Execute";
        act_cfg["title"] = "Configure";
        act_cfg["verb"] = "configure";
        actions.push_back(std::move(act_cfg));

        glz::json_t act_build;
        act_build["type"] = "Action.Execute";
        act_build["title"] = "Build (-j2)";
        act_build["verb"] = "build";
        actions.push_back(std::move(act_build));

        glz::json_t act_clean;
        act_clean["type"] = "Action.Execute";
        act_clean["title"] = "Clean";
        act_clean["verb"] = "clean";
        actions.push_back(std::move(act_clean));

        glz::json_t act_syntax;
        act_syntax["type"] = "Action.Execute";
        act_syntax["title"] = "Check Syntax";
        act_syntax["verb"] = "check_syntax";
        actions.push_back(std::move(act_syntax));

        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    void cmake_card::handle_action(std::string_view action_json) {
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

            if (verb == "configure") {
                if (data.contains("build_type") && data["build_type"].holds<std::string>()) {
                    build_type_ = data["build_type"].get<std::string>();
                }
                run_cmake_action("configure", "Configuring CMake project");
            } else if (verb == "build") {
                if (data.contains("target_name") && data["target_name"].holds<std::string>()) {
                    selected_target_ = data["target_name"].get<std::string>();
                } else if (data.contains("target") && data["target"].holds<std::string>()) {
                    selected_target_ = data["target"].get<std::string>();
                }
                run_cmake_action("build", "Building CMake project");
            } else if (verb == "clean") {
                run_cmake_action("clean", "Cleaning CMake project");
            } else if (verb == "check_syntax") {
                check_syntax();
            } else if (verb == "cancel") {
                cancel_running_action();
            } else if (verb == "conventional_commit") {
                generate_conventional_commit();
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
            } else if (verb == "set_build_type") {
                if (data.contains("build_type") && data["build_type"].holds<std::string>()) {
                    build_type_ = data["build_type"].get<std::string>();
                }
            } else if (verb == "set_target" || verb == "set_target_name") {
                if (data.contains("target_name") && data["target_name"].holds<std::string>()) {
                    selected_target_ = data["target_name"].get<std::string>();
                } else if (data.contains("target") && data["target"].holds<std::string>()) {
                    selected_target_ = data["target"].get<std::string>();
                }
            }

            last_adaptive_parse_time_ = {};
        } catch (const std::exception& e) {
            std::cerr << "[cmake] Action error: " << e.what() << std::endl;
        } catch (...) {
        }
    }

} // namespace rouen::cards
