#pragma once

#include <string>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include "config_service.hpp"
#include "platform_utils.hpp"
#include "git_sync_service.hpp"
#include "debug.hpp"
#include "../models/travel/sqliterepo.hpp"
#include "../models/rss/sqliterepo.hpp"
#include "../models/notes/notes_repository.hpp"

// Logging macros for UniversalSyncService
#define UNIV_SYNC_ERROR(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_ERROR, message)
#define UNIV_SYNC_WARN(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_WARN, message)
#define UNIV_SYNC_INFO(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_INFO, message)
#define UNIV_SYNC_DEBUG(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_DEBUG, message)

#define UNIV_SYNC_ERROR_FMT(fmt, ...) UNIV_SYNC_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define UNIV_SYNC_WARN_FMT(fmt, ...) UNIV_SYNC_WARN(debug::format_log(fmt, __VA_ARGS__))
#define UNIV_SYNC_INFO_FMT(fmt, ...) UNIV_SYNC_INFO(debug::format_log(fmt, __VA_ARGS__))
#define UNIV_SYNC_DEBUG_FMT(fmt, ...) UNIV_SYNC_DEBUG(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::helpers {

    class UniversalSyncService {
    public:
        static UniversalSyncService& instance() {
            static UniversalSyncService service;
            return service;
        }

        std::string get_status_message() {
            std::lock_guard<std::mutex> lock(mutex_);
            return status_message_;
        }

        bool is_syncing() {
            std::lock_guard<std::mutex> lock(mutex_);
            return is_syncing_;
        }

        // Pull from Git remote and import into local databases
        bool sync_in(bool import_config = true) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_syncing_) return false;
            is_syncing_ = true;
            status_message_ = "Sync In: Starting...";

            auto& git = GitSyncService::instance();
            if (!git.is_configured()) {
                status_message_ = "Sync In: Git repository is not configured";
                UNIV_SYNC_WARN(status_message_);
                is_syncing_ = false;
                return false;
            }

            // Ensure local repo clone is set up
            if (!git.initialize()) {
                status_message_ = "Sync In: Git initialization failed: " + git.get_status_message();
                UNIV_SYNC_ERROR(status_message_);
                is_syncing_ = false;
                return false;
            }

            // Pull changes
            if (!git.pull()) {
                status_message_ = "Sync In: Git pull failed: " + git.get_status_message();
                UNIV_SYNC_ERROR(status_message_);
                is_syncing_ = false;
                return false;
            }

            auto cache_dir = git.get_cache_path();

            try {
                // 1. Import Notes
                UNIV_SYNC_INFO("Sync In: Importing Markdown Notes...");
                status_message_ = "Sync In: Importing Notes...";
                models::notes::notes_repository notes_repo;
                notes_repo.import_from_directory(cache_dir / "notes");

                // 2. Import Travel Plans
                UNIV_SYNC_INFO("Sync In: Importing Travel Plans...");
                status_message_ = "Sync In: Importing Travel Plans...";
                media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
                travel_repo.import_from_directory(cache_dir / "travel");

                // 3. Import RSS subscriptions
                UNIV_SYNC_INFO("Sync In: Importing RSS Subscriptions...");
                status_message_ = "Sync In: Importing RSS Subscriptions...";
                media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
                rss_repo.import_from_directory(cache_dir / "rss");

                // 4. Copy Objectives and Ledgers
                UNIV_SYNC_INFO("Sync In: Copying Objectives JSON...");
                status_message_ = "Sync In: Copying Objectives...";
                copy_file_if_exists(cache_dir / "objectives" / "objectives.json", 
                                    rouen::platform::get_user_data_path("objectives") / "objectives.json");
                copy_file_if_exists(cache_dir / "objectives" / "ledger.json", 
                                    rouen::platform::get_user_data_path("objectives") / "ledger.json");

                // 5. Copy Sovereign KPIs
                UNIV_SYNC_INFO("Sync In: Copying KPIs JSON...");
                status_message_ = "Sync In: Copying KPIs...";
                copy_file_if_exists(cache_dir / "kpis.json", 
                                    rouen::platform::get_user_data_path("kpis.json"));

                // 6. Copy Configurations (Layout / Themes)
                if (import_config) {
                    UNIV_SYNC_INFO("Sync In: Copying configurations...");
                    status_message_ = "Sync In: Copying Layout & Config...";
                    copy_file_if_exists(cache_dir / "config" / "rouen.ini", 
                                        rouen::platform::get_user_config_directory() / "rouen.ini");
                    copy_file_if_exists(cache_dir / "config" / "themes.json", 
                                        rouen::platform::get_user_config_directory() / "themes.json");
                } else {
                    UNIV_SYNC_INFO("Sync In: Skipping configuration import to keep local window state");
                }

                status_message_ = "Sync In: Complete";
                UNIV_SYNC_INFO(status_message_);
                is_syncing_ = false;
                return true;
            } catch (const std::exception& e) {
                status_message_ = std::format("Sync In failed: {}", e.what());
                UNIV_SYNC_ERROR(status_message_);
                is_syncing_ = false;
                return false;
            }
        }

        // Export local databases to Git folder and Push
        bool sync_out(const std::string& commit_message = "Auto-sync update") {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_syncing_) return false;
            is_syncing_ = true;
            status_message_ = "Sync Out: Starting...";

            auto& git = GitSyncService::instance();
            if (!git.is_configured()) {
                status_message_ = "Sync Out: Git repository is not configured";
                UNIV_SYNC_WARN(status_message_);
                is_syncing_ = false;
                return false;
            }

            if (!git.initialize()) {
                status_message_ = "Sync Out: Git initialization failed: " + git.get_status_message();
                UNIV_SYNC_ERROR(status_message_);
                is_syncing_ = false;
                return false;
            }

            auto cache_dir = git.get_cache_path();

            try {
                // Ensure subdirectories exist in git cache folder
                std::filesystem::create_directories(cache_dir / "notes");
                std::filesystem::create_directories(cache_dir / "travel");
                std::filesystem::create_directories(cache_dir / "rss");
                std::filesystem::create_directories(cache_dir / "objectives");
                std::filesystem::create_directories(cache_dir / "config");

                // 1. Export Notes
                UNIV_SYNC_INFO("Sync Out: Exporting Markdown Notes...");
                status_message_ = "Sync Out: Exporting Notes...";
                models::notes::notes_repository notes_repo;
                notes_repo.export_to_directory(cache_dir / "notes");

                // 2. Export Travel Plans
                UNIV_SYNC_INFO("Sync Out: Exporting Travel Plans...");
                status_message_ = "Sync Out: Exporting Travel Plans...";
                media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
                travel_repo.export_to_directory(cache_dir / "travel");

                // 3. Export RSS subscriptions
                UNIV_SYNC_INFO("Sync Out: Exporting RSS Subscriptions...");
                status_message_ = "Sync Out: Exporting RSS Subscriptions...";
                media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
                rss_repo.export_to_directory(cache_dir / "rss");

                // 4. Copy Objectives and Ledgers
                UNIV_SYNC_INFO("Sync Out: Copying Objectives JSON...");
                status_message_ = "Sync Out: Copying Objectives...";
                copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "objectives.json",
                                    cache_dir / "objectives" / "objectives.json");
                copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "ledger.json",
                                    cache_dir / "objectives" / "ledger.json");

                // 5. Copy Sovereign KPIs
                UNIV_SYNC_INFO("Sync Out: Copying KPIs JSON...");
                status_message_ = "Sync Out: Copying KPIs...";
                copy_file_if_exists(rouen::platform::get_user_data_path("kpis.json"),
                                    cache_dir / "kpis.json");

                // 6. Copy Configurations (Layout / Themes)
                UNIV_SYNC_INFO("Sync Out: Copying configurations...");
                status_message_ = "Sync Out: Copying Layout & Config...";
                copy_file_if_exists(rouen::platform::get_user_config_directory() / "rouen.ini",
                                    cache_dir / "config" / "rouen.ini");
                copy_file_if_exists(rouen::platform::get_user_config_directory() / "themes.json",
                                    cache_dir / "config" / "themes.json");

                // Commit and Push
                UNIV_SYNC_INFO("Sync Out: Committing and pushing changes...");
                status_message_ = "Sync Out: Committing and pushing...";

                // Create a standard commit message with machine identifier if possible
                std::string full_msg = commit_message;
                const char* user = std::getenv("USER");
                if (!user) user = std::getenv("USERNAME");
                if (user) {
                    full_msg += " (by " + std::string(user) + ")";
                }

                if (!git.commit_and_push(full_msg)) {
                    status_message_ = "Sync Out: Push failed: " + git.get_status_message();
                    UNIV_SYNC_ERROR(status_message_);
                    is_syncing_ = false;
                    return false;
                }

                status_message_ = "Sync Out: Complete";
                UNIV_SYNC_INFO(status_message_);
                is_syncing_ = false;
                return true;
            } catch (const std::exception& e) {
                status_message_ = std::format("Sync Out failed: {}", e.what());
                UNIV_SYNC_ERROR(status_message_);
                is_syncing_ = false;
                return false;
            }
        }

        // Two-way sync: Export local additions -> Pull & Merge -> Import merged -> Push
        bool sync_twoway(const std::string& commit_message = "Two-way sync update", bool import_config = true) {
            UNIV_SYNC_INFO("Starting Two-Way Sync process...");
            
            // Export local states first so they are not overwritten by sync_in
            try {
                auto& git = GitSyncService::instance();
                if (git.is_configured() && git.initialize()) {
                    auto cache_dir = git.get_cache_path();
                    std::filesystem::create_directories(cache_dir / "notes");
                    std::filesystem::create_directories(cache_dir / "travel");
                    std::filesystem::create_directories(cache_dir / "rss");
                    std::filesystem::create_directories(cache_dir / "objectives");
                    std::filesystem::create_directories(cache_dir / "config");

                    // 1. Export Notes
                    models::notes::notes_repository notes_repo;
                    notes_repo.export_to_directory(cache_dir / "notes");

                    // 2. Export Travel Plans
                    media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
                    travel_repo.export_to_directory(cache_dir / "travel");

                    // 3. Export RSS subscriptions
                    media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
                    rss_repo.export_to_directory(cache_dir / "rss");

                    // 4. Copy Objectives
                    copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "objectives.json",
                                        cache_dir / "objectives" / "objectives.json");
                    copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "ledger.json",
                                        cache_dir / "objectives" / "ledger.json");

                    // 5. Copy Sovereign KPIs
                    copy_file_if_exists(rouen::platform::get_user_data_path("kpis.json"),
                                        cache_dir / "kpis.json");

                    // 6. Copy Configurations (Layout / Themes)
                    copy_file_if_exists(rouen::platform::get_user_config_directory() / "rouen.ini",
                                        cache_dir / "config" / "rouen.ini");
                    copy_file_if_exists(rouen::platform::get_user_config_directory() / "themes.json",
                                        cache_dir / "config" / "themes.json");
                }
            } catch (const std::exception& e) {
                UNIV_SYNC_WARN_FMT("Failed to export local state before sync_in: {}", e.what());
            }
            
            // First Pull and merge remote edits
            if (!sync_in(import_config)) {
                return false;
            }

            // Export current local database states (merged with remote) and push
            return sync_out(commit_message);
        }

    private:
        UniversalSyncService() = default;
        ~UniversalSyncService() = default;
        UniversalSyncService(const UniversalSyncService&) = delete;
        UniversalSyncService& operator=(const UniversalSyncService&) = delete;

        mutable std::mutex mutex_;
        std::string status_message_{"Idle"};
        bool is_syncing_{false};

        // Secure file copying with error ignoring if source is not present
        static void copy_file_if_exists(const std::filesystem::path& from, const std::filesystem::path& to) {
            try {
                if (std::filesystem::exists(from)) {
                    // Create destination folders if needed
                    auto to_dir = to.parent_path();
                    if (!to_dir.empty()) {
                        std::filesystem::create_directories(to_dir);
                    }
                    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
                }
            } catch (const std::exception& e) {
                UNIV_SYNC_WARN_FMT("Failed to copy file from '{}' to '{}': {}", from.string(), to.string(), e.what());
            }
        }
    };
}
