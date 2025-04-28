#pragma once

#include <filesystem>
#include <fstream>
#include <format>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include "imgui.h"

#include "../interface/card.hpp"
#include "../../registrar.hpp"

namespace rouen::cards
{
    struct cmake_card : public card
    {
        cmake_card(std::string_view path)
            : path_(path), start_time_(std::chrono::steady_clock::now())
        {
            // Set custom colors for CMake card
            colors[0] = {0.4f, 0.6f, 0.8f, 1.0f};  // Primary color - blue
            colors[1] = {0.6f, 0.8f, 1.0f, 0.7f};  // Secondary color - light blue
            
            // Additional colors
            get_color(2, {0.8f, 0.4f, 0.4f, 1.0f}); // Error color - red
            get_color(3, {0.4f, 0.8f, 0.4f, 1.0f}); // Success color - green
            get_color(4, {0.8f, 0.8f, 0.4f, 1.0f}); // Warning color - yellow
            
            name(std::format("CMake: {}", std::filesystem::path(path).string()));
            width = 500.0f;
            
            // Default to 'build' subdirectory
            build_dir_ = std::filesystem::path(path_).parent_path() / "build";
            
            // Try to read the CMakeLists.txt file to extract project info
            read_cmake_file();
        }

        std::string get_uri() const override
        {
            return std::format("cmake:{}", path_);
        }

        void read_cmake_file()
        {
            try {
                std::ifstream file(path_);
                if (!file.is_open()) {
                    error_message_ = "Could not open CMakeLists.txt file";
                    return;
                }
                
                std::string line;
                while (std::getline(file, line)) {
                    // Extract project name
                    if (line.find("project(") != std::string::npos) {
                        size_t start = line.find("project(") + 8;
                        size_t end = line.find(")", start);
                        if (end != std::string::npos) {
                            project_name_ = line.substr(start, end - start);
                            // Clean up whitespace and quotes
                            while (project_name_.front() == ' ' || project_name_.front() == '"') {
                                project_name_.erase(0, 1);
                            }
                            while (project_name_.back() == ' ' || project_name_.back() == '"') {
                                project_name_.pop_back();
                            }
                        }
                    }
                    
                    // Extract version if available
                    if (line.find("VERSION") != std::string::npos && project_version_.empty()) {
                        size_t start = line.find("VERSION") + 7;
                        size_t end = line.find(")", start);
                        if (end == std::string::npos) {
                            end = line.size();
                        }
                        project_version_ = line.substr(start, end - start);
                        // Clean up whitespace
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
                        size_t start = line.find('(') + 1;
                        size_t end = line.find(' ', start);
                        if (end != std::string::npos) {
                            std::string target = line.substr(start, end - start);
                            // Clean up whitespace
                            while (target.front() == ' ' || target.front() == '"') {
                                target.erase(0, 1);
                            }
                            while (target.back() == ' ' || target.back() == '"') {
                                target.pop_back();
                            }
                            targets_.push_back(target);
                        }
                    }
                }
                
                file.close();
            } catch (const std::exception& e) {
                error_message_ = std::format("Error reading CMakeLists.txt: {}", e.what());
            }
        }

