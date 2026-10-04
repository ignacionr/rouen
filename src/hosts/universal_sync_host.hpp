#pragma once

#include <string>
#include <mutex>
#include <filesystem>
#include <unordered_map>
#include <atomic>
#include "../helpers/compat/compat.hpp"
#include "git_sync_host.hpp"

namespace rouen::hosts {

    /**
     * @brief UniversalSyncHost coordinates cross-device multi-data sync (Notes, Travel, RSS, Series, Cards, Contacts)
     * via Git host infrastructure.
     */
    class UniversalSyncHost {
    public:
        static UniversalSyncHost& instance();

        std::string get_status_message();
        bool is_syncing();

        [[nodiscard]] bool is_mesh_sync_active() const;

        bool sync_in(bool import_config = true);
        bool sync_out(const std::string& commit_message = "Auto-sync update");
        bool sync_twoway(const std::string& commit_message = "Two-way sync update", bool import_config = true);

        // Granular real-time sync for single entity mutation
        bool sync_item(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted = false);

        // Explicit Mesh-backed synchronization methods
        bool sync_in_mesh(bool import_config = true, bool incremental = false);
        bool sync_out_mesh(const std::string& commit_message = "Auto-sync update", bool incremental = false);
        bool sync_twoway_mesh(const std::string& commit_message = "Two-way sync update", bool import_config = true, bool incremental = false);

        // Periodic Background Synchronization
        void start_periodic_sync(uint32_t interval_seconds = 300);
        void stop_periodic_sync();
        [[nodiscard]] bool is_periodic_sync_running() const;
        [[nodiscard]] uint32_t get_periodic_interval_seconds() const;
        void set_periodic_interval_seconds(uint32_t sec);

        enum class CanaryStatus {
            Valid,
            Mismatch,
            Uninitialized
        };

        [[nodiscard]] CanaryStatus check_canary() const;
        bool initialize_canary();
        [[nodiscard]] bool is_passphrase_mismatch() const;

        [[nodiscard]] std::filesystem::path get_mesh_cache_path() const;
        [[nodiscard]] size_t get_mesh_entry_count(std::string_view dataset = "") const;

        static void copy_file_if_exists(const std::filesystem::path& from, const std::filesystem::path& to);

    private:
        UniversalSyncHost();
        ~UniversalSyncHost();
        UniversalSyncHost(const UniversalSyncHost&) = delete;
        UniversalSyncHost& operator=(const UniversalSyncHost&) = delete;

        mutable std::mutex mutex_;
        std::string status_message_{"Idle"};
        bool is_syncing_{false};

        // Incremental change tracking
        std::unordered_map<std::string, size_t> published_content_hashes_;
        std::unordered_map<std::string, uint64_t> applied_registry_timestamps_;

        // Periodic background runner
        mutable std::mutex periodic_mutex_;
        std::jthread periodic_thread_;
        std::atomic<bool> periodic_running_{false};
        std::atomic<uint32_t> periodic_interval_seconds_{300};
    };

} // namespace rouen::hosts

namespace rouen::helpers {
    using UniversalSyncService = ::rouen::hosts::UniversalSyncHost;
}
