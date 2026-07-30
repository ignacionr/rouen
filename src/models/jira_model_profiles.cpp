#include "debug.hpp"
#include "jira_model.hpp"
#include "../helpers/config_service.hpp"
#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <ios>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace rouen::models {

// Encapsulated state accessed via jira_model static getters

// Forward declarations of helper methods
static void load_profiles_from_env();
static bool save_profiles(const std::vector<jira_connection_profile>& profiles);

// Load any profiles defined in environment variables
static void load_profiles_from_env() {
    // Use centralized configuration service for environment variable access
    auto config_service = rouen::helpers::ConfigService::instance();
    
    // Process environment variables to find JIRA profile groups
    jira_model::get_environment_profiles_ref().clear();
    
    // Get discovered JIRA profiles from configuration service
    auto discovered_profiles = rouen::helpers::ConfigService::get_jira_profiles();
    
    for (const auto& profile_name : discovered_profiles) {
        // Get JIRA configuration for this profile
        auto url = config_service->get_jira_config(profile_name, "URL");
        auto username = config_service->get_jira_config(profile_name, "USERNAME");
        auto user = config_service->get_jira_config(profile_name, "USER");  // Alternative key
        auto token = config_service->get_jira_config(profile_name, "TOKEN");
        
        // Use USER if USERNAME is not found
        if (username.empty() && !user.empty()) {
            username = user;
        }
        
        if (!url.empty() && !username.empty() && !token.empty()) {
            jira_connection_profile profile;
            profile.name = profile_name;
            profile.server_url = url;
            profile.username = username;
            profile.api_token = token;
            profile.is_environment = true;
            profile.organization = profile_name;
            
            jira_model::get_environment_profiles_ref().push_back(profile);
            
            DB_INFO_FMT("Loaded JIRA profile '{}' from environment variables", profile_name);
        } else {
            DB_WARN_FMT("Incomplete JIRA configuration for profile '{}' - URL: {}, Username: {}, Token: {}", 
                       profile_name, 
                       url.empty() ? "missing" : "present",
                       username.empty() ? "missing" : "present", 
                       token.empty() ? "missing" : "present");
        }
    }
    
    // Also check for legacy environment variable patterns for backward compatibility
    const std::vector<std::string> legacy_prefixes = {"", "EYECU_", "VISUALBLASTERS_", "REXI_"};
    
    for (const auto& prefix : legacy_prefixes) {
        auto url_env = config_service->get_env(prefix + "JIRA_URL");
        auto user_env = config_service->get_env(prefix + "JIRA_USER");
        auto token_env = config_service->get_env(prefix + "JIRA_TOKEN");
        
        if (!url_env.empty() && !user_env.empty() && !token_env.empty()) {
            std::string legacy_profile_name = prefix.empty() ? "Default" : prefix.substr(0, prefix.size() - 1);
            
            // Check if we already have this profile from the discovery process
            bool already_exists = std::any_of(jira_model::get_environment_profiles_ref().begin(), jira_model::get_environment_profiles_ref().end(),
                [&legacy_profile_name](const auto& p) { return p.name == legacy_profile_name; });
            
            if (!already_exists) {
                jira_connection_profile profile;
                profile.name = legacy_profile_name;
                profile.server_url = url_env;
                profile.username = user_env;
                profile.api_token = token_env;
                profile.is_environment = true;
                profile.organization = legacy_profile_name;
                
                jira_model::get_environment_profiles_ref().push_back(profile);
                
                DB_INFO_FMT("Loaded legacy JIRA profile '{}' from environment variables", legacy_profile_name);
            }
        }
    }
    
    DB_INFO_FMT("Loaded {} JIRA profiles from environment variables", jira_model::get_environment_profiles_ref().size());
}

