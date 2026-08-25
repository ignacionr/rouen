#include "helpers/debug.hpp"
#include "helpers/platform_utils.hpp"
#include "jira_model.hpp"
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

// Forward declarations of helper methods
static void load_profiles_from_env();
static bool save_profiles(const std::vector<jira_connection_profile>& profiles);

// Load any profiles defined in environment variables
static void load_profiles_from_env() {
    jira_model::get_environment_profiles_ref().clear();
    
    // Check primary environment variables
    auto url = rouen::platform::get_env("JIRA_URL");
    auto user = rouen::platform::get_env("JIRA_USER");
    if (user.empty()) user = rouen::platform::get_env("JIRA_USERNAME");
    if (user.empty()) user = rouen::platform::get_env("JIRA_EMAIL");
    auto token = rouen::platform::get_env("JIRA_TOKEN");
    if (token.empty()) token = rouen::platform::get_env("JIRA_API_TOKEN");

    if (!url.empty() && !user.empty() && !token.empty()) {
        jira_connection_profile profile;
        profile.name = "Default";
        profile.server_url = url;
        profile.username = user;
        profile.api_token = token;
        profile.is_environment = true;
        profile.organization = "Default";
        jira_model::get_environment_profiles_ref().push_back(profile);
        
        DB_INFO_FMT("Loaded primary JIRA profile from environment variables: {}", url);
    }
    
    // Check legacy prefixes
    const std::vector<std::string> legacy_prefixes = {"EYECU_", "VISUALBLASTERS_", "REXI_"};
    for (const auto& prefix : legacy_prefixes) {
        auto url_env = rouen::platform::get_env(prefix + "JIRA_URL");
        auto user_env = rouen::platform::get_env(prefix + "JIRA_USER");
        if (user_env.empty()) user_env = rouen::platform::get_env(prefix + "JIRA_USERNAME");
        auto token_env = rouen::platform::get_env(prefix + "JIRA_TOKEN");
        if (token_env.empty()) token_env = rouen::platform::get_env(prefix + "JIRA_API_TOKEN");
        
        if (!url_env.empty() && !user_env.empty() && !token_env.empty()) {
            std::string legacy_profile_name = prefix.substr(0, prefix.size() - 1);
            
            bool const already_exists = std::any_of(jira_model::get_environment_profiles_ref().begin(), jira_model::get_environment_profiles_ref().end(),
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
    std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
    
    if (!jira_model::get_saved_profiles_ref().empty()) {
        return jira_model::get_saved_profiles_ref();
    }
    
    fs::path const profiles_path = jira_model::get_profiles_file_path();
    
    if (fs::exists(profiles_path)) {
        try {
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
    std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
    
    auto& saved = jira_model::get_saved_profiles_ref();
    auto it = std::find_if(saved.begin(), saved.end(),
                         [&profile](const auto& p) { return p.name == profile.name; });
    
    if (it != saved.end()) {
        *it = profile;
    } else {
        saved.push_back(profile);
    }
    
    save_profiles(saved);
}

// Static method to delete a profile
void jira_model::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
    
    auto& saved = jira_model::get_saved_profiles_ref();
    auto it = std::remove_if(saved.begin(), saved.end(),
                           [&profile_name](const auto& p) { return p.name == profile_name; });
    
    if (it != saved.end()) {
        saved.erase(it, saved.end());
        save_profiles(saved);
    }
}

// Static method to detect environment profiles
std::vector<jira_connection_profile> jira_model::detect_environment_profiles() {
    if (jira_model::get_environment_profiles_ref().empty()) {
        load_profiles_from_env();
    }
    return jira_model::get_environment_profiles_ref();
}

// Static method to save profiles
void jira_model::save_profiles(const std::vector<jira_connection_profile>& profiles) {
    std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
    jira_model::get_saved_profiles_ref() = profiles;
    ::rouen::models::save_profiles(profiles);
}

// Static method to get environment profiles
std::vector<jira_connection_profile> jira_model::get_env_profiles() {
    std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
    return jira_model::get_environment_profiles_ref();
}

// Helper to save profiles to disk
static bool save_profiles(const std::vector<jira_connection_profile>& profiles) {
    fs::path const profiles_path = jira_model::get_profiles_file_path();
    
    try {
        fs::create_directories(profiles_path.parent_path());
        
        std::vector<jira_connection_profile> filtered_profiles;
        std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered_profiles),
                   [](const auto& profile) { return !profile.is_environment; });
        
        std::string json_str;
        auto result = glz::write_json(filtered_profiles, json_str);
        if (result) {
            throw std::runtime_error("Failed to serialize profiles to JSON");
        }
        
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
