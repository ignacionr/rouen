#include "config_service.hpp"
#include "platform_utils.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <ranges>
#include <chrono>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#elif defined(_WIN32)
#include <windows.h>
#endif

#ifdef _WIN32
    #include <stdlib.h>
#else
    #include <cstdlib>
    extern char **environ; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

namespace rouen::helpers {

    namespace {
        bool set_process_environment_variable(const std::string& name, const std::string& value) {
#ifdef _WIN32
            return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
            return ::setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
        }
    }

    // Static member definitions
    std::shared_ptr<ConfigService> ConfigService::instance_ = nullptr;
    std::mutex ConfigService::instance_mutex_;

    std::shared_ptr<ConfigService> ConfigService::instance() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (!instance_) {
            instance_ = std::shared_ptr<ConfigService>(new ConfigService());
            // Load .env file if it exists
            instance_->load_env_file();
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
        
        // Use priority-based lookup (.env file first, then environment)
        std::string value = get_env_value_priority(name);
        cache_[name] = value;
        
        CONFIG_DEBUG_FMT("Retrieved configuration '{}': '{}'", 
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

    bool ConfigService::set_env_value(const std::string& name, const std::string& value, bool persist_to_env_file) {
        std::function<void(const std::string&, const std::string&)> callback;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!set_process_environment_variable(name, value)) {
                CONFIG_ERROR_FMT("Failed to set environment variable '{}'", name);
                return false;
            }

            cache_[name] = value;

            if (auto it = registered_configs_.find(name); it != registered_configs_.end()) {
                it->second.value = value;
            }

            if (env_file_loaded_ || persist_to_env_file) {
                env_file_values_[name] = value;
                env_file_loaded_ = true;
            }

            callback = change_callback_;
        }

        if (callback) {
            callback(name, value);
        }

        if (persist_to_env_file) {
            return export_to_env_file();
        }

        return true;
    }

    void ConfigService::register_config(const std::string& name, Category category, 
                                       bool is_required, bool is_sensitive,
                                       const std::string& description,
                                       const std::optional<std::string>& default_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Respect configured priority (.env first, then process environment)
        // and avoid clobbering existing cache entries populated during .env load.
        std::string env_value = get_env_value_priority(name);
        cache_.insert_or_assign(name, env_value);
        
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
            if (config.is_required && get_env_value_priority(name).empty()) {
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
                entry.value = get_env_value_priority(name);
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
                entry.value = get_env_value_priority(name);
                result.push_back(entry);
            }
        }
        
