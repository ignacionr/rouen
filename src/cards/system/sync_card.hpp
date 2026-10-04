#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <thread>
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/universal_sync_service.hpp"
#include "../../helpers/sync_crypto_service.hpp"
#include "../../hosts/rouen_mesh_host.hpp"
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
        
        name("Universal Sync");
        width = 640.0f;
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
    std::string passphrase_;
    bool auto_startup_{false};
    bool auto_shutdown_{false};
    bool auto_periodic_{true};
    int periodic_interval_sec_{300};
    bool force_full_sync_{false};

    std::array<char, 512> git_url_buf_{};
    std::array<char, 256> token_buf_{};
    std::array<char, 512> cache_path_buf_{};
    std::array<char, 256> passphrase_buf_{};
    
    std::vector<std::string> sync_logs_;

    void load_config_values() {
        auto config = rouen::helpers::ConfigService::instance();
        git_url_ = config->get_env("ROUEN_SYNC_GIT_URL");
        token_ = config->get_env("ROUEN_SYNC_TOKEN");
        cache_path_ = config->get_env("ROUEN_SYNC_CACHE_PATH");
        if (cache_path_.empty()) {
            cache_path_ = rouen::platform::get_user_data_path("rouen-sync", false).string();
        }
        
        passphrase_ = config->get_env("ROUEN_SYNC_PASSPHRASE");

        bool is_mesh = rouen::helpers::UniversalSyncService::instance().is_mesh_sync_active();
        auto env_startup = config->get_env("ROUEN_SYNC_AUTO_ON_STARTUP");
        auto env_shutdown = config->get_env("ROUEN_SYNC_AUTO_ON_SHUTDOWN");
        auto env_periodic = config->get_env("ROUEN_SYNC_PERIODIC_ENABLED");
        auto env_interval = config->get_env("ROUEN_SYNC_PERIODIC_INTERVAL_SEC");

        if (is_mesh) {
            auto_startup_ = (env_startup != "0");
            auto_shutdown_ = (env_shutdown != "0");
            auto_periodic_ = (env_periodic != "0");
        } else {
            auto_startup_ = (env_startup == "1");
            auto_shutdown_ = (env_shutdown == "1");
            auto_periodic_ = (env_periodic == "1");
        }

        if (!env_interval.empty()) {
            try { periodic_interval_sec_ = std::stoi(env_interval); } catch (...) {}
        }
        if (periodic_interval_sec_ <= 0) periodic_interval_sec_ = 300;

        std::fill(git_url_buf_.begin(), git_url_buf_.end(), '\0');
        std::snprintf(git_url_buf_.data(), git_url_buf_.size(), "%s", git_url_.c_str());

        std::fill(token_buf_.begin(), token_buf_.end(), '\0');
        std::snprintf(token_buf_.data(), token_buf_.size(), "%s", token_.c_str());

        std::fill(cache_path_buf_.begin(), cache_path_buf_.end(), '\0');
        std::snprintf(cache_path_buf_.data(), cache_path_buf_.size(), "%s", cache_path_.c_str());

        std::fill(passphrase_buf_.begin(), passphrase_buf_.end(), '\0');
        std::snprintf(passphrase_buf_.data(), passphrase_buf_.size(), "%s", passphrase_.c_str());
    }

    void save_config_values() {
        auto config = rouen::helpers::ConfigService::instance();
        
        git_url_ = git_url_buf_.data();
        token_ = token_buf_.data();
        cache_path_ = cache_path_buf_.data();
        passphrase_ = passphrase_buf_.data();

        config->set_env_value("ROUEN_SYNC_GIT_URL", git_url_, true);
        config->set_env_value("ROUEN_SYNC_TOKEN", token_, true);
        config->set_env_value("ROUEN_SYNC_CACHE_PATH", cache_path_, true);
        config->set_env_value("ROUEN_SYNC_PASSPHRASE", passphrase_, true);
        config->set_env_value("ROUEN_SYNC_AUTO_ON_STARTUP", auto_startup_ ? "1" : "0", true);
        config->set_env_value("ROUEN_SYNC_AUTO_ON_SHUTDOWN", auto_shutdown_ ? "1" : "0", true);
        config->set_env_value("ROUEN_SYNC_PERIODIC_ENABLED", auto_periodic_ ? "1" : "0", true);
        config->set_env_value("ROUEN_SYNC_PERIODIC_INTERVAL_SEC", std::to_string(periodic_interval_sec_), true);
        
        rouen::sync::SyncCryptoService::instance().set_passphrase(passphrase_);

        if (auto_periodic_ && rouen::helpers::UniversalSyncService::instance().is_mesh_sync_active()) {
            rouen::helpers::UniversalSyncService::instance().start_periodic_sync(static_cast<uint32_t>(periodic_interval_sec_));
        } else {
            rouen::helpers::UniversalSyncService::instance().stop_periodic_sync();
        }

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
        bool is_mesh = service.is_mesh_sync_active();

        // Mode Status Header
        if (is_mesh) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.22f, 0.25f, 0.7f));
            ImGui::BeginChild("MeshHeader", ImVec2(0, 72), true);
            ImGui::TextColored(get_color(6), "● Mesh Persistent Registry Active (Paired)");
            auto mesh_cfg = rouen::hosts::rouen_mesh_host::instance().get_config();
            bool connected = rouen::hosts::rouen_mesh_host::instance().is_connected();
            ImGui::Text("Client ID: %s | Server: %s", mesh_cfg.client_id.c_str(), mesh_cfg.server_url.c_str());
            ImGui::Text("Connection: %s", connected ? "Connected" : "Reconnecting...");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.15f, 0.05f, 0.7f));
            ImGui::BeginChild("GitHeader", ImVec2(0, 52), true);
            ImGui::TextColored({0.9f, 0.7f, 0.2f, 1.0f}, "⚠ Mesh Unpaired - Operating in Legacy Git Sync Mode");
            ImGui::Text("Pair this device with Rouen Mesh to automatically upgrade to Mesh Persistent Sync.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // 1. Settings Section
        if (is_mesh) {
            if (service.is_passphrase_mismatch()) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.08f, 0.08f, 0.85f));
                ImGui::BeginChild("PassphraseMismatchAlert", ImVec2(0, 56), true);
                ImGui::TextColored(get_color(5), "⛔ MASTER PASSPHRASE MISMATCH DETECTED");
                ImGui::TextWrapped("Sync is halted to prevent data corruption. Enter the matching Master Sync Passphrase used by the cluster.");
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            ImGui::TextColored(get_color(2), "Mesh End-to-End Encryption (E2EE)");
            ImGui::InputText("Master Sync Passphrase", passphrase_buf_.data(), passphrase_buf_.size(), ImGuiInputTextFlags_Password);
            ImGui::TextDisabled("Encrypts all notes, contacts, travel, and RSS via AES-256-GCM prior to mesh dispatch.");
            if (service.is_passphrase_mismatch()) {
                ImGui::TextColored(get_color(5), "⛔ Canary check failed. Key does not match existing cluster entries.");
            } else if (passphrase_buf_[0] == '\0') {
                ImGui::TextColored({0.95f, 0.65f, 0.2f, 1.0f}, "⚠ Local fallback key active. For multi-device sync, set the same Master Passphrase on all devices.");
            } else {
                ImGui::TextColored(get_color(6), "✓ Custom E2EE passphrase active and verified with cluster.");
            }
        } else {
            ImGui::TextColored(get_color(2), "Git Repository Configuration");
            ImGui::InputText("Remote Repository URL", git_url_buf_.data(), git_url_buf_.size());
            ImGui::InputText("Personal Access Token (PAT)", token_buf_.data(), token_buf_.size(), ImGuiInputTextFlags_Password);
            ImGui::InputText("Local Cache Path", cache_path_buf_.data(), cache_path_buf_.size());
        }
        
        ImGui::Spacing();
        ImGui::Checkbox("Auto-Sync In on Startup", &auto_startup_);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Sync Out on Shutdown", &auto_shutdown_);
        if (is_mesh) {
            ImGui::SameLine();
            ImGui::Checkbox("Periodic Incremental Sync", &auto_periodic_);
            if (service.is_periodic_sync_running()) {
                ImGui::TextColored(get_color(6), "● Background reconciliation active every %u seconds (changes only)", 
                                   service.get_periodic_interval_seconds());
            } else {
                ImGui::TextDisabled("○ Periodic reconciliation paused");
            }
        }

        if (ImGui::Button("Save Configuration")) {
            save_config_values();
        }
        
        ImGui::Separator();

        // Dataset Counters (Mesh mode)
        if (is_mesh) {
            ImGui::TextColored(get_color(2), "Registry Synchronized Items");
            size_t notes_n = service.get_mesh_entry_count("notes");
            size_t contacts_n = service.get_mesh_entry_count("contacts");
            size_t personas_n = service.get_mesh_entry_count("personas");
            size_t travel_n = service.get_mesh_entry_count("travel");
            size_t rss_n = service.get_mesh_entry_count("rss");
            size_t total_n = service.get_mesh_entry_count();

            ImGui::Text("Notes: %zu  |  Contacts: %zu  |  Personas: %zu  |  Travel: %zu  |  RSS: %zu  |  Total: %zu",
                        notes_n, contacts_n, personas_n, travel_n, rss_n, total_n);
            ImGui::Separator();
        }

        // 2. Control Buttons Section
        ImGui::TextColored(get_color(2), "Operations");
        if (is_mesh) {
            ImGui::SameLine();
            ImGui::Checkbox("Force Full Re-sync (bypass incremental check)", &force_full_sync_);
        }

        bool running = service.is_syncing();
        bool mismatch = service.is_passphrase_mismatch();
        if (running || mismatch) {
            ImGui::BeginDisabled();
        }

        if (!is_mesh) {
            if (ImGui::Button("Initialize & Clone Repository")) {
                add_log_entry("Starting repository initialization...");
                std::thread([this]() {
                    auto& git = rouen::helpers::GitSyncService::instance();
                    git.initialize();
                    add_log_entry(git.get_status_message());
                }).detach();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button(is_mesh ? "Reconcile In (Pull)" : "Sync In (Pull)")) {
            add_log_entry(is_mesh ? "Pulling registry entries from Mesh..." : "Starting Sync In (pull & import)...");
            std::thread([this, force = force_full_sync_]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_in(true, !force);
                add_log_entry(s.get_status_message());
            }).detach();
        }

        ImGui::SameLine();

        if (ImGui::Button(is_mesh ? "Reconcile Out (Push)" : "Sync Out (Push)")) {
            add_log_entry(is_mesh ? "Publishing encrypted entries to Mesh..." : "Starting Sync Out (export & push)...");
            std::thread([this, force = force_full_sync_]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_out("Manual sync push", !force);
                add_log_entry(s.get_status_message());
            }).detach();
        }

        ImGui::SameLine();

        if (ImGui::Button("Two-Way Sync")) {
            add_log_entry(is_mesh ? "Starting Mesh Two-Way Reconciliation..." : "Starting full Two-Way Sync...");
            std::thread([this, force = force_full_sync_]() {
                auto& s = rouen::helpers::UniversalSyncService::instance();
                s.sync_twoway("Manual two-way sync", true, !force);
                add_log_entry(s.get_status_message());
            }).detach();
        }

        if (running || mismatch) {
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
        } else if (mismatch) {
            ImGui::TextColored(get_color(5), "⛔ Halted (Passphrase Mismatch)");
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
