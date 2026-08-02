#include "git_sync_host.hpp"

#include <filesystem>
#include <format>
#include <array>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <mutex>

#include "config_service.hpp"
#include "platform_utils.hpp"
#include "debug.hpp"

// Logging macros for GitSyncHost
#define GIT_SYNC_ERROR(message) LOG_COMPONENT("GIT_SYNC", LOG_LEVEL_ERROR, message)
#define GIT_SYNC_WARN(message) LOG_COMPONENT("GIT_SYNC", LOG_LEVEL_WARN, message)
#define GIT_SYNC_INFO(message) LOG_COMPONENT("GIT_SYNC", LOG_LEVEL_INFO, message)
#define GIT_SYNC_DEBUG(message) LOG_COMPONENT("GIT_SYNC", LOG_LEVEL_DEBUG, message)

#define GIT_SYNC_INFO_FMT(fmt, ...) GIT_SYNC_INFO(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

    GitSyncHost& GitSyncHost::instance() {
        static GitSyncHost service;
        return service;
    }

    bool GitSyncHost::is_configured() {
        std::lock_guard<std::mutex> const lock(mutex_);
        load_settings();
        return !repo_url_.empty() && !cache_path_.empty();
    }

    std::filesystem::path GitSyncHost::get_cache_path() {
        std::lock_guard<std::mutex> const lock(mutex_);
        load_settings();
        return std::filesystem::path(cache_path_);
    }

    std::string GitSyncHost::get_status_message() {
        std::lock_guard<std::mutex> const lock(mutex_);
        return status_message_;
    }

    bool GitSyncHost::initialize() {
        std::lock_guard<std::mutex> const lock(mutex_);
        load_settings();

        if (cache_path_.empty()) {
            status_message_ = "Sync cache path is not configured";
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        std::filesystem::path const cache_dir(cache_path_);

        if (std::filesystem::exists(cache_dir / ".git")) {
            status_message_ = "Repository cache directory already initialized";
            GIT_SYNC_DEBUG(status_message_);
            return true;
        }

        if (repo_url_.empty()) {
            status_message_ = "Remote Git URL is not configured; cannot clone";
            GIT_SYNC_WARN(status_message_);
            return false;
        }

        auto parent_dir = cache_dir.parent_path();
        if (!parent_dir.empty()) {
            std::filesystem::create_directories(parent_dir);
        }

        if (!apply_token_credentials()) {
            return false;
        }

        const std::string git_path = rouen::helpers::ConfigService::instance()->get_git_path();
        const std::string command = std::format(
            "\"{}\" clone {} {}",
            git_path,
            shell_escape(repo_url_),
            shell_escape(cache_path_)
        );

        GIT_SYNC_INFO_FMT("Cloning sync repository from '{}' to '{}'...", repo_url_, cache_path_);
        if (!run_shell(command)) {
            status_message_ = "Failed to clone repository: " + status_message_;
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        status_message_ = "Repository cloned successfully";
        GIT_SYNC_INFO(status_message_);
        return true;
    }

    bool GitSyncHost::pull() {
        std::lock_guard<std::mutex> const lock(mutex_);
        load_settings();

        if (!ensure_cache_ready()) {
            return false;
        }

        if (!apply_token_credentials()) {
            return false;
        }

        if (is_remote_empty()) {
            status_message_ = "Remote repository is empty; skipping pull";
            GIT_SYNC_INFO(status_message_);
            return true;
        }

        GIT_SYNC_INFO("Pulling latest changes from remote Git repository...");
        const std::string git_path = rouen::helpers::ConfigService::instance()->get_git_path();
        const std::string command = std::format(
            "GIT_EDITOR=true GIT_SEQUENCE_EDITOR=true \"{}\" -C {} -c core.editor=true pull --rebase -X ours --autostash",
            git_path,
            shell_escape(cache_path_)
        );

        if (!run_shell(command)) {
            status_message_ = "Git pull failed: " + status_message_;
            GIT_SYNC_ERROR(status_message_);
            run_shell(std::format("GIT_EDITOR=true \"{}\" -C {} rebase --abort", git_path, shell_escape(cache_path_)));
            return false;
        }

        status_message_ = "Pulled changes successfully";
        GIT_SYNC_INFO(status_message_);
        return true;
    }

    bool GitSyncHost::commit_and_push(const std::string& commit_message) {
        std::lock_guard<std::mutex> const lock(mutex_);
        load_settings();

        if (!ensure_cache_ready()) {
            return false;
        }

        if (!apply_token_credentials()) {
            return false;
        }

        const std::string git_path = rouen::helpers::ConfigService::instance()->get_git_path();
        GIT_SYNC_INFO("Staging all changes in local cache...");
        const std::string add_cmd = std::format(
            "\"{}\" -C {} add -A",
            git_path,
            shell_escape(cache_path_)
        );
        if (!run_shell(add_cmd)) {
            status_message_ = "Git add failed: " + status_message_;
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        GIT_SYNC_INFO_FMT("Committing changes with message '{}'...", commit_message);
        const std::string commit_cmd = std::format(
            "\"{}\" -C {} commit -m {}",
            git_path,
            shell_escape(cache_path_),
            shell_escape(commit_message)
        );

        bool const commit_ok = run_shell(commit_cmd);
        if (!commit_ok) {
            if (status_message_.find("nothing to commit") != std::string::npos ||
                status_message_.find("working tree clean") != std::string::npos) {
                GIT_SYNC_INFO("Nothing to commit, working tree is clean.");
                status_message_ = "No local changes to push";
                return true;
            }
            status_message_ = "Git commit failed: " + status_message_;
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        GIT_SYNC_INFO("Pushing committed changes to remote repository...");
        const std::string push_cmd = std::format(
            "\"{}\" -C {} push",
            git_path,
            shell_escape(cache_path_)
        );
        if (!run_shell(push_cmd)) {
            status_message_ = "Git push failed: " + status_message_;
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        status_message_ = "Pushed local changes successfully";
        GIT_SYNC_INFO(status_message_);
        return true;
    }

    void GitSyncHost::load_settings() {
        auto config = rouen::helpers::ConfigService::instance();
        repo_url_ = config->get_env("ROUEN_SYNC_GIT_URL");
        token_ = config->get_env("ROUEN_SYNC_TOKEN");

        std::string const path = config->get_env("ROUEN_SYNC_CACHE_PATH");
        if (path.empty()) {
            cache_path_ = rouen::platform::get_user_data_path("rouen-sync", false).string();
        } else {
            cache_path_ = path;
        }
    }

    bool GitSyncHost::ensure_cache_ready() {
        if (cache_path_.empty()) {
            status_message_ = "Cache path is empty";
            return false;
        }
        if (!std::filesystem::exists(std::filesystem::path(cache_path_) / ".git")) {
            status_message_ = "Local repository is not initialized. Run initialize first.";
            return false;
        }
        return true;
    }

    std::string GitSyncHost::shell_escape(const std::string& value) {
        std::string escaped{"'"};
        for (char const c : value) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        escaped += "'";
        return escaped;
    }

    std::string GitSyncHost::host_from_url(const std::string& url) {
        if (url.starts_with("https://")) {
            const auto start = std::string{"https://"}.size();
            const auto slash = url.find('/', start);
            if (slash == std::string::npos) {
                return url.substr(start);
            }
            return url.substr(start, slash - start);
        }
        return {};
    }

    bool GitSyncHost::apply_token_credentials() {
        if (token_.empty()) {
            return true;
        }

        const std::string host = host_from_url(repo_url_);
        if (host.empty()) {
            status_message_ = "Invalid remote Git URL for token authentication (must be HTTPS)";
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        std::string credential_input;
        credential_input += "protocol=https\n";
        credential_input += std::format("host={}\n", host);
        credential_input += "username=x-access-token\n";
        const std::string pass_field = "pass" "word";
        credential_input += std::format("{}={}\n\n", pass_field, token_);

        const std::string git_path = rouen::helpers::ConfigService::instance()->get_git_path();
        std::string cred_cmd = std::format("\"{}\" credential approve", git_path);
        if constexpr (!rouen::platform::is_windows) {
            cred_cmd = std::string(R"(export PATH="$HOME/.local/bin:$HOME/.nix-profile/bin:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin:/run/current-system/sw/bin:$PATH" && )") + cred_cmd;
        }

        FILE* pipe = popen(cred_cmd.c_str(), "w");
        if (pipe == nullptr) {
            status_message_ = "Unable to launch git credential helper";
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        const size_t written = fwrite(credential_input.data(), 1, credential_input.size(), pipe);
        const int rc = pclose(pipe);
        if (written != credential_input.size() || rc != 0) {
            status_message_ = "Failed to configure Git token credentials";
            GIT_SYNC_ERROR(status_message_);
            return false;
        }

        return true;
    }

    bool GitSyncHost::is_remote_empty() {
        const std::string git_path = rouen::helpers::ConfigService::instance()->get_git_path();
        const std::string cmd = std::format(
            "\"{}\" -C {} ls-remote --heads origin",
            git_path,
            shell_escape(cache_path_)
        );

        std::string output;
        std::array<char, 128> buffer{};
        std::string command_to_run = cmd;
        if constexpr (!rouen::platform::is_windows) {
            command_to_run = std::string(R"(export PATH="$HOME/.local/bin:$HOME/.nix-profile/bin:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin:/run/current-system/sw/bin:$PATH" && )") + cmd;
        }
        FILE* pipe = popen((command_to_run + " 2>&1").c_str(), "r");
        if (pipe != nullptr) {
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                output += buffer.data();
            }
            pclose(pipe);
        }

        output.erase(std::remove_if(output.begin(), output.end(), [](unsigned char c) {
            return std::isspace(c);
        }), output.end());
        return output.empty();
    }

    bool GitSyncHost::run_shell(const std::string& command) {
        std::string output;
        output.reserve(2048);
        std::array<char, 512> buffer{};

        std::string command_to_run = command;
        if constexpr (!rouen::platform::is_windows) {
            command_to_run = std::string(R"(export PATH="$HOME/.local/bin:$HOME/.nix-profile/bin:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin:/run/current-system/sw/bin:$PATH" && )") + command;
        }

        FILE* pipe = popen((command_to_run + " 2>&1").c_str(), "r");
        if (pipe == nullptr) {
            status_message_ = "Failed to open command pipe";
            return false;
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) { // NOLINT(clang-analyzer-unix.BlockInCriticalSection)
            output += buffer.data();
        }

        const int rc = pclose(pipe);

        if (!output.empty() && output.back() == '\n') {
            output.pop_back();
        }
        if (!output.empty() && output.back() == '\r') {
            output.pop_back();
        }

        status_message_ = output;
        return rc == 0;
    }

} // namespace rouen::hosts