// Static method to load saved connection profiles
std::vector<jira_connection_profile> jira_model::load_profiles() {
    std::lock_guard<std::mutex> lock(jira_model::get_profiles_mutex());
    
    // If profiles already loaded, return them
    if (!jira_model::get_saved_profiles_ref().empty()) {
        return jira_model::get_saved_profiles_ref();
    }
    
    // Get path to saved profiles file
    fs::path profiles_path = jira_model::get_profiles_file_path();
    
    // Check if file exists
    if (fs::exists(profiles_path)) {
        try {
            // Read file content using a safer method to avoid GCC null pointer warnings
            std::ifstream file(profiles_path);
            if (!file) {
                DB_ERROR_FMT("Failed to open JIRA profiles file: {}", profiles_path.string());
                return jira_model::get_saved_profiles_ref();
            }
            
            file.seekg(0, std::ios::end);
            const auto file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            std::string json_str;
            if (file_size > 0) {
                json_str.resize(static_cast<size_t>(file_size));
                file.read(json_str.data(), file_size);
            }
            file.close();
            
            // Parse JSON
            auto json_result = glz::read_json<std::vector<jira_connection_profile>>(json_str);
            if (json_result.has_value()) {
                jira_model::get_saved_profiles_ref() = json_result.value();
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error loading JIRA profiles: {}", e.what());
        }
    }
    
    return jira_model::get_saved_profiles_ref();
}

// Static method to save a profile
void jira_model::save_profile(const jira_connection_profile& profile) {
    std::lock_guard<std::mutex> lock(jira_model::get_profiles_mutex());
    
    auto& saved = jira_model::get_saved_profiles_ref();
    // Check if profile already exists
    auto it = std::find_if(saved.begin(), saved.end(),
                         [&profile](const auto& p) { return p.name == profile.name; });
    
    if (it != saved.end()) {
        // Update existing profile
        *it = profile;
    } else {
        // Add new profile
        saved.push_back(profile);
    }
    
    // Save to disk
    save_profiles(saved);
}

// Static method to delete a profile
void jira_model::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> lock(jira_model::get_profiles_mutex());
    
    auto& saved = jira_model::get_saved_profiles_ref();
    // Remove profile if it exists
    auto it = std::remove_if(saved.begin(), saved.end(),
                           [&profile_name](const auto& p) { return p.name == profile_name; });
    
    if (it != saved.end()) {
        saved.erase(it, saved.end());
        
        // Save to disk
        save_profiles(saved);
    }
}

// Static method to detect environment profiles
std::vector<jira_connection_profile> jira_model::detect_environment_profiles() {
    // Load profiles from environment if needed
    if (jira_model::get_environment_profiles_ref().empty()) {
        load_profiles_from_env();
    }
    
    return jira_model::get_environment_profiles_ref();
}

// Static method to save profiles (wrapper for the static function)
void jira_model::save_profiles(const std::vector<jira_connection_profile>& profiles) {
    std::lock_guard<std::mutex> lock(jira_model::get_profiles_mutex());
    jira_model::get_saved_profiles_ref() = profiles;
    ::rouen::models::save_profiles(profiles);
}

// Static method to get environment profiles
std::vector<jira_connection_profile> jira_model::get_env_profiles() {
    std::lock_guard<std::mutex> lock(jira_model::get_profiles_mutex());
    return jira_model::get_environment_profiles_ref();
}

// Helper to save profiles to disk
static bool save_profiles(const std::vector<jira_connection_profile>& profiles) {
    // Get path from the class directly using the static method
    fs::path profiles_path = jira_model::get_profiles_file_path();
    
    try {
        // Ensure directory exists
        fs::create_directories(profiles_path.parent_path());
        
        // Filter out environment profiles before saving
        std::vector<jira_connection_profile> filtered_profiles;
        std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered_profiles),
                   [](const auto& profile) { return !profile.is_environment; });
        
        // Convert to JSON
        std::string json_str;
        auto result = glz::write_json(filtered_profiles, json_str);
        if (result) {
            throw std::runtime_error("Failed to serialize profiles to JSON");
        }
        
        // Write to file
        std::ofstream file(profiles_path);
        file << json_str;
        file.close();
        
        jira_model::get_profiles_modified() = false;
        return true;
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Error saving JIRA profiles: {}", e.what());
        return false;
    }
}

} // namespace rouen::models