        bool run_cmake_action(const std::string& action, const std::string& explanation) 
        {
            // Don't run a new command if one is already running
            if (cmd_running_) {
                return false;
            }
            
            std::string cmd;
            if (action == "configure") {
                if (!std::filesystem::exists(build_dir_)) {
                    std::filesystem::create_directories(build_dir_);
                }
                
                cmd = std::format("cd {} && cmake -B . -S {}", 
                    build_dir_.string(), 
                    std::filesystem::path(path_).parent_path().string());
            } else if (action == "build") {
                cmd = std::format("cd {} && cmake --build .", build_dir_.string());
            } else if (action == "clean") {
                cmd = std::format("cd {} && cmake --build . --target clean", build_dir_.string());
            } else if (action == "install") {
                cmd = std::format("cd {} && cmake --install .", build_dir_.string());
            } else if (action == "open_dir") {
                // Use xdg-open to open the build directory in the file manager
                cmd = std::format("xdg-open {}", build_dir_.string());
            } else if (action == "rebuild") {
                // Clean and then build
                cmd = std::format("cd {} && cmake --build . --target clean && cmake --build .", build_dir_.string());
            } else {
                return false;
            }
            
            // Store process ID for potential cancellation
            process_pid_ = 0;
            
            // Clear previous output when starting a new command
            last_output_.clear();
            
            // Set a timestamp for progress indication
            start_time_ = std::chrono::steady_clock::now();
            
            auto output_func = std::make_shared<std::function<void(std::string)>>(
                [this](std::string output) {
                    // Check for the special completion marker we added to run_command
                    if (output.find("<PROCESS_COMPLETED>") != std::string::npos) {
                        // Remove the marker from the displayed output
                        auto marker_pos = output.find("<PROCESS_COMPLETED>");
                        if (marker_pos != std::string::npos) {
                            output = output.substr(0, marker_pos);
                        }
                        
                        // This is our definitive signal that the process is done
                        this->last_output_ = output;
                        this->cmd_running_ = false;
                        this->process_pid_ = 0;
                        return;
                    }

                    // We get incremental updates, store them as they arrive
                    this->last_output_ = output;
                    
                    // We're still running unless we see the special marker
                    this->cmd_running_ = true;
                    
                    // Extract process ID if available (only works on Linux)
                    if (this->process_pid_ == 0) {
                        // Try to get the PID of the first process from ps
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
                    
                    // As a fallback, also check for common completion indicators in the output
                    // This helps with backward compatibility and cases where the process might
                    // end without waiting for our marker to be appended
                    if (output.find("Process exited with code:") != std::string::npos ||
                        output.find("Process terminated by signal:") != std::string::npos ||
                        output.find("Built target") != std::string::npos || 
                        output.find("Build complete") != std::string::npos ||
                        output.find("Configuring done") != std::string::npos ||
                        output.find("Installing") != std::string::npos ||
                        output.find("Error") != std::string::npos ||
                        output.find("Failed") != std::string::npos) {
                        
                        // Wait a short moment to ensure all output is received
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        this->cmd_running_ = false;
                        this->process_pid_ = 0;
                    }
                });

            // For process tracking, modify the command to output its PID first (Linux only)
            if (action != "open_dir") {
                cmd = std::format("echo PID:$$ && {}", cmd);
            }
            
            "run_command"_sfn2(cmd, output_func);
            cmd_running_ = true;
            last_cmd_ = cmd;
            last_action_ = explanation;
            
            return true;
        }

        void cancel_running_action()
        {
            if (!cmd_running_) {
                return;
            }
            
            // Only attempt to kill if we have a PID
            if (process_pid_ > 0) {
                std::string kill_cmd = std::format("kill -TERM -{}", process_pid_);
                
                // Execute the kill command
                auto output_func = std::make_shared<std::function<void(std::string)>>(
                    [this](std::string output) {
                        this->last_output_ += "\n\n[ACTION CANCELLED BY USER]\n\n" + output;
                    }
                );
                
                "run_command"_sfn2(kill_cmd, output_func);
            }
            
            // Mark as not running anymore
            cmd_running_ = false;
            process_pid_ = 0;
        }

        bool render() override
        {
            return render_window([this]()
            {
                // Project info section
                ImGui::TextColored(colors[0], "CMake Project: %s", 
                    project_name_.empty() ? "Unknown" : project_name_.c_str());
                
                if (!project_version_.empty()) {
                    ImGui::SameLine();
                    ImGui::Text("v%s", project_version_.c_str());
                }
                
                // Edit build directory
                char build_dir_buf[512];
                strncpy(build_dir_buf, build_dir_.string().c_str(), sizeof(build_dir_buf) - 1);
                build_dir_buf[sizeof(build_dir_buf) - 1] = '\0';
                
                if (ImGui::InputText("Build Dir", build_dir_buf, sizeof(build_dir_buf))) {
                    build_dir_ = build_dir_buf;
                }
                
                ImGui::Separator();
                
                // Actions section
                ImGui::TextColored(colors[0], "Actions");
                
                // Use RAII approach for disabled state to avoid mismatched begin/end calls
                // Create a temporary struct that will call EndDisabled in its destructor if needed
                struct DisabledGuard {
                    bool disabled;
                    DisabledGuard(bool is_disabled) : disabled(is_disabled) {
                        if (disabled) ImGui::BeginDisabled();
                    }
                    ~DisabledGuard() {
                        if (disabled) ImGui::EndDisabled();
                    }
                };
                
                // Create the guard with current state (won't change during this frame)
                bool current_cmd_running = cmd_running_;
                {
                    DisabledGuard disabled_guard(current_cmd_running);
                    
                    // First row of buttons
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
                    
                    // Second row of buttons
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
                
                // No need for explicit EndDisabled - the guard will handle it

                // Show cancel button when a command is running - check the saved state
                if (current_cmd_running) {
                    ImGui::Separator();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        cancel_running_action();
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(colors[2], "Terminate the running process");
                }
                
                // Show current or last action status
                if (cmd_running_) {
                    // Calculate elapsed time
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
                    
                    ImGui::TextColored(colors[4], "Running: %s (%lld seconds)", last_action_.c_str(), elapsed);
                    
                    // Add a spinner animation to indicate ongoing work
                    static int spinner_counter = 0;
                    spinner_counter = (spinner_counter + 1) % 60;
                    const char* spinner_chars = "|/-\\";
                    ImGui::SameLine();
                    ImGui::Text("%c", spinner_chars[(spinner_counter/15) % 4]);
                    
                    // Request a faster frame rate while command is running to keep spinner animated
                    requested_fps = 30;
                } else if (!last_action_.empty()) {
                    ImGui::TextColored(colors[3], "Last action: %s", last_action_.c_str());
                    
                    // Reset to default frame rate when idle
                    requested_fps = 1;
                }
                
                ImGui::Separator();
                
                // Targets section if any were found
                if (!targets_.empty()) {
                    ImGui::TextColored(colors[0], "Targets");
                    for (const auto& target : targets_) {
                        ImGui::BulletText("%s", target.c_str());
                    }
                    ImGui::Separator();
                }
                
                // Output section
                ImGui::TextColored(colors[0], "Output");
                
                if (!error_message_.empty()) {
                    ImGui::TextColored(colors[2], "%s", error_message_.c_str());
                }
                
                if (!last_output_.empty()) {
                    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 200), true, 
                                     ImGuiWindowFlags_HorizontalScrollbar);
                    
                    ImGui::TextWrapped("%s", last_output_.c_str());
                    
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                    
                    ImGui::EndChild();
                }
            });
        }

    private:
        std::string path_;
        std::filesystem::path build_dir_;
        std::string project_name_;
        std::string project_version_;
        std::vector<std::string> targets_;
        std::string error_message_;
        std::string last_output_;
        std::string last_cmd_;
        std::string last_action_;
        bool cmd_running_ = false;
        std::chrono::steady_clock::time_point start_time_;
        pid_t process_pid_ = 0; // Process ID for tracking the running command
    };
}