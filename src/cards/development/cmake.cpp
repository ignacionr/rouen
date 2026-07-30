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

#ifdef _WIN32
#include <tlhelp32.h>
#else
#endif

#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
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
        width = 500.0f;

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
                    size_t const end = line.find(")", start);
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
                    size_t end = line.find(")", start);
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
                [this](std::string output) {
                    this->last_output_ += "\n\n[ACTION CANCELLED BY USER]\n\n" + output;
                }
            );
            "run_command"_sfn2(kill_cmd, output_func);
        }

        cmd_running_ = false;
        process_pid_ = 0;
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
            }

            if (current_cmd_running) {
                ImGui::Separator();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    cancel_running_action();
                }
                ImGui::SameLine();
                ImGui::TextColored(colors[2], "Terminate the running process");
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