        return result;
    }

    void ConfigService::update_cache_entry(const std::string& name) const {
        std::string value = get_env_value_priority(name);
        cache_[name] = value;
    }

    std::string ConfigService::mask_sensitive_value(const std::string& value, bool is_sensitive) {
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

    std::map<std::string, std::string> ConfigService::get_all_env_vars() {
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
        register_config("OPENWEATHER_KEY", Category::API_CREDENTIALS, false, true,
                       "OpenWeather API key for weather data (get free key at openweathermap.org/api)");
        register_config("BYBIT_API_KEY", Category::API_CREDENTIALS, false, true,
                       "Bybit trading API key");
        register_config("BYBIT_SECRET", Category::API_CREDENTIALS, false, true,
                       "Bybit trading API secret");
        register_config("BYBIT_HOST", Category::BYBIT_CONFIG, false, false,
                       "Bybit API host endpoint", "https://api.bybit.com");
        
        // Trello Configuration
        register_config("TRELLO_API_KEY", Category::API_CREDENTIALS, false, true,
                       "Trello API key for board access");
        register_config("TRELLO_API_SECRET", Category::API_CREDENTIALS, false, true,
                       "Trello API secret for OAuth and webhooks");
        register_config("TRELLO_TOKEN", Category::API_CREDENTIALS, false, true,
                       "Trello user token for authentication");
        register_config("TRELLO_HOST", Category::API_CREDENTIALS, false, false,
                       "Trello API host endpoint", "https://api.trello.com");
                       
        // LLM Configuration
        register_config("LLM_PROVIDER", Category::LLM_CONFIG, false, false,
                       "Default LLM provider (grok, openai, groq, gemini, custom)", "grok");
        register_config("LLM_CUSTOM_URL", Category::LLM_CONFIG, false, false,
                       "Custom LLM API base URL (when provider is 'custom')");
        register_config("LLM_CUSTOM_MODEL", Category::LLM_CONFIG, false, false,
                       "Custom LLM model name (when provider is 'custom')", "gpt-3.5-turbo");
        register_config("LLM_CUSTOM_API_KEY", Category::LLM_CONFIG, false, true,
                       "Custom LLM API key (when provider is 'custom')");
        register_config("OPENAI_API_KEY", Category::API_CREDENTIALS, false, true,
                       "OpenAI API key for GPT models");
        register_config("GROQ_API_KEY", Category::API_CREDENTIALS, false, true,
                       "Groq API key for fast inference");
        register_config("GEMINI_API_KEY", Category::API_CREDENTIALS, false, true,
                       "Google Gemini API key for Gemini models");
                       
        // TMDB Configuration
        register_config("TMDB_API_KEY", Category::API_CREDENTIALS, false, true,
                       "The Movie Database (TMDB) API Key (v3)");
        register_config("TMDB_TOKEN", Category::API_CREDENTIALS, false, true,
                       "The Movie Database (TMDB) Read Access Bearer Token (v4)");
                       
        // HTTP/SSL Configuration
        register_config("ROUEN_SSL_MODE", Category::HTTP_SSL_CONFIG, false, false,
                       "SSL mode: strict (default), relaxed, compatible, atlassian, insecure", "strict");
        register_config("ROUEN_SSL_VERIFY_PEER", Category::HTTP_SSL_CONFIG, false, false,
                       "Verify peer certificate (1=true, 0=false)", "1");
        register_config("ROUEN_SSL_VERIFY_HOST", Category::HTTP_SSL_CONFIG, false, false,
                       "Verify host in certificate (1=true, 0=false)", "1");
        register_config("ROUEN_SSL_CHECK_REVOCATION", Category::HTTP_SSL_CONFIG, false, false,
                       "Check certificate revocation (1=true, 0=false)", "1");

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

        // Executable paths
        register_config("MPV_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to MPV media player executable", "mpv");
        register_config("CMAKE_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to CMake build system executable", "cmake");
        register_config("GIT_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to Git version control executable", "git");
        register_config("SAY_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to system text-to-speech executable", "say");
        register_config("BASH_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to Bash shell executable", "/bin/bash");
        register_config("SUDO_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to sudo privilege escalation executable", "sudo");
        register_config("VSCODE_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to the VS Code executable (e.g., code)", "code");
        register_config("PING_PATH", Category::EXECUTABLE_PATHS, false, false,
                       "Path to the ping executable (e.g., ping)", "ping");
        register_config("ROUEN_SPOKEN_NOTIFICATIONS", Category::GENERAL, false, false,
                       "Enable spoken notifications (1=true, 0=false)", "1");
        register_config("ROUEN_COOKIES_BROWSER", Category::GENERAL, false, false,
                       "Browser to extract cookies from for yt-dlp (chrome, safari, firefox, brave, edge, etc.)");

        CONFIG_INFO("Default configurations registered");
    }

    void ConfigService::log_configuration_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        CONFIG_INFO("=== Configuration Service Status ===");
        CONFIG_INFO_FMT("Total registered configurations: {}", registered_configs_.size());
        CONFIG_INFO_FMT("Cached values: {}", cache_.size());
        
        // Log by category
        for (int cat = 0; cat <= static_cast<int>(Category::GENERAL); ++cat) {
            auto category = static_cast<Category>(cat);
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

/**
 * Validates if an executable path exists and is executable
 * 
 * @param path The path to validate
 * @return true if the path exists and is executable, false otherwise
 */
bool ConfigService::validate_executable_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    // If the path contains no directory separators, we assume it's in PATH
    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
        // For commands available in PATH, we can't directly check if they exist
        // A more thorough check would use "which" or similar, but for simplicity
        // we just return true here
        return true;
    }
    
    // Check if the file exists and is executable
    std::filesystem::path file_path(path);
    if (!std::filesystem::exists(file_path)) {
        return false;
    }
    
    // On POSIX systems, check if the file is executable
#ifdef _WIN32
    // On Windows, check for .exe, .com, .bat, .cmd extensions
    std::string extension = file_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return extension == ".exe" || extension == ".com" || extension == ".bat" || extension == ".cmd";
#else
    // On POSIX systems, check if the file has execute permission
    namespace fs = std::filesystem;
    try {
        fs::perms p = fs::status(file_path).permissions();
        return (p & fs::perms::owner_exec) != fs::perms::none ||
               (p & fs::perms::group_exec) != fs::perms::none ||
               (p & fs::perms::others_exec) != fs::perms::none;
    } catch (const std::exception&) {
        return false;
    }
#endif
}

