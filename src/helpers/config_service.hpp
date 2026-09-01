#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <map>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include "platform_utils.hpp"
#include "debug.hpp"

namespace rouen::helpers {

    class ConfigService {
    public:
        // Configuration categories for better organization
        enum class Category {
            API_CREDENTIALS,    // API keys, tokens, secrets
            JIRA_PROFILES,     // JIRA organization profiles
            BYBIT_CONFIG,      // Bybit trading configuration
            LLM_CONFIG,        // Large Language Model configuration
            SYSTEM_PATHS,      // File system paths and directories
            EXECUTABLE_PATHS,  // Paths to external executables
            DATABASE_CONFIG,   // Database connection settings
            LOGGING_CONFIG,    // Logging and debug settings
            HTTP_SSL_CONFIG,   // HTTP/SSL connection settings
            GENERAL           // General application settings
        };

        // Configuration entry with metadata
        struct ConfigEntry {
            std::string key;
            std::string value;
            Category category;
            bool is_required;
            bool is_sensitive;  // For masking sensitive values in logs
            std::string description;
            std::optional<std::string> default_value;
        };

        // Get singleton instance
        static std::shared_ptr<ConfigService> instance();

        // Core environment variable access
        std::string get_env(std::string_view name) const;
        std::optional<std::string> get_env_optional(std::string_view name) const;
        bool has_env(std::string_view name) const;
        bool set_env_value(std::string_view name, std::string_view value, bool persist_to_env_file = false);

        // Typed getters with validation
        template<typename T>
        std::optional<T> get_typed(const std::string& name) const;

        // Configuration registration and validation
        void register_config(const std::string& name, Category category, 
                           bool is_required = false, bool is_sensitive = false,
                           const std::string& description = "",
                           const std::optional<std::string>& default_value = std::nullopt);

        // Validate all required configurations
        std::vector<std::string> validate_required_configs() const;

        // Get configurations by category
        std::vector<ConfigEntry> get_configs_by_category(Category category) const;

        // Specialized getters for common patterns
        std::string get_api_key(const std::string& service_name) const;
        static std::vector<std::string> get_jira_profiles() ;
        std::string get_jira_config(const std::string& profile, const std::string& key) const;
        
        // Bybit configuration helpers
        std::string get_bybit_api_key() const;
        std::string get_bybit_secret() const;
        std::string get_bybit_host() const;

        // System path helpers
        std::string resolve_path_with_env(const std::string& path) const;
        
        // Executable path helpers
        std::string get_mpv_path() const;
        std::string get_cmake_path() const;
        std::string get_git_path() const;
        std::string get_say_path() const;
        std::string get_bash_path() const;
        std::string get_sudo_path() const;
        std::string get_vscode_path() const; // Added for VS Code
        std::string get_ping_path() const;  // Added for ping

        // yt-dlp cookie options helper
        std::string get_ytdlp_cookie_args() const;
        bool refresh_youtube_cookies() const;
        void clear_youtube_cookies() const;

        // Path validation helpers
        static bool validate_executable_path(const std::string& path);
        std::string get_validated_executable_path(const std::string& env_name, const std::string& default_value) const;

        // Configuration monitoring and refresh
        void refresh_cache();
        void set_change_callback(std::function<void(const std::string&, const std::string&)> callback);

        // Debug and diagnostics
        void log_configuration_status() const;
        std::vector<ConfigEntry> get_all_configs() const;
        
        // System monitoring - get all environment variables
        static std::map<std::string, std::string> get_all_env_vars();

        // .env file support
        bool load_env_file(const std::string& file_path = "");
        bool export_to_env_file(const std::string& file_path = "") const;
        static std::string get_env_file_path() ;

    private:
        ConfigService() = default;
        
        mutable std::mutex mutex_;
        mutable std::unordered_map<std::string, std::string> cache_;
        std::unordered_map<std::string, ConfigEntry> registered_configs_;
        std::function<void(const std::string&, const std::string&)> change_callback_;
        
        // .env file support
        std::unordered_map<std::string, std::string> env_file_values_;
        bool env_file_loaded_ = false;

        // Internal helpers
        void update_cache_entry(const std::string& name) const;
        static std::string mask_sensitive_value(const std::string& value, bool is_sensitive);
        bool is_sensitive_config(const std::string& name) const;
        void register_default_configs();
        
        // .env file parsing helpers
        static std::pair<std::string, std::string> parse_env_line(const std::string& line);
        static std::string get_executable_directory();
        std::string get_env_value_priority(const std::string& name) const;  // Check .env file first, then environment
        
        // Private helper that doesn't acquire mutex (assumes caller already has lock)
        std::vector<ConfigEntry> get_configs_by_category_unlocked(Category category) const;
        
        static std::shared_ptr<ConfigService> instance_;
        static std::mutex instance_mutex_;
    };

    // Template specializations for common types
    template<>
    std::optional<int> ConfigService::get_typed<int>(const std::string& name) const;
    
    template<>
    std::optional<bool> ConfigService::get_typed<bool>(const std::string& name) const;
    
    template<>
    std::optional<double> ConfigService::get_typed<double>(const std::string& name) const;

} // namespace rouen::helpers

// Convenience macros for configuration access
#define CONFIG_SERVICE() rouen::helpers::ConfigService::instance()
#define GET_CONFIG(name) CONFIG_SERVICE()->get_env(name)
#define GET_CONFIG_OPTIONAL(name) CONFIG_SERVICE()->get_env_optional(name)
#define GET_CONFIG_OPT(name) GET_CONFIG_OPTIONAL(name)
#define HAS_CONFIG(name) CONFIG_SERVICE()->has_env(name)

// Macro for registering configurations easily
#define REGISTER_CONFIG(name, category, required, sensitive, desc, default_val) \
    CONFIG_SERVICE()->register_config(name, category, required, sensitive, desc, default_val)

// Macros for specific configuration categories
#define GET_API_KEY(service) CONFIG_SERVICE()->get_api_key(service)
#define GET_JIRA_CONFIG(profile, key) CONFIG_SERVICE()->get_jira_config(profile, key)

// Macros for logging with configuration service
#define CONFIG_ERROR(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_ERROR, message)
#define CONFIG_WARN(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_WARN, message)
#define CONFIG_INFO(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_INFO, message)
#define CONFIG_DEBUG(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_DEBUG, message)

#define CONFIG_ERROR_FMT(fmt, ...) CONFIG_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_WARN_FMT(fmt, ...) CONFIG_WARN(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_INFO_FMT(fmt, ...) CONFIG_INFO(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_DEBUG_FMT(fmt, ...) CONFIG_DEBUG(debug::format_log(fmt, __VA_ARGS__))
