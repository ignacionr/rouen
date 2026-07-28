#pragma once

#include <string>
#include <mutex>
#include <filesystem>
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

        bool sync_in(bool import_config = true);
        bool sync_out(const std::string& commit_message = "Auto-sync update");
        bool sync_twoway(const std::string& commit_message = "Two-way sync update", bool import_config = true);

    private:
        UniversalSyncHost() = default;
        ~UniversalSyncHost() = default;
        UniversalSyncHost(const UniversalSyncHost&) = delete;
        UniversalSyncHost& operator=(const UniversalSyncHost&) = delete;

        static void copy_file_if_exists(const std::filesystem::path& from, const std::filesystem::path& to);

        mutable std::mutex mutex_;
        std::string status_message_{"Idle"};
        bool is_syncing_{false};
    };

} // namespace rouen::hosts

namespace rouen::helpers {
    using UniversalSyncService = ::rouen::hosts::UniversalSyncHost;
}