/**
 * Gets a validated executable path, falling back to a default value if the configured path is invalid
 */
std::string ConfigService::get_validated_executable_path(const std::string& env_name, const std::string& default_value) const {
    std::string path = get_env(env_name);
    
    if (path.empty()) {
        path = default_value;
    }
    
    // Check if the path is valid as is
    if (validate_executable_path(path)) {
        return path;
    }
    
    // Try to resolve the executable (handles Nix/Homebrew and system PATH paths)
    std::string resolved_path = rouen::platform::find_executable(path);
    if (validate_executable_path(resolved_path)) {
        return resolved_path;
    }
    
    // Log a warning that we're using the default
    CONFIG_WARN_FMT("Configured path for {} is invalid. Using default: {}", env_name, default_value);
    
    return default_value;
}

// Executable path getters with validation
std::string ConfigService::get_mpv_path() const {
    return get_validated_executable_path("MPV_PATH", "mpv");
}

std::string ConfigService::get_cmake_path() const {
    return get_validated_executable_path("CMAKE_PATH", "cmake");
}

std::string ConfigService::get_git_path() const {
    return get_validated_executable_path("GIT_PATH", "git");
}

std::string ConfigService::get_say_path() const {
    return get_validated_executable_path("SAY_PATH", "say");
}

std::string ConfigService::get_bash_path() const {
    return get_validated_executable_path("BASH_PATH", "/bin/bash");
}

std::string ConfigService::get_sudo_path() const {
    return get_validated_executable_path("SUDO_PATH", "sudo");
}

std::string ConfigService::get_vscode_path() const {
    return get_validated_executable_path("VSCODE_PATH", "code");
}

