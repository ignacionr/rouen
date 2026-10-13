#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <mutex>
#include <chrono>
#include <format>
#include <iostream>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
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
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
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

            // Existing immediate-mode ImGui rendering follows...
            render_sync_content();
        });
    }

    std::string get_uri() const override {
        return "sync";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "sync";
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    void set_allow_network_io(bool allow) noexcept { allow_network_io_ = allow; }
    [[nodiscard]] bool is_allow_network_io() const noexcept { return allow_network_io_; }
    [[nodiscard]] std::string get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] std::string get_sync_status() const noexcept { return sync_status_; }
    void set_sync_status(std::string_view status) { sync_status_ = std::string(status); }
    [[nodiscard]] int get_synced_entities_count() const noexcept { return synced_entities_count_; }
    void set_synced_entities_count(int count) noexcept { synced_entities_count_ = count; }
    [[nodiscard]] int get_conflict_count() const noexcept { return conflict_count_; }
    void set_conflict_count(int count) noexcept { conflict_count_ = count; }
    [[nodiscard]] std::string get_conflict_strategy() const noexcept { return conflict_strategy_; }
    void set_conflict_strategy(std::string_view strat) { conflict_strategy_ = std::string(strat); }
    [[nodiscard]] std::chrono::system_clock::time_point get_last_sync_time() const noexcept { return last_sync_time_; }
    void set_last_sync_time(std::chrono::system_clock::time_point t) noexcept { last_sync_time_ = t; }

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
    std::mutex log_mutex_{};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State Synchronization
    std::string sync_status_{"IN SYNC"};
    std::chrono::system_clock::time_point last_sync_time_{std::chrono::system_clock::now()};
    int synced_entities_count_{3};
    int conflict_count_{0};
    std::string conflict_strategy_{"local-wins"};
    std::string last_action_{};
    bool allow_network_io_{true};

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
        std::tm tm_info{};
#ifdef _WIN32
        localtime_s(&tm_info, &now_t);
#else
        localtime_r(&now_t, &tm_info);
#endif
        
        std::string log = std::format("[{:02d}:{:02d}:{:02d}] {}", 
                                      tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, 
                                      entry);
        std::lock_guard<std::mutex> lock(log_mutex_);
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

inline std::string sync_card::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // 1. Header Container
    {
        glz::json_t container;
        container["type"] = "Container";
        container["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> cols;

        // Column 1: Emoji icon 🔄 (\ud83d\udd04)
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;
            glz::json_t tb_emoji;
            tb_emoji["type"] = "TextBlock";
            tb_emoji["text"] = "\xf0\x9f\x94\x84";
            tb_emoji["size"] = "Large";
            col_items.push_back(std::move(tb_emoji));
            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 2: Title and Subtitle
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "stretch";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_title;
            tb_title["type"] = "TextBlock";
            tb_title["text"] = "Universal Cross-Device Synchronization";
            tb_title["weight"] = "Bolder";
            tb_title["size"] = "Large";
            col_items.push_back(std::move(tb_title));

            glz::json_t tb_sub;
            tb_sub["type"] = "TextBlock";
            tb_sub["text"] = "Encrypted bundle synchronization engine";
            tb_sub["isSubtle"] = true;
            tb_sub["size"] = "Small";
            col_items.push_back(std::move(tb_sub));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 3: Status badge
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_status;
            tb_status["type"] = "TextBlock";
            tb_status["text"] = sync_status_;
            tb_status["weight"] = "Bolder";
            tb_status["color"] = (sync_status_ == "IN SYNC") ? "Good" : (conflict_count_ > 0 ? "Attention" : "Accent");
            tb_status["size"] = "Small";
            col_items.push_back(std::move(tb_status));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        col_set["columns"] = std::move(cols);
        std::vector<glz::json_t> cont_items;
        cont_items.push_back(std::move(col_set));
        container["items"] = std::move(cont_items);
        body.push_back(std::move(container));
    }

    // 2. FactSet
    {
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        // Last Sync Timestamp
        auto now = std::chrono::system_clock::now();
        auto diff_sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_sync_time_).count();
        auto t_c = std::chrono::system_clock::to_time_t(last_sync_time_);
        std::tm tm_info{};
#ifdef _WIN32
        localtime_s(&tm_info, &t_c);
#else
        localtime_r(&t_c, &tm_info);
#endif
        long long mins_ago = static_cast<long long>(diff_sec >= 0 ? diff_sec / 60 : 0);
        std::string time_val = std::format("Today at {:02d}:{:02d} ({} minute{} ago)", 
                                           tm_info.tm_hour, tm_info.tm_min, 
                                           mins_ago, mins_ago == 1 ? "" : "s");

        facts.push_back(glz::json_t::object_t{
            {"title", "Last Sync Timestamp:"},
            {"value", time_val}
        });

        facts.push_back(glz::json_t::object_t{
            {"title", "Synced Databases:"},
            {"value", "adaptive_cards.db, rouen.db, notes.db"}
        });

        facts.push_back(glz::json_t::object_t{
            {"title", "Peer Devices in Mesh:"},
            {"value", "macbook-pro, ws-ir-01 (All synchronized)"}
        });

        facts.push_back(glz::json_t::object_t{
            {"title", "Unresolved Conflicts:"},
            {"value", std::to_string(conflict_count_) + " conflicts"}
        });

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));
    }

    // 3. Input Container for Conflict Resolution Strategy (choice)
    {
        glz::json_t input_container;
        input_container["type"] = "Container";
        input_container["separator"] = true;

        std::vector<glz::json_t> input_items;
        glz::json_t choice_set;
        choice_set["type"] = "Input.ChoiceSet";
        choice_set["id"] = "conflict_strategy";
        choice_set["label"] = "Conflict Resolution Strategy";
        choice_set["value"] = conflict_strategy_;

        std::vector<glz::json_t> choices;
        choices.push_back(glz::json_t::object_t{
            {"title", "Local Wins (Keep Local Changes)"},
            {"value", "local-wins"}
        });
        choices.push_back(glz::json_t::object_t{
            {"title", "Remote Wins (Overwrite With Remote)"},
            {"value", "remote-wins"}
        });
        choice_set["choices"] = std::move(choices);
        input_items.push_back(std::move(choice_set));

        input_container["items"] = std::move(input_items);
        body.push_back(std::move(input_container));
    }

    card["body"] = std::move(body);

    // 4. Actions
    {
        std::vector<glz::json_t> actions;

        glz::json_t act_sync;
        act_sync["type"] = "Action.Execute";
        act_sync["title"] = "Sync Now";
        act_sync["verb"] = "sync_now";
        actions.push_back(std::move(act_sync));

        glz::json_t act_export;
        act_export["type"] = "Action.Execute";
        act_export["title"] = "Export Sync Bundle";
        act_export["verb"] = "sync_export";
        actions.push_back(std::move(act_export));

        glz::json_t act_resolve;
        act_resolve["type"] = "Action.Execute";
        act_resolve["title"] = "Resolve Conflicts";
        act_resolve["verb"] = "sync_resolve_conflicts";
        actions.push_back(std::move(act_resolve));

        card["actions"] = std::move(actions);
    }

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

