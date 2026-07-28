#include "universal_sync_host.hpp"

#include <fstream>
#include <algorithm>
#include <chrono>
#include <format>

#include "config_service.hpp"
#include "platform_utils.hpp"
#include "git_sync_host.hpp"
#include "debug.hpp"
#include "../models/travel/sqliterepo.hpp"
#include "../models/rss/sqliterepo.hpp"
#include "../models/notes/notes_repository.hpp"
#include "../models/series/series_repository.hpp"
#include "../models/adaptive_cards/adaptive_cards_repository.hpp"
#include "../models/contacts/contacts_repository.hpp"
#include "persona_manager.hpp"

#define UNIV_SYNC_ERROR(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_ERROR, message)
#define UNIV_SYNC_WARN(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_WARN, message)
#define UNIV_SYNC_INFO(message) LOG_COMPONENT("UNIV_SYNC", LOG_LEVEL_INFO, message)

#define UNIV_SYNC_WARN_FMT(fmt, ...) UNIV_SYNC_WARN(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

    UniversalSyncHost& UniversalSyncHost::instance() {
        static UniversalSyncHost service;
        return service;
    }

    std::string UniversalSyncHost::get_status_message() {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_message_;
    }

    bool UniversalSyncHost::is_syncing() {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_syncing_;
    }

    bool UniversalSyncHost::sync_in(bool import_config) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_syncing_) return false;
        is_syncing_ = true;
        status_message_ = "Sync In: Starting...";

        auto& git = GitSyncHost::instance();
        if (!git.is_configured()) {
            status_message_ = "Sync In: Git repository is not configured";
            UNIV_SYNC_WARN(status_message_);
            is_syncing_ = false;
            return false;
        }

        if (!git.initialize()) {
            status_message_ = "Sync In: Git initialization failed: " + git.get_status_message();
            UNIV_SYNC_ERROR(status_message_);
            is_syncing_ = false;
            return false;
        }

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

            // 3.5. Import Number Series
            UNIV_SYNC_INFO("Sync In: Importing Number Series...");
            status_message_ = "Sync In: Importing Number Series...";
            models::series::series_repository series_repo;
            series_repo.import_from_directory(cache_dir / "series");

            // 3.6. Import Adaptive Cards
            UNIV_SYNC_INFO("Sync In: Importing Adaptive Cards...");
            status_message_ = "Sync In: Importing Adaptive Cards...";
            models::adaptive_cards::adaptive_cards_repository adaptive_cards_repo;
            adaptive_cards_repo.import_from_directory(cache_dir / "adaptive_cards");

            // 3.7. Import Contacts
            UNIV_SYNC_INFO("Sync In: Importing Contacts...");
            status_message_ = "Sync In: Importing Contacts...";
            models::contacts::contacts_repository contacts_repo(rouen::platform::get_user_data_path("contacts.db").string());
            contacts_repo.import_from_directory(cache_dir / "contacts");

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
                copy_file_if_exists(cache_dir / "config" / "personas.json", 
                                    rouen::platform::get_user_config_directory() / "personas.json");
                rouen::helpers::PersonaManager::instance().reload();
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

    bool UniversalSyncHost::sync_out(const std::string& commit_message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_syncing_) return false;
        is_syncing_ = true;
        status_message_ = "Sync Out: Starting...";

        auto& git = GitSyncHost::instance();
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
            std::filesystem::create_directories(cache_dir / "notes");
            std::filesystem::create_directories(cache_dir / "travel");
            std::filesystem::create_directories(cache_dir / "rss");
            std::filesystem::create_directories(cache_dir / "series");
            std::filesystem::create_directories(cache_dir / "adaptive_cards");
            std::filesystem::create_directories(cache_dir / "contacts");
            std::filesystem::create_directories(cache_dir / "objectives");
            std::filesystem::create_directories(cache_dir / "config");

            UNIV_SYNC_INFO("Sync Out: Exporting Markdown Notes...");
            status_message_ = "Sync Out: Exporting Notes...";
            models::notes::notes_repository notes_repo;
            notes_repo.export_to_directory(cache_dir / "notes");

            UNIV_SYNC_INFO("Sync Out: Exporting Travel Plans...");
            status_message_ = "Sync Out: Exporting Travel Plans...";
            media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
            travel_repo.export_to_directory(cache_dir / "travel");

            UNIV_SYNC_INFO("Sync Out: Exporting RSS Subscriptions...");
            status_message_ = "Sync Out: Exporting RSS Subscriptions...";
            media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
            rss_repo.export_to_directory(cache_dir / "rss");

            UNIV_SYNC_INFO("Sync Out: Exporting Number Series...");
            status_message_ = "Sync Out: Exporting Number Series...";
            models::series::series_repository series_repo;
            series_repo.export_to_directory(cache_dir / "series");

            UNIV_SYNC_INFO("Sync Out: Exporting Adaptive Cards...");
            status_message_ = "Sync Out: Exporting Adaptive Cards...";
            models::adaptive_cards::adaptive_cards_repository adaptive_cards_repo;
            adaptive_cards_repo.export_to_directory(cache_dir / "adaptive_cards");

            UNIV_SYNC_INFO("Sync Out: Exporting Contacts...");
            status_message_ = "Sync Out: Exporting Contacts...";
            models::contacts::contacts_repository contacts_repo(rouen::platform::get_user_data_path("contacts.db").string());
            contacts_repo.export_to_directory(cache_dir / "contacts");

            UNIV_SYNC_INFO("Sync Out: Copying Objectives JSON...");
            status_message_ = "Sync Out: Copying Objectives...";
            copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "objectives.json",
                                cache_dir / "objectives" / "objectives.json");
            copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "ledger.json",
                                cache_dir / "objectives" / "ledger.json");

            UNIV_SYNC_INFO("Sync Out: Copying KPIs JSON...");
            status_message_ = "Sync Out: Copying KPIs...";
            copy_file_if_exists(rouen::platform::get_user_data_path("kpis.json"),
                                cache_dir / "kpis.json");

            UNIV_SYNC_INFO("Sync Out: Copying configurations...");
            status_message_ = "Sync Out: Copying Layout & Config...";
            copy_file_if_exists(rouen::platform::get_user_config_directory() / "rouen.ini",
                                cache_dir / "config" / "rouen.ini");
            copy_file_if_exists(rouen::platform::get_user_config_directory() / "themes.json",
                                cache_dir / "config" / "themes.json");
            copy_file_if_exists(rouen::platform::get_user_config_directory() / "personas.json",
                                cache_dir / "config" / "personas.json");

            UNIV_SYNC_INFO("Sync Out: Committing and pushing changes...");
            status_message_ = "Sync Out: Committing and pushing...";

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

    bool UniversalSyncHost::sync_twoway(const std::string& commit_message, bool import_config) {
        UNIV_SYNC_INFO("Starting Two-Way Sync process...");

        try {
            auto& git = GitSyncHost::instance();
            if (git.is_configured() && git.initialize()) {
                auto cache_dir = git.get_cache_path();
                std::filesystem::create_directories(cache_dir / "notes");
                std::filesystem::create_directories(cache_dir / "travel");
                std::filesystem::create_directories(cache_dir / "rss");
                std::filesystem::create_directories(cache_dir / "series");
                std::filesystem::create_directories(cache_dir / "adaptive_cards");
                std::filesystem::create_directories(cache_dir / "objectives");
                std::filesystem::create_directories(cache_dir / "config");

                models::notes::notes_repository notes_repo;
                notes_repo.export_to_directory(cache_dir / "notes");

                media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
                travel_repo.export_to_directory(cache_dir / "travel");

                media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
                rss_repo.export_to_directory(cache_dir / "rss");

                models::series::series_repository series_repo;
                series_repo.export_to_directory(cache_dir / "series");

                models::adaptive_cards::adaptive_cards_repository adaptive_cards_repo;
                adaptive_cards_repo.export_to_directory(cache_dir / "adaptive_cards");

                copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "objectives.json",
                                    cache_dir / "objectives" / "objectives.json");
                copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "ledger.json",
                                    cache_dir / "objectives" / "ledger.json");

                copy_file_if_exists(rouen::platform::get_user_data_path("kpis.json"),
                                    cache_dir / "kpis.json");

                copy_file_if_exists(rouen::platform::get_user_config_directory() / "rouen.ini",
                                    cache_dir / "config" / "rouen.ini");
                copy_file_if_exists(rouen::platform::get_user_config_directory() / "themes.json",
                                    cache_dir / "config" / "themes.json");
                copy_file_if_exists(rouen::platform::get_user_config_directory() / "personas.json",
                                    cache_dir / "config" / "personas.json");
            }
        } catch (const std::exception& e) {
            UNIV_SYNC_WARN_FMT("Failed to export local state before sync_in: {}", e.what());
        }

        if (!sync_in(import_config)) {
            return false;
        }

        return sync_out(commit_message);
    }

    void UniversalSyncHost::copy_file_if_exists(const std::filesystem::path& from, const std::filesystem::path& to) {
        try {
            if (std::filesystem::exists(from)) {
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

} // namespace rouen::hosts