std::string ConfigService::get_ping_path() const {
    return get_validated_executable_path("PING_PATH", "ping");
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
        change_callback_ = std::move(callback);
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
        }
        if (lower_value == "false" || lower_value == "0" || lower_value == "no" || lower_value == "off") {
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

    // .env file support implementation
    bool ConfigService::load_env_file(const std::string& file_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        CONFIG_DEBUG_FMT("ConfigService::load_env_file() called with file_path: '{}'", file_path);
        
        std::string env_file_path = file_path.empty() ? get_env_file_path() : file_path;
        
        CONFIG_DEBUG_FMT("ConfigService: Resolved env_file_path to: {}", env_file_path);
        
        // Check if .env file exists
        if (!std::filesystem::exists(env_file_path)) {
            CONFIG_DEBUG_FMT(".env file not found at: {}", env_file_path);
            return false;
        }
        
        CONFIG_DEBUG_FMT("ConfigService: .env file exists, attempting to open: {}", env_file_path);
        
        // Check if .env file exists
        if (!std::filesystem::exists(env_file_path)) {
            CONFIG_DEBUG_FMT(".env file not found at: {}", env_file_path);
            return false;
        }
        
        std::ifstream env_file(env_file_path);
        if (!env_file.is_open()) {
            CONFIG_ERROR_FMT("Failed to open .env file: {}", env_file_path);
            return false;
        }
        
        // Clear existing .env values
        env_file_values_.clear();
        
        std::string line;
        int line_number = 0;
        while (std::getline(env_file, line)) {
            line_number++;
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            auto [key, value] = parse_env_line(line);
            if (!key.empty()) {
                env_file_values_[key] = value;
                CONFIG_DEBUG_FMT("Loaded from .env: {} = {}", key, 
                               mask_sensitive_value(value, is_sensitive_config(key)));
            } else {
                CONFIG_WARN_FMT("Invalid .env line {} in {}: {}", line_number, env_file_path, line);
            }
        }
        
        env_file_loaded_ = true;
        
        // Clear cache to force re-evaluation with new .env values
        cache_.clear();
        
        CONFIG_INFO_FMT("Loaded {} configuration entries from .env file: {}", 
                       env_file_values_.size(), env_file_path);
        
        return true;
    }

    bool ConfigService::export_to_env_file(const std::string& file_path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string env_file_path = file_path.empty() ? get_env_file_path() : file_path;
        
        std::ofstream env_file(env_file_path);
        if (!env_file.is_open()) {
            CONFIG_ERROR_FMT("Failed to create .env file: {}", env_file_path);
            return false;
        }
        
        // Write header comment
        env_file << "# Rouen Application Configuration\n";
        env_file << "# Generated on " << std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now()) << "\n";
        env_file << "# This file contains all registered configuration values\n\n";
        
        // Group configs by category for better organization
        std::map<Category, std::vector<ConfigEntry>> configs_by_category;
        for (const auto& [name, config] : registered_configs_) {
            // Update config value with current data
            ConfigEntry current_config = config;
            current_config.value = get_env_value_priority(name);
            configs_by_category[config.category].push_back(current_config);
        }
        
        // Category names for display
        std::map<Category, std::string> category_names = {
            {Category::API_CREDENTIALS, "API Credentials"},
            {Category::JIRA_PROFILES, "JIRA Profiles"},
            {Category::BYBIT_CONFIG, "Bybit Configuration"},
            {Category::SYSTEM_PATHS, "System Paths"},
            {Category::DATABASE_CONFIG, "Database Configuration"},
            {Category::LOGGING_CONFIG, "Logging Configuration"},
            {Category::GENERAL, "General Settings"}
        };
        
        // Write configurations by category
        for (const auto& [category, configs] : configs_by_category) {
            if (configs.empty()) continue;
            
            env_file << "# " << category_names[category] << "\n";
            
            for (const auto& config : configs) {
                // Write description as comment if available
                if (!config.description.empty()) {
                    env_file << "# " << config.description << "\n";
                }
                
                // Write additional info
                std::vector<std::string> flags;
                if (config.is_required) flags.push_back("REQUIRED");
                if (config.is_sensitive) flags.push_back("SENSITIVE");
                if (!flags.empty()) {
                    std::string flags_str;
                    for (size_t i = 0; i < flags.size(); ++i) {
                        if (i > 0) flags_str += ", ";
                        flags_str += flags[i];
                    }
                    env_file << "# " << std::format("({})", flags_str) << "\n";
                }
                
                // Write the actual configuration
                if (config.value.empty() && config.default_value.has_value()) {
                    env_file << "# " << config.key << "=" << config.default_value.value() 
                            << "  # (default value - uncomment to override)\n";
                } else {
                    env_file << config.key << "=" << config.value << "\n";
                }
                
                env_file << "\n";
            }
        }
        
        CONFIG_INFO_FMT("Exported {} configuration entries to .env file: {}", 
                       registered_configs_.size(), env_file_path);
        
        return true;
    }

    std::string ConfigService::get_env_file_path() const {
        // Debug: Show what directories we're checking
        CONFIG_DEBUG("ConfigService: Looking for .env file...");
        CONFIG_DEBUG_FMT("ConfigService: Current working directory: {}", std::filesystem::current_path().string());
        CONFIG_DEBUG_FMT("ConfigService: Executable directory: {}", get_executable_directory());
        
        // Try multiple locations for .env file in order of preference:
        
        // 1. Current working directory (for development - VS Code sets this correctly)
        std::string cwd_env = std::filesystem::current_path().string() + "/.env";
        CONFIG_DEBUG_FMT("ConfigService: Checking current working dir: {}", cwd_env);
        if (std::filesystem::exists(cwd_env)) {
            CONFIG_DEBUG("ConfigService: Found .env at current working directory");
            return cwd_env;
        }
        
        // 2. Executable directory (for deployed apps)
        std::string exec_env = get_executable_directory() + "/.env";
        CONFIG_DEBUG_FMT("ConfigService: Checking executable dir: {}", exec_env);
        if (std::filesystem::exists(exec_env)) {
            CONFIG_DEBUG("ConfigService: Found .env at executable directory");
            return exec_env;
        }
        
        // 3. Parent directories up to 3 levels (for build directories)
        std::filesystem::path current = std::filesystem::current_path();
        for (int i = 0; i < 3; ++i) {
            std::string parent_env = current.string() + "/.env";
            CONFIG_DEBUG_FMT("ConfigService: Checking parent directory {}: {}", i+1, parent_env);
            if (std::filesystem::exists(parent_env)) {
                CONFIG_DEBUG_FMT("ConfigService: Found .env at parent directory {}", i+1);
                return parent_env;
            }
            current = current.parent_path();
            if (current == current.parent_path()) break; // Reached root
        }
        
        // 4. Try some common project root locations (for VS Code debugging)
        std::vector<std::string> common_locations = {
            "/Users/inz/src/rouen/.env",  // Hardcoded project root as fallback
            std::filesystem::current_path().parent_path().string() + "/.env",
        };
        
        for (const auto& location : common_locations) {
            CONFIG_DEBUG_FMT("ConfigService: Checking common location: {}", location);
            if (std::filesystem::exists(location)) {
                CONFIG_DEBUG_FMT("ConfigService: Found .env at common location: {}", location);
                return location;
            }
        }
        
        // 5. Default to current working directory (even if file doesn't exist)
        CONFIG_DEBUG_FMT("ConfigService: .env file not found, defaulting to: {}", cwd_env);
        return cwd_env;
    }

    // Private helper methods
    std::pair<std::string, std::string> ConfigService::parse_env_line(const std::string& line) {
        // Find the first = character
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            return {"", ""};  // Invalid line
        }
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace from key
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        
        // Trim whitespace from unquoted value
        if (value.length() < 2 || (value.front() != '"' && value.front() != '\'')) {
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            auto last_non_ws = value.find_last_not_of(" \t\r\n");
            if (last_non_ws != std::string::npos) {
                value.erase(last_non_ws + 1);
            } else {
                value.clear();
            }
        }
        
        // Handle quoted values
        if (value.length() >= 2) {
            if ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')) {
                value = value.substr(1, value.length() - 2);
            }
        }
        
        // Handle escape sequences in value
        std::string unescaped_value;
        for (size_t i = 0; i < value.length(); ++i) {
            if (value[i] == '\\' && i + 1 < value.length()) {
                switch (value[i + 1]) {
                    case 'n': unescaped_value += '\n'; ++i; break;
                    case 't': unescaped_value += '\t'; ++i; break;
                    case 'r': unescaped_value += '\r'; ++i; break;
                    case '\\': unescaped_value += '\\'; ++i; break;
                    case '"': unescaped_value += '"'; ++i; break;
                    case '\'': unescaped_value += '\''; ++i; break;
                    default: unescaped_value += value[i]; break;
                }
            } else {
                unescaped_value += value[i];
            }
        }
        
        return {key, unescaped_value};
    }

    std::string ConfigService::get_executable_directory() {
        // Use the centralized platform-specific executable directory function
        return rouen::platform::get_executable_directory().string();
    }

    std::string ConfigService::get_env_value_priority(const std::string& name) const {
        // First check .env file values (if loaded)
        if (env_file_loaded_) {
            auto env_it = env_file_values_.find(name);
            if (env_it != env_file_values_.end() && !env_it->second.empty()) {
                return env_it->second;
            }
        }
        
        // Fall back to environment variables
        return platform::get_env(name);
    }

} // namespace rouen::helpers
