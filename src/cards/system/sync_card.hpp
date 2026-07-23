#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/universal_sync_service.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct sync_card : public card {
    sync_card() {
        colors[0] = {0.2f, 0.5f, 0.6f, 1.0f};   // Cool blue/cyan primary
        colors[1] = {0.3f, 0.6f, 0.7f, 0.7f};   // Secondary
        
        get_color(2, {0.3f, 0.8f, 0.9f, 1.0f}); // Highlight cyan
        get_color(3, {0.1f, 0.18f, 0.22f, 0.8f}); // Section background
        get_color(4, {1.0f, 1.0f, 1.0f, 0.95f}); // White text
        get_color(5, {0.8f, 0.3f, 0.3f, 1.0f}); // Error red
        get_color(6, {0.3f, 0.7f, 0.3f, 1.0f}); // Success green
        
        name("Universal Git Sync");
        width = 620.0f;
        requested_fps = 5;
        
        load_config_values();
    }

    ~sync_card() override = default;

    bool render() override {
        return render_window([this]() {
            render_sync_content();
        });
    }

    std::string get_uri() const override {
        return "sync";
    }

private:
    std::string git_url_;
    std::string token_;
    std::string cache_path_;
    bool auto_startup_{false};
    bool auto_shutdown_{false};

    std::array<char, 512> git_url_buf_{};
    std::array<char, 256> token_buf_{};
    std::array<char, 512> cache_path_buf_{};
    
    std::vector<std::string> sync_logs_;

    void load_config_values() {
        auto config = rouen::helpers::ConfigService::instance();
        git_url_ = config->get_env("ROUEN_SYNC_GIT_URL");
        token_ = config->get_env("ROUEN_SYNC_TOKEN");
        cache_path_ = config->get_env("ROUEN_SYNC_CACHE_PATH");
        if (cache_path_.empty()) {
            cache_path_ = rouen::platform::get_user_data_path("rouen-sync", false).string();
        }
        
        auto_startup_ = config->get_env("ROUEN_SYNC_AUTO_ON_STARTUP") == "1";
        auto_shutdown_ = config->get_env("ROUEN_SYNC_AUTO_ON_SHUTDOWN") == "1";

        std::fill(git_url_buf_.begin(), git_url_buf_.end(), '\0');
        std::snprintf(git_url_buf_.data(), git_url_buf_.size(), "%s", git_url_.c_str());

        std::fill(token_buf_.begin(), token_buf_.end(), '\0');
        std::snprintf(token_buf_.data(), token_buf_.size(), "%s", token_.c_str());

        std::fill(cache_path_buf_.begin(), cache_path_buf_.end(), '\0');
        std::snprintf(cache_path_buf_.data(), cache_path_buf_.size(), "%s", cache_path_.c_str());
    }

    void save_config_values() {
        auto config = rouen::helpers::ConfigService::instance();
        
        git_url_ = git_url_buf_.data();
        token_ = token_buf_.data();
        cache_path_ = cache_path_buf_.data();

        config->set_env_value("ROUEN_SYNC_GIT_URL", git_url_, true);
        config->set_env_value("ROUEN_SYNC_TOKEN", token_, true);
        config->set_env_value("ROUEN_SYNC_CACHE_PATH", cache_path_, true);
        config->set_env_value("ROUEN_SYNC_AUTO_ON_STARTUP", auto_startup_ ? "1" : "0", true);
        config->set_env_value("ROUEN_SYNC_AUTO_ON_SHUTDOWN", auto_shutdown_ ? "1" : "0", true);
        
        add_log_entry("Configuration saved and persisted to .env");
    }

    void add_log_entry(const std::string& entry) {
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_info = *std::localtime(&now_t);
        
        std::string log = std::format("[{:02d}:{:02d}:{:02d}] {}", 
                                      tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, 
                                      entry);
        sync_logs_.push_back(log);
        if (sync_logs_.size() > 50) {
            sync_logs_.erase(sync_logs_.begin());
        }
    }

    void render_sync_content() {
        auto& service = rouen::helpers::UniversalSyncService::instance();

        // 1. Settings Section
        ImGui::TextColored(get_color(2), "Git Repository Configuration");
        
        ImGui::InputText("Remote Repository URL", git_url_buf_.data(), git_url_buf_.size());
        ImGui::InputText("Personal Access Token (PAT)", token_buf_.data(), token_buf_.size(), ImGuiInputTextFlags_Password);
        ImGui::InputText("Local Cache Path", cache_path_buf_.data(), cache_path_buf_.size());
        
        ImGui::Checkbox("Auto-Pull on Startup", &auto_startup_);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Push (Two-Way Sync) on Shutdown", &auto_shutdown_);

        if (ImGui::Button("Save Configuration")) {
            save_config_values();
        }
        
        ImGui::Separator();

        // 2. Control Buttons Section
        ImGui::TextColored(get_color(2), "Operations");

        bool running = service.is_syncing();
        if (running) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Initialize & Clone Repository")) {
            add_log_entry("Starting repository initialization...");
            std::thread([this]() {
                auto& git = rouen::helpers::GitSyncService::instance();
                git.initialize();
                add_log_entry(git.get_status_message());
            }).detach();
        }
        
        ImGui::SameLine();

        if (ImGui::Button("Sync In (Pull)")) {
            add_log_entry("Starting Sync In (pull & import)...");
            std::thread([this]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_in();
                add_log_entry(s.get_status_message());
            }).detach();
        }

        ImGui::SameLine();

        if (ImGui::Button("Sync Out (Push)")) {
            add_log_entry("Starting Sync Out (export & push)...");
            std::thread([this]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_out("Manual sync push");
                add_log_entry(s.get_status_message());
            }).detach();
        }

        ImGui::SameLine();

        if (ImGui::Button("Two-Way Sync")) {
            add_log_entry("Starting full Two-Way Sync...");
            std::thread([this]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_twoway("Manual two-way sync");
                add_log_entry(s.get_status_message());
            }).detach();
        }

        if (running) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        // 3. Status Display
        std::string current_status = service.get_status_message();
        ImGui::Text("Engine Status: ");
        ImGui::SameLine();
        if (running) {
            ImGui::TextColored({0.9f, 0.6f, 0.1f, 1.0f}, "Syncing...");
            ImGui::TextWrapped("Detail: %s", current_status.c_str());
        } else {
            ImGui::TextColored(get_color(6), "Idle");
            ImGui::TextWrapped("Last Operation Result: %s", current_status.c_str());
        }

        ImGui::Separator();

        // 4. Logs Console
        ImGui::TextColored(get_color(2), "Sync Log Console");
        ImGui::BeginChild("SyncLogs", ImVec2(0, 180), true, ImGuiWindowFlags_NoScrollbar);
        for (const auto& log : sync_logs_) {
            if (log.find("failed") != std::string::npos || log.find("Error") != std::string::npos) {
                ImGui::TextColored(get_color(5), "%s", log.c_str());
            } else if (log.find("successfully") != std::string::npos || log.find("Complete") != std::string::npos) {
                ImGui::TextColored(get_color(6), "%s", log.c_str());
            } else {
                ImGui::TextUnformatted(log.c_str());
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
};

} // namespace rouen::cards
