#include "config_service.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
    #include <stdlib.h>
#else
    #include <cstdlib>
    extern char **environ;
#endif

namespace rouen::helpers {

    // Static member definitions
    std::shared_ptr<ConfigService> ConfigService::instance_ = nullptr;
    std::mutex ConfigService::instance_mutex_;

    std::shared_ptr<ConfigService> ConfigService::instance() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (!instance_) {
            instance_ = std::shared_ptr<ConfigService>(new ConfigService());
            // Register default configurations
            instance_->register_default_configs();
        }
        return instance_;
    }

    std::string ConfigService::get_env(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check cache first
        auto cache_it = cache_.find(name);
        if (cache_it != cache_.end()) {
            return cache_it->second;
        }
        
        // Use platform utility for consistent access
        std::string value = platform::get_env(name);
        cache_[name] = value;
        
        CONFIG_DEBUG_FMT("Retrieved environment variable '{}': '{}'", 
                        name, mask_sensitive_value(value, is_sensitive_config(name)));
        
        return value;
    }

    std::optional<std::string> ConfigService::get_env_optional(const std::string& name) const {
        std::string value = get_env(name);
        return value.empty() ? std::nullopt : std::make_optional(value);
    }

    bool ConfigService::has_env(const std::string& name) const {
        return !get_env(name).empty();
    }

    void ConfigService::register_config(const std::string& name, Category category, 
                                       bool is_required, bool is_sensitive,
                                       const std::string& description,
                                       const std::optional<std::string>& default_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get the environment value without re-acquiring the mutex
        std::string env_value = platform::get_env(name);
        cache_[name] = env_value;  // Update cache while we have the lock
        
        ConfigEntry entry{
            .key = name,
            .value = env_value,
            .category = category,
            .is_required = is_required,
            .is_sensitive = is_sensitive,
            .description = description,
            .default_value = default_value
        };
        
        registered_configs_[name] = entry;
        
        CONFIG_DEBUG_FMT("Registered configuration '{}' in category {}", 
                        name, static_cast<int>(category));
    }

    std::vector<std::string> ConfigService::validate_required_configs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> missing_configs;
        
        for (const auto& [name, config] : registered_configs_) {
            if (config.is_required && platform::get_env(name).empty()) {
                missing_configs.push_back(name);
                CONFIG_WARN_FMT("Required configuration '{}' is missing", name);
            }
        }
        
        return missing_configs;
    }

    std::vector<ConfigService::ConfigEntry> ConfigService::get_configs_by_category(Category category) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ConfigEntry> result;
        
        for (const auto& [name, config] : registered_configs_) {
            if (config.category == category) {
                ConfigEntry entry = config;
                entry.value = platform::get_env(name); // Get current value without mutex deadlock
                result.push_back(entry);
            }
        }
        
        return result;
    }

    // Private helper methods
    std::vector<ConfigService::ConfigEntry> ConfigService::get_configs_by_category_unlocked(Category category) const {
        std::vector<ConfigEntry> result;
        
        for (const auto& [name, config] : registered_configs_) {
            if (config.category == category) {
                ConfigEntry entry = config;
                entry.value = platform::get_env(name); // Get current value without mutex deadlock
                result.push_back(entry);
            }
        }
        
        return result;
    }

    void ConfigService::update_cache_entry(const std::string& name) const {
        std::string value = platform::get_env(name);
        cache_[name] = value;
    }

    std::string ConfigService::mask_sensitive_value(const std::string& value, bool is_sensitive) const {
        if (!is_sensitive || value.empty()) {
            return value;
        }
        
        if (value.length() <= 8) {
            return "***";
        }
        
        // Show first 4 and last 4 characters, mask the middle
        return value.substr(0, 4) + "..." + value.substr(value.length() - 4);
    }

    bool ConfigService::is_sensitive_config(const std::string& name) const {
        // Check if the config is registered as sensitive
        auto it = registered_configs_.find(name);
        if (it != registered_configs_.end()) {
            return it->second.is_sensitive;
        }
        
        // Default heuristics for sensitive configurations
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        
        return lower_name.find("key") != std::string::npos ||
               lower_name.find("secret") != std::string::npos ||
               lower_name.find("token") != std::string::npos ||
               lower_name.find("password") != std::string::npos ||
               lower_name.find("auth") != std::string::npos;
    }

    std::map<std::string, std::string> ConfigService::get_all_env_vars() const {
        std::map<std::string, std::string> env_vars;
        
        // Get all environment variables (platform-specific)
        #ifdef _WIN32
            for (char **env = _environ; *env != nullptr; ++env) {
        #else
            for (char **env = environ; *env != nullptr; ++env) {
        #endif
                std::string entry = *env;
                auto pos = entry.find('=');
                if (pos != std::string::npos) {
                    std::string key = entry.substr(0, pos);
                    std::string value = entry.substr(pos + 1);
                    env_vars[key] = value;
                }
            }
        
        return env_vars;
    }

    void ConfigService::register_default_configs() {
        // API Credentials
        register_config("GROK_API_KEY", Category::API_CREDENTIALS, false, true, 
                       "Grok AI API key for enhanced features");
        register_config("BYBIT_API_KEY", Category::API_CREDENTIALS, false, true,
                       "Bybit trading API key");
        register_config("BYBIT_SECRET", Category::API_CREDENTIALS, false, true,
                       "Bybit trading API secret");
        register_config("BYBIT_HOST", Category::BYBIT_CONFIG, false, false,
                       "Bybit API host endpoint", "https://api.bybit.com");

        // System paths
        register_config("HOME", Category::SYSTEM_PATHS, false, false,
                       "User home directory");
        register_config("APPDATA", Category::SYSTEM_PATHS, false, false,
                       "Windows application data directory");
        register_config("USERPROFILE", Category::SYSTEM_PATHS, false, false,
                       "Windows user profile directory");

        // Logging configuration
        register_config("ROUEN_LOG_LEVEL", Category::LOGGING_CONFIG, false, false,
                       "Application log level (ERROR, WARN, INFO, DEBUG, TRACE)");
        register_config("ROUEN_DEBUG", Category::LOGGING_CONFIG, false, false,
                       "Enable debug mode");

        CONFIG_INFO("Default configurations registered");
    }

    void ConfigService::log_configuration_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        CONFIG_INFO("=== Configuration Service Status ===");
        CONFIG_INFO_FMT("Total registered configurations: {}", registered_configs_.size());
        CONFIG_INFO_FMT("Cached values: {}", cache_.size());
        
        // Log by category
        for (int cat = 0; cat <= static_cast<int>(Category::GENERAL); ++cat) {
            Category category = static_cast<Category>(cat);
            auto configs = get_configs_by_category_unlocked(category);
            if (!configs.empty()) {
                CONFIG_INFO_FMT("Category {}: {} configurations", static_cast<int>(category), configs.size());
                for (const auto& config : configs) {
                    std::string status = config.value.empty() ? "MISSING" : "SET";
                    if (config.is_required && config.value.empty()) {
                        status = "REQUIRED MISSING";
                    }
                    CONFIG_INFO_FMT("  {} - {}: {}", 
                                   config.key, 
                                   mask_sensitive_value(config.value, config.is_sensitive), 
                                   status);
                }
            }
        }
        
        // Validate required configurations
        std::vector<std::string> missing_configs;
        for (const auto& [name, config] : registered_configs_) {
            if (config.is_required && platform::get_env(name).empty()) {
                missing_configs.push_back(name);
            }
        }
        
        if (!missing_configs.empty()) {
            CONFIG_ERROR_FMT("Missing required configurations: {}", missing_configs.size());
            for (const auto& missing_config : missing_configs) {
                CONFIG_ERROR_FMT("  - {}", missing_config);
            }
        } else {
            CONFIG_INFO("All required configurations are present");
        }
    }

    std::string ConfigService::get_api_key(const std::string& service_name) const {
        // Try multiple common patterns for API keys
        std::vector<std::string> patterns = {
            service_name + "_API_KEY",
            service_name + "_KEY",
            service_name + "_TOKEN",
            service_name + "_SECRET"
        };
        
        for (const auto& pattern : patterns) {
            std::string value = get_env(pattern);
            if (!value.empty()) {
                CONFIG_DEBUG_FMT("Found API key for service '{}' using pattern '{}'", 
                               service_name, pattern);
                return value;
            }
        }
        
        CONFIG_WARN_FMT("No API key found for service '{}'", service_name);
        return "";
    }

    std::vector<std::string> ConfigService::get_jira_profiles() const {
        std::vector<std::string> profiles;
        
        // Common JIRA profile prefixes
        std::vector<std::string> prefixes = {"JIRA", "ATLASSIAN"};
        
        for (const auto& prefix : prefixes) {
            // Look for patterns like JIRA_ORG1_URL, JIRA_ORG2_URL, etc.
            std::regex pattern(prefix + R"(_([A-Z0-9_]+)_URL)");
            
            // Get all environment variables and check for matches
            for (const auto& [key, value] : get_all_env_vars()) {
                std::smatch match;
                if (std::regex_match(key, match, pattern)) {
                    std::string profile = match[1].str();
                    if (std::find(profiles.begin(), profiles.end(), profile) == profiles.end()) {
                        profiles.push_back(profile);
                    }
                }
            }
        }
        
        CONFIG_DEBUG_FMT("Found {} JIRA profiles", profiles.size());
        return profiles;
    }

    std::string ConfigService::get_jira_config(const std::string& profile, const std::string& key) const {
        // Try multiple patterns for JIRA configuration
        std::vector<std::string> patterns = {
            "JIRA_" + profile + "_" + key,
            "ATLASSIAN_" + profile + "_" + key,
            profile + "_" + key
        };
        
        for (const auto& pattern : patterns) {
            std::string value = get_env(pattern);
            if (!value.empty()) {
                return value;
            }
        }
        
        return "";
    }

    std::string ConfigService::get_bybit_api_key() const {
        return get_api_key("BYBIT");
    }

    std::string ConfigService::get_bybit_secret() const {
        std::string secret = get_env("BYBIT_SECRET");
        if (secret.empty()) {
            secret = get_env("BYBIT_API_SECRET");
        }
        return secret;
    }

    std::string ConfigService::get_bybit_host() const {
        std::string host = get_env("BYBIT_HOST");
        if (host.empty()) {
            host = get_env("BYBIT_ENDPOINT");
        }
        if (host.empty()) {
            host = "https://api.bybit.com"; // Default value
        }
        return host;
    }

    std::string ConfigService::resolve_path_with_env(const std::string& path) const {
        std::string result = path;
        std::regex env_var_regex(R"(\$\{?(\w+)\}?)");
        
        std::smatch match;
        std::string temp = result;
        while (std::regex_search(temp, match, env_var_regex)) {
            std::string var_name = match[1].str();
            std::string var_value = get_env(var_name);
            
            // Replace the variable with its value
            size_t pos = result.find(match[0].str());
            if (pos != std::string::npos) {
                result.replace(pos, static_cast<size_t>(match[0].length()), var_value);
            }
            
            temp = match.suffix();
        }
        
        return result;
    }

    void ConfigService::refresh_cache() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        CONFIG_DEBUG("Refreshing configuration cache");
        
        // Clear cache and reload all registered configs
        cache_.clear();
        for (const auto& [name, config] : registered_configs_) {
            update_cache_entry(name);
        }
        
        // Trigger change callback if set
        if (change_callback_) {
            for (const auto& [name, value] : cache_) {
                change_callback_(name, value);
            }
        }
    }

    void ConfigService::set_change_callback(std::function<void(const std::string&, const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        change_callback_ = callback;
    }

    // Template specializations
    template<>
    std::optional<int> ConfigService::get_typed<int>(const std::string& name) const {
        std::string value = get_env(name);
        if (value.empty()) {
            return std::nullopt;
        }
        
        try {
            return std::stoi(value);
        } catch (const std::exception& e) {
            CONFIG_WARN_FMT("Failed to convert '{}' to int for config '{}': {}", 
                           value, name, e.what());
            return std::nullopt;
        }
    }

    template<>
    std::optional<bool> ConfigService::get_typed<bool>(const std::string& name) const {
        std::string value = get_env(name);
        if (value.empty()) {
            return std::nullopt;
        }
        
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        
        if (lower_value == "true" || lower_value == "1" || lower_value == "yes" || lower_value == "on") {
            return true;
        } else if (lower_value == "false" || lower_value == "0" || lower_value == "no" || lower_value == "off") {
            return false;
        }
        
        CONFIG_WARN_FMT("Invalid boolean value '{}' for config '{}'", value, name);
        return std::nullopt;
    }

    template<>
    std::optional<double> ConfigService::get_typed<double>(const std::string& name) const {
        std::string value = get_env(name);
        if (value.empty()) {
            return std::nullopt;
        }
        
        try {
            return std::stod(value);
        } catch (const std::exception& e) {
            CONFIG_WARN_FMT("Failed to convert '{}' to double for config '{}': {}", 
                           value, name, e.what());
            return std::nullopt;
        }
    }

} // namespace rouen::helpers