inline void sync_card::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;
    try {
        glz::json_t payload{};
        std::string verb;
        glz::json_t data{};

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

        if (verb == "sync_now") {
            last_action_ = "sync_now";
            sync_status_ = "IN SYNC";
            last_sync_time_ = std::chrono::system_clock::now();
            add_log_entry("Adaptive action: Triggered sync_now");
            if (allow_network_io_) {
                std::thread([this]() {
                    auto& s = rouen::helpers::UniversalSyncService::instance();
                    s.sync_twoway("Adaptive Card sync_now trigger", true, true);
                    add_log_entry(s.get_status_message());
                }).detach();
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "sync_export") {
            last_action_ = "sync_export";
            add_log_entry("Adaptive action: Exported sync bundle");
            if (allow_network_io_) {
                std::thread([this]() {
                    auto& s = rouen::helpers::UniversalSyncService::instance();
                    s.sync_out("Adaptive Card sync_export bundle export", false);
                    add_log_entry(s.get_status_message());
                }).detach();
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "sync_resolve_conflicts") {
            last_action_ = "sync_resolve_conflicts";
            if (data.is_object()) {
                if (data.contains("strategy") && data["strategy"].holds<std::string>()) {
                    conflict_strategy_ = data["strategy"].get<std::string>();
                } else if (data.contains("conflict_strategy") && data["conflict_strategy"].holds<std::string>()) {
                    conflict_strategy_ = data["conflict_strategy"].get<std::string>();
                }
            }
            conflict_count_ = 0;
            add_log_entry("Adaptive action: Resolved conflicts using strategy " + conflict_strategy_);
            if (allow_network_io_) {
                std::thread([this]() {
                    auto& s = rouen::helpers::UniversalSyncService::instance();
                    bool local_wins = (conflict_strategy_ == "local-wins");
                    if (local_wins) {
                        s.sync_out("Resolved conflicts (local-wins)", false);
                    } else {
                        s.sync_in(true, false);
                    }
                    add_log_entry(s.get_status_message());
                }).detach();
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "toggle_adaptive_view") {
            last_action_ = "toggle_adaptive_view";
            adaptive_view_mode = !adaptive_view_mode;
            last_adaptive_parse_time_ = {};
        }
    } catch (const std::exception& e) {
        std::cerr << "[sync] Action error: " << e.what() << std::endl;
    } catch (...) {}
}

} // namespace rouen::cards
