#pragma once

#include <string>
#include <mutex>
#include <filesystem>
#include <memory>

namespace rouen::hosts {

    /**
     * @brief GitSyncHost manages remote repository authentication (SSH / HTTPS tokens),
     * repository clone/pull/push synchronization, and background sync state.
     */
    class GitSyncHost {
    public:
        static GitSyncHost& instance();

        bool is_configured();
        std::filesystem::path get_cache_path();
        std::string get_status_message();

        bool initialize();
        bool pull();
        bool commit_and_push(const std::string& commit_message = "Auto-sync from Rouen");

    private:
        GitSyncHost() = default;
        ~GitSyncHost() = default;
        GitSyncHost(const GitSyncHost&) = delete;
        GitSyncHost& operator=(const GitSyncHost&) = delete;

        void load_settings();
        bool ensure_cache_ready();
        static std::string shell_escape(const std::string& value);
        static std::string host_from_url(const std::string& url);
        bool apply_token_credentials();
        bool is_remote_empty();
        bool run_shell(const std::string& command);

        mutable std::mutex mutex_;
        std::string repo_url_;
        std::string token_;
        std::string cache_path_;
        std::string status_message_;
    };

} // namespace rouen::hosts

namespace rouen::helpers {
    using GitSyncService = ::rouen::hosts::GitSyncHost;
}
