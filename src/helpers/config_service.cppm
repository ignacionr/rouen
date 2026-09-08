module;

#include <utility>
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

export module rouen.helpers.config_service;

export namespace rouen::helpers {

    class ConfigService {
    public:
        enum class Category {
            API_CREDENTIALS,
            JIRA_PROFILES,
            BYBIT_CONFIG,
            LLM_CONFIG,
            SYSTEM_PATHS,
            EXECUTABLE_PATHS,
            DATABASE_CONFIG,
            LOGGING_CONFIG,
            HTTP_SSL_CONFIG,
            GENERAL
        };

        struct ConfigEntry {
            std::string key;
            std::string value;
            Category category;
            bool is_required;
            bool is_sensitive;
            std::string description;
            std::optional<std::string> default_value;
        };

        static std::shared_ptr<ConfigService> instance();

        std::string get_env(std::string_view name) const;
        std::optional<std::string> get_env_optional(std::string_view name) const;
        bool has_env(std::string_view name) const;
        bool set_env_value(std::string_view name, std::string_view value, bool persist_to_env_file = false);

        template<typename T>
        std::optional<T> get_typed(const std::string& name) const;

        void register_config(const std::string& name, Category category, 
                           bool is_required = false, bool is_sensitive = false,
                           const std::string& description = "",
                           const std::optional<std::string>& default_value = std::nullopt);

        std::vector<std::string> validate_required_configs() const;
        std::vector<ConfigEntry> get_configs_by_category(Category category) const;

        std::string get_api_key(const std::string& service_name) const;
        static std::vector<std::string> get_jira_profiles();
        std::string get_jira_config(const std::string& profile, const std::string& key) const;
        
        std::string get_bybit_api_key() const;
        std::string get_bybit_secret() const;
        std::string get_bybit_host() const;

        std::string resolve_path_with_env(const std::string& path) const;
        
        std::string get_mpv_path() const;
        std::string get_cmake_path() const;
        std::string get_git_path() const;
        std::string get_say_path() const;
        std::string get_bash_path() const;
        std::string get_sudo_path() const;
        std::string get_vscode_path() const;
        std::string get_ping_path() const;

        std::string get_ytdlp_cookie_args() const;
        bool refresh_youtube_cookies() const;
        void clear_youtube_cookies() const;

        static bool validate_executable_path(const std::string& path);
        std::string get_validated_executable_path(const std::string& env_name, const std::string& default_value) const;

        void refresh_cache();
        void set_change_callback(std::function<void(const std::string&, const std::string&)> callback);

        void log_configuration_status() const;
        std::vector<ConfigEntry> get_all_configs() const;
        
        static std::map<std::string, std::string> get_all_env_vars();

        bool load_env_file(const std::string& file_path = "");
        bool export_to_env_file(const std::string& file_path = "") const;
        static std::string get_env_file_path();

    private:
        ConfigService() = default;
        
        mutable std::mutex mutex_;
        mutable std::unordered_map<std::string, std::string> cache_;
        std::unordered_map<std::string, ConfigEntry> registered_configs_;
        std::function<void(const std::string&, const std::string&)> change_callback_;
        
        std::unordered_map<std::string, std::string> env_file_values_;
        bool env_file_loaded_ = false;

        void update_cache_entry(const std::string& name) const;
        static std::string mask_sensitive_value(const std::string& value, bool is_sensitive);
        bool is_sensitive_config(const std::string& name) const;
        void register_default_configs();
        
        static std::pair<std::string, std::string> parse_env_line(const std::string& line);
        static std::string get_executable_directory();
        std::string get_env_value_priority(const std::string& name) const;
        
        std::vector<ConfigEntry> get_configs_by_category_unlocked(Category category) const;
        
        static std::shared_ptr<ConfigService> instance_;
        static std::mutex instance_mutex_;
    };

    template<>
    std::optional<int> ConfigService::get_typed<int>(const std::string& name) const;
    
    template<>
    std::optional<bool> ConfigService::get_typed<bool>(const std::string& name) const;
    
    template<>
    std::optional<double> ConfigService::get_typed<double>(const std::string& name) const;

} // namespace rouen::helpers
