#include "universal_sync_host.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#include "platform_utils.hpp"
#include "git_sync_host.hpp"
#include "rouen_mesh_host.hpp"
#include "../helpers/sync_crypto_service.hpp"
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
#define UNIV_SYNC_INFO_FMT(fmt, ...) UNIV_SYNC_INFO(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

    static void export_all_local_datasets(const std::filesystem::path& cache_dir) {
        std::filesystem::create_directories(cache_dir / "notes");
        std::filesystem::create_directories(cache_dir / "travel");
        std::filesystem::create_directories(cache_dir / "rss");
        std::filesystem::create_directories(cache_dir / "series");
        std::filesystem::create_directories(cache_dir / "adaptive_cards");
        std::filesystem::create_directories(cache_dir / "contacts");
        std::filesystem::create_directories(cache_dir / "objectives");
        std::filesystem::create_directories(cache_dir / "config");

        UNIV_SYNC_INFO("Exporting Markdown Notes...");
        models::notes::notes_repository notes_repo;
        notes_repo.export_to_directory(cache_dir / "notes");

        UNIV_SYNC_INFO("Exporting Travel Plans...");
        media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
        travel_repo.export_to_directory(cache_dir / "travel");

        UNIV_SYNC_INFO("Exporting RSS Subscriptions...");
        media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
        rss_repo.export_to_directory(cache_dir / "rss");

        UNIV_SYNC_INFO("Exporting Number Series...");
        models::series::series_repository series_repo;
        series_repo.export_to_directory(cache_dir / "series");

        UNIV_SYNC_INFO("Exporting Adaptive Cards...");
        models::adaptive_cards::adaptive_cards_repository adaptive_cards_repo;
        adaptive_cards_repo.export_to_directory(cache_dir / "adaptive_cards");

        UNIV_SYNC_INFO("Exporting Contacts...");
        models::contacts::contacts_repository contacts_repo(rouen::platform::get_user_data_path("contacts.db").string());
        contacts_repo.export_to_directory(cache_dir / "contacts");

        UNIV_SYNC_INFO("Copying Objectives JSON...");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "objectives.json",
                                               cache_dir / "objectives" / "objectives.json");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_data_path("objectives") / "ledger.json",
                                               cache_dir / "objectives" / "ledger.json");

        UNIV_SYNC_INFO("Copying KPIs JSON...");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_data_path("kpis.json"),
                                               cache_dir / "kpis.json");

        UNIV_SYNC_INFO("Copying configurations...");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_config_directory() / "rouen.ini",
                                               cache_dir / "config" / "rouen.ini");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_config_directory() / "themes.json",
                                               cache_dir / "config" / "themes.json");
        UniversalSyncHost::copy_file_if_exists(rouen::platform::get_user_config_directory() / "personas.json",
                                               cache_dir / "config" / "personas.json");
    }

    static void import_all_local_datasets(const std::filesystem::path& cache_dir, bool import_config) {
        UNIV_SYNC_INFO("Importing Markdown Notes...");
        models::notes::notes_repository notes_repo;
        notes_repo.import_from_directory(cache_dir / "notes");

        UNIV_SYNC_INFO("Importing Travel Plans...");
        media::travel::sqliterepo travel_repo(rouen::platform::get_user_data_path("travel.db").string());
        travel_repo.import_from_directory(cache_dir / "travel");

        UNIV_SYNC_INFO("Importing RSS Subscriptions...");
        media::rss::sqliterepo rss_repo(rouen::platform::get_user_data_path("rss.db").string());
        rss_repo.import_from_directory(cache_dir / "rss");

        UNIV_SYNC_INFO("Importing Number Series...");
        models::series::series_repository series_repo;
        series_repo.import_from_directory(cache_dir / "series");

        UNIV_SYNC_INFO("Importing Adaptive Cards...");
        models::adaptive_cards::adaptive_cards_repository adaptive_cards_repo;
        adaptive_cards_repo.import_from_directory(cache_dir / "adaptive_cards");

        UNIV_SYNC_INFO("Importing Contacts...");
        models::contacts::contacts_repository contacts_repo(rouen::platform::get_user_data_path("contacts.db").string());
        contacts_repo.import_from_directory(cache_dir / "contacts");

        UNIV_SYNC_INFO("Copying Objectives JSON...");
        UniversalSyncHost::copy_file_if_exists(cache_dir / "objectives" / "objectives.json", 
                                               rouen::platform::get_user_data_path("objectives") / "objectives.json");
        UniversalSyncHost::copy_file_if_exists(cache_dir / "objectives" / "ledger.json", 
                                               rouen::platform::get_user_data_path("objectives") / "ledger.json");

        UNIV_SYNC_INFO("Copying KPIs JSON...");
        UniversalSyncHost::copy_file_if_exists(cache_dir / "kpis.json", 
                                               rouen::platform::get_user_data_path("kpis.json"));

        if (import_config) {
            UNIV_SYNC_INFO("Copying configurations...");
            UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "rouen.ini", 
                                                   rouen::platform::get_user_config_directory() / "rouen.ini");
            UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "themes.json", 
                                                   rouen::platform::get_user_config_directory() / "themes.json");
            UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "personas.json", 
                                                   rouen::platform::get_user_config_directory() / "personas.json");
            rouen::helpers::PersonaManager::instance().reload();
        } else {
            UNIV_SYNC_INFO("Skipping configuration import to keep local window state");
        }
    }

    UniversalSyncHost::UniversalSyncHost() = default;

    UniversalSyncHost::~UniversalSyncHost() {
        stop_periodic_sync();
    }

    UniversalSyncHost& UniversalSyncHost::instance() {
        static UniversalSyncHost service;
        return service;
    }

    void UniversalSyncHost::start_periodic_sync(uint32_t interval_seconds) {
        std::lock_guard<std::mutex> lock(periodic_mutex_);
        if (periodic_running_.load()) return;
        if (interval_seconds == 0) interval_seconds = 300;
        periodic_interval_seconds_.store(interval_seconds);
        periodic_running_.store(true);

        periodic_thread_ = std::jthread([this](std::stop_token stop_tok) {
            UNIV_SYNC_INFO_FMT("Periodic Mesh Sync worker started (interval: {}s)", periodic_interval_seconds_.load());
            while (!stop_tok.stop_requested()) {
                uint32_t interval = periodic_interval_seconds_.load();
                for (uint32_t s = 0; s < interval && !stop_tok.stop_requested(); ++s) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (stop_tok.stop_requested()) break;

                if (is_mesh_sync_active() && !is_passphrase_mismatch()) {
                    UNIV_SYNC_INFO("Executing periodic incremental Mesh sync...");
                    sync_twoway_mesh("Periodic incremental sync", true /* import_config */, true /* incremental */);
                }
            }
            UNIV_SYNC_INFO("Periodic Mesh Sync worker stopped.");
        });
    }

    void UniversalSyncHost::stop_periodic_sync() {
        std::lock_guard<std::mutex> lock(periodic_mutex_);
        if (periodic_running_.load()) {
            periodic_running_.store(false);
            if (periodic_thread_.joinable()) {
                periodic_thread_.request_stop();
                periodic_thread_.join();
            }
        }
    }

    bool UniversalSyncHost::is_periodic_sync_running() const {
        return periodic_running_.load();
    }

    uint32_t UniversalSyncHost::get_periodic_interval_seconds() const {
        return periodic_interval_seconds_.load();
    }

    void UniversalSyncHost::set_periodic_interval_seconds(uint32_t sec) {
        if (sec > 0) {
            periodic_interval_seconds_.store(sec);
        }
    }

    std::string UniversalSyncHost::get_status_message() {
        std::lock_guard<std::mutex> const lock(mutex_);
        return status_message_;
    }

    bool UniversalSyncHost::is_syncing() {
        std::lock_guard<std::mutex> const lock(mutex_);
        return is_syncing_;
    }

    bool UniversalSyncHost::is_mesh_sync_active() const {
        return rouen_mesh_host::instance().is_paired();
    }

    std::filesystem::path UniversalSyncHost::get_mesh_cache_path() const {
        return rouen::platform::get_user_data_path("rouen-mesh-sync", false);
    }

    size_t UniversalSyncHost::get_mesh_entry_count(std::string_view dataset) const {
        std::string prefix = "sync/v1/";
        if (!dataset.empty()) {
            prefix += std::string(dataset) + "/";
        }
        auto entries = rouen_mesh_host::instance().get_registry_entries(prefix);
        if (dataset.empty()) {
            size_t count = 0;
            for (const auto& [k, _] : entries) {
                if (!k.starts_with("sync/v1/_")) {
                    count++;
                }
            }
            return count;
        }
        return entries.size();
    }

    UniversalSyncHost::CanaryStatus UniversalSyncHost::check_canary() const {
        if (!is_mesh_sync_active()) {
            return CanaryStatus::Valid;
        }

        auto& mesh = rouen_mesh_host::instance();
        auto entries = mesh.get_registry_entries("sync/v1/_canary");
        auto it = entries.find("sync/v1/_canary");
        auto& crypto = sync::SyncCryptoService::instance();

        if (it != entries.end()) {
            auto decrypted = crypto.decrypt_envelope(it->second.value);
            if (!decrypted.has_value() || decrypted->plaintext != "ROUEN_SYNC_CANARY_V1") {
                return CanaryStatus::Mismatch;
            }
            return CanaryStatus::Valid;
        }

        // If _canary is not present, check if there are any existing sync/v1/* entries
        auto all_sync_entries = mesh.get_registry_entries("sync/v1/");
        if (!all_sync_entries.empty()) {
            bool any_decrypted = false;
            size_t checked = 0;
            for (const auto& [k, v] : all_sync_entries) {
                if (k.starts_with("sync/v1/_")) continue;
                checked++;
                auto dec = crypto.decrypt_envelope(v.value);
                if (dec.has_value()) {
                    any_decrypted = true;
                    break;
                }
                if (checked >= 5) break;
            }
            if (checked > 0 && !any_decrypted) {
                return CanaryStatus::Mismatch;
            }
        }

        return CanaryStatus::Uninitialized;
    }

    bool UniversalSyncHost::initialize_canary() {
        if (!is_mesh_sync_active()) return false;
        auto& crypto = sync::SyncCryptoService::instance();
        auto envelope = crypto.encrypt_envelope("system", "_canary", "ROUEN_SYNC_CANARY_V1");
        if (!envelope.has_value()) return false;
        auto& mesh = rouen_mesh_host::instance();
        mesh.set_registry_value("sync/v1/_canary", *envelope, false /* is_ephemeral */);
        return true;
    }

    bool UniversalSyncHost::is_passphrase_mismatch() const {
        return is_mesh_sync_active() && check_canary() == CanaryStatus::Mismatch;
    }

    bool UniversalSyncHost::sync_item(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted) {
        if (!is_mesh_sync_active()) {
            return false;
        }

        auto canary = check_canary();
        if (canary == CanaryStatus::Mismatch) {
            status_message_ = "⛔ Sync Halted: Master Sync Passphrase mismatch with Mesh cluster.";
            UNIV_SYNC_ERROR(status_message_);
            return false;
        }
        if (canary == CanaryStatus::Uninitialized) {
            initialize_canary();
        }

        std::string rel_path = (dataset == "root") ? std::string(key) : (std::string(dataset) + "/" + std::string(key));
        auto cache_dir = get_mesh_cache_path();
        auto target_file = cache_dir / std::filesystem::path(rel_path);

        try {
            if (is_deleted) {
                if (std::filesystem::exists(target_file)) {
                    std::filesystem::remove(target_file);
                }
            } else {
                if (target_file.has_parent_path()) {
                    std::filesystem::create_directories(target_file.parent_path());
                }
                std::ofstream out(target_file, std::ios::binary);
                if (out.is_open()) {
                    out.write(content.data(), static_cast<std::streamsize>(content.size()));
                }
            }

            auto& crypto = sync::SyncCryptoService::instance();
            auto envelope = crypto.encrypt_envelope(dataset, key, content, 0, is_deleted);
            if (!envelope.has_value()) {
                return false;
            }

            auto& mesh = rouen_mesh_host::instance();
            mesh.set_registry_value("sync/v1/" + rel_path, *envelope, false /* is_ephemeral */);

            if (is_deleted) {
                published_content_hashes_.erase(rel_path);
            } else {
                published_content_hashes_[rel_path] = std::hash<std::string_view>{}(content);
            }

            return true;
        } catch (const std::exception& e) {
            UNIV_SYNC_WARN_FMT("sync_item failed for '{}/{}': {}", dataset, key, e.what());
            return false;
        }
    }

    bool UniversalSyncHost::sync_in_mesh(bool import_config, bool incremental) {
        std::lock_guard<std::mutex> const lock(mutex_);
        if (is_syncing_) return false;

        auto canary = check_canary();
        if (canary == CanaryStatus::Mismatch) {
            status_message_ = "⛔ Sync Halted: Master Sync Passphrase mismatch with Mesh cluster.";
            UNIV_SYNC_ERROR(status_message_);
            return false;
        }

        is_syncing_ = true;
        status_message_ = incremental ? "Sync In (Mesh): Fetching incremental registry entries..." : "Sync In (Mesh): Fetching registry entries...";

        try {
            auto& mesh = rouen_mesh_host::instance();
            if (mesh.is_connected()) {
                mesh.refresh_registry();
            }

            auto cache_dir = get_mesh_cache_path();
            std::filesystem::create_directories(cache_dir);

            auto entries = mesh.get_registry_entries("sync/v1/");
            auto& crypto = sync::SyncCryptoService::instance();

            size_t updated_count = 0;
            size_t deleted_count = 0;

            for (const auto& [wire_key, entry] : entries) {
                if (wire_key.size() <= 8) continue;
                std::string rel_path = wire_key.substr(8);
                if (rel_path.empty() || rel_path.starts_with("_")) continue;

                if (incremental) {
                    auto it = applied_registry_timestamps_.find(wire_key);
                    if (it != applied_registry_timestamps_.end() && it->second >= entry.updated_at_sec) {
                        continue; // Skip: already applied this registry version
                    }
                }

                auto item = crypto.decrypt_envelope(entry.value);
                if (!item.has_value()) {
                    UNIV_SYNC_WARN_FMT("Failed to decrypt envelope for key '{}'", wire_key);
                    continue;
                }

                auto target_file = cache_dir / std::filesystem::path(rel_path);

                if (item->deleted) {
                    if (std::filesystem::exists(target_file)) {
                        std::filesystem::remove(target_file);
                        deleted_count++;
                    }
                } else {
                    bool should_write = true;
                    if (std::filesystem::exists(target_file)) {
                        auto ftime = std::filesystem::last_write_time(target_file);
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                        );
                        uint64_t local_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                            sctp.time_since_epoch()).count());
                        if (local_sec > item->updated_at) {
                            should_write = false; // Local file in staging cache is newer
                        }
                    }

                    if (should_write) {
                        if (target_file.has_parent_path()) {
                            std::filesystem::create_directories(target_file.parent_path());
                        }
                        std::ofstream out(target_file, std::ios::binary);
                        if (out.is_open()) {
                            out.write(item->plaintext.data(), static_cast<std::streamsize>(item->plaintext.size()));
                            updated_count++;
                            published_content_hashes_[rel_path] = std::hash<std::string>{}(item->plaintext);
                        }
                    }
                }

                applied_registry_timestamps_[wire_key] = entry.updated_at_sec;
            }

            UNIV_SYNC_INFO_FMT("Sync In (Mesh): Applied {} updates, {} deletions from {} registry entries.",
                               updated_count, deleted_count, entries.size());

            // Reconcile and import datasets into local repositories if changes occurred or if full sync
            if (updated_count > 0 || deleted_count > 0 || !incremental) {
                import_all_local_datasets(cache_dir, import_config);
            }

            status_message_ = std::format("Sync In (Mesh): Complete ({} updates applied, {} entries total)",
                                          updated_count, entries.size());
            UNIV_SYNC_INFO(status_message_);
            is_syncing_ = false;
            return true;
        } catch (const std::exception& e) {
            status_message_ = std::format("Sync In (Mesh) failed: {}", e.what());
            UNIV_SYNC_ERROR(status_message_);
            is_syncing_ = false;
            return false;
        }
    }

    bool UniversalSyncHost::sync_out_mesh(const std::string& commit_message, bool incremental) {
        (void)commit_message;
        std::lock_guard<std::mutex> const lock(mutex_);
        if (is_syncing_) return false;

        auto canary = check_canary();
        if (canary == CanaryStatus::Mismatch) {
            status_message_ = "⛔ Sync Halted: Master Sync Passphrase mismatch with Mesh cluster.";
            UNIV_SYNC_ERROR(status_message_);
            return false;
        }
        if (canary == CanaryStatus::Uninitialized) {
            initialize_canary();
        }

        is_syncing_ = true;
        status_message_ = incremental ? "Sync Out (Mesh): Exporting modified datasets..." : "Sync Out (Mesh): Exporting local datasets...";

        try {
            auto cache_dir = get_mesh_cache_path();
            std::filesystem::create_directories(cache_dir);

            // 1. Export all local databases to staging folder
            export_all_local_datasets(cache_dir);

            // 2. Encrypt and publish each modified file to Mesh Registry
            auto& mesh = rouen_mesh_host::instance();
            auto& crypto = sync::SyncCryptoService::instance();
            size_t published_count = 0;
            size_t skipped_count = 0;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(cache_dir)) {
                if (!entry.is_regular_file()) continue;

                std::string rel_path = std::filesystem::relative(entry.path(), cache_dir).generic_string();
                if (rel_path.starts_with("_")) continue;

                std::ifstream in(entry.path(), std::ios::binary);
                if (!in.is_open()) continue;
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

                size_t content_hash = std::hash<std::string>{}(content);
                if (incremental) {
                    auto it = published_content_hashes_.find(rel_path);
                    if (it != published_content_hashes_.end() && it->second == content_hash) {
                        skipped_count++;
                        continue; // Skip untouched file (identical content)
                    }
                }

                auto ftime = std::filesystem::last_write_time(entry.path());
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                uint64_t mtime_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                    sctp.time_since_epoch()).count());

                std::string dataset;
                std::string key;
                size_t slash = rel_path.find('/');
                if (slash != std::string::npos) {
                    dataset = rel_path.substr(0, slash);
                    key = rel_path.substr(slash + 1);
                } else {
                    dataset = "root";
                    key = rel_path;
                }

                auto envelope = crypto.encrypt_envelope(dataset, key, content, mtime_sec, false);
                if (envelope.has_value()) {
                    std::string wire_key = "sync/v1/" + rel_path;
                    mesh.set_registry_value(wire_key, *envelope, false /* is_ephemeral */);
                    published_content_hashes_[rel_path] = content_hash;
                    published_count++;
                }
            }

            status_message_ = std::format("Sync Out (Mesh): Complete ({} published, {} unchanged)",
                                          published_count, skipped_count);
            UNIV_SYNC_INFO(status_message_);
            is_syncing_ = false;
            return true;
        } catch (const std::exception& e) {
            status_message_ = std::format("Sync Out (Mesh) failed: {}", e.what());
            UNIV_SYNC_ERROR(status_message_);
            is_syncing_ = false;
            return false;
        }
    }

    bool UniversalSyncHost::sync_twoway_mesh(const std::string& commit_message, bool import_config, bool incremental) {
        UNIV_SYNC_INFO_FMT("Starting Mesh Two-Way Sync process (incremental: {})...", incremental ? "true" : "false");
        auto canary = check_canary();
        if (canary == CanaryStatus::Mismatch) {
            status_message_ = "⛔ Sync Halted: Master Sync Passphrase mismatch with Mesh cluster.";
            UNIV_SYNC_ERROR(status_message_);
            return false;
        }
        if (!sync_in_mesh(import_config, incremental)) {
            return false;
        }
        return sync_out_mesh(commit_message, incremental);
    }

    bool UniversalSyncHost::sync_in(bool import_config) {
        if (is_mesh_sync_active()) {
            return sync_in_mesh(import_config);
        }

        std::lock_guard<std::mutex> const lock(mutex_);
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
            import_all_local_datasets(cache_dir, import_config);
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
        if (is_mesh_sync_active()) {
            return sync_out_mesh(commit_message);
        }

        std::lock_guard<std::mutex> const lock(mutex_);
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
            export_all_local_datasets(cache_dir);

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
        if (is_mesh_sync_active()) {
            return sync_twoway_mesh(commit_message, import_config);
        }

        UNIV_SYNC_INFO("Starting Two-Way Sync process...");

        try {
            auto& git = GitSyncHost::instance();
            if (git.is_configured() && git.initialize()) {
                auto cache_dir = git.get_cache_path();
                export_all_local_datasets(cache_dir);
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
