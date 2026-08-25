#include "helpers/debug.hpp"
#include "helpers/fetch.hpp"
#include "jira_model.hpp"
#include <algorithm>
#include <exception>
#include <format>
#include <glaze/json/json_t.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <mutex>
#include <stdexcept>
#include <string>

namespace rouen::models {

// Encapsulated state accessed via jira_model static getters
std::string strip_trailing_slash(const std::string& url);
std::string base64_encode(const std::string& input);

// Helper struct for JIRA server information (forward declaration in core)
struct jira_server_info {
    std::string version;
    double build_number;
    std::string base_url;
    std::string server_title;
};

// Get server information (helper function) using environment URL directly
static jira_server_info get_server_info(const jira_connection_profile& profile) {
    jira_server_info info;
    
    try {
        if (profile.server_url.empty()) {
            throw std::runtime_error("Server URL is empty");
        }
        if (profile.username.empty()) {
            throw std::runtime_error("Username is empty");
        }
        if (profile.api_token.empty()) {
            throw std::runtime_error("API token is empty");
        }
        
        std::string url = strip_trailing_slash(profile.server_url) + "/serverInfo";
        
        DB_INFO("JIRA Connection Attempt:");
        DB_INFO_FMT("  Profile: {}", profile.name);
        DB_INFO_FMT("  Server URL: {}", profile.server_url);
        DB_INFO_FMT("  Username: {}", profile.username);
        DB_INFO_FMT("  Full serverInfo URL: {}", url);
        DB_INFO_FMT("  Is Cloud: {}", profile.is_cloud ? "true" : "false");
        DB_INFO_FMT("  From Environment: {}", profile.is_environment ? "true" : "false");
        
        std::string const auth_string = profile.username + ":" + profile.api_token;
        std::string const base64_auth = base64_encode(auth_string);
        
        http::fetch fetcher;
        
        auto headers = [base64_auth](auto set_header) {
            set_header("Authorization: Basic " + base64_auth);
            set_header("Content-Type: application/json");
            set_header("Accept: application/json");
        };
        
        DB_INFO_FMT("Making HTTP request to: {}", url);
        
        std::string response = fetcher(url, headers);
        
        DB_INFO_FMT("HTTP request completed. Response length: {} bytes", response.length());
        
        if (response.empty()) {
            throw std::runtime_error("Empty response from server");
        }
        
        std::string response_preview = response.length() > 200 ? response.substr(0, 200) + "..." : response;
        DB_INFO_FMT("Response preview: {}", response_preview);
        
        auto json_result = glz::read_json<glz::json_t>(response);
        if (!json_result.has_value()) {
            throw std::runtime_error("Failed to parse JSON response. Response: " + response);
        }
        
        glz::json_t& json = json_result.value();
        
        if (json.contains("errorMessages") || json.contains("errors")) {
            std::string error_msg = "JIRA API returned error: ";
            if (json.contains("errorMessages")) {
                std::string error_messages_str;
                auto result = glz::write_json(json["errorMessages"], error_messages_str);
                if (!result) {
                    error_msg += "Messages: " + error_messages_str;
                }
            }
            if (json.contains("errors")) {
                std::string errors_str;
                auto result = glz::write_json(json["errors"], errors_str);
                if (!result) {
                    error_msg += " Errors: " + errors_str;
                }
            }
            throw std::runtime_error(error_msg);
        }
        
        try {
            info.version = json["version"].get<std::string>();
            info.build_number = json["buildNumber"].get<double>();
            info.base_url = json["baseUrl"].get<std::string>();
            info.server_title = json["serverTitle"].get<std::string>();
            
            DB_INFO("JIRA serverInfo parsed successfully:");
            DB_INFO_FMT("  Version: {}", info.version);
            DB_INFO_FMT("  Build: {}", info.build_number);
            DB_INFO_FMT("  Base URL: {}", info.base_url);
            DB_INFO_FMT("  Title: {}", info.server_title);
        } catch (const std::exception& e) {
            std::string json_str;
            auto result = glz::write_json(json, json_str);
            if (result) {
                json_str = "<error serializing JSON>";
            }
            throw std::runtime_error("Failed to extract server info from JSON: " + std::string(e.what()) + ". JSON: " + json_str);
        }
        
    } catch (const std::exception& e) {
        DB_ERROR_FMT("JIRA serverInfo request failed - Profile: '{}', URL: '{}', Username: '{}', Error: {}", 
                     profile.name, profile.server_url, profile.username, e.what());
        throw;
    }
    
    return info;
}

void jira_model::connect(const jira_connection_profile& profile) {
    current_profile_ = profile;
    connected_ = false;
    
    DB_INFO_FMT("Attempting to connect to JIRA with profile: {}", profile.name);
    
    try {
        if (profile.name.empty()) {
            throw std::runtime_error("Profile name is empty");
        }
        if (profile.server_url.empty()) {
            throw std::runtime_error("Server URL is empty for profile '" + profile.name + "'");
        }
        if (profile.username.empty()) {
            throw std::runtime_error("Username is empty for profile '" + profile.name + "'");
        }
        if (profile.api_token.empty()) {
            throw std::runtime_error("API token is empty for profile '" + profile.name + "'");
        }
        
        auto server_info = get_server_info(profile);
        
        if (server_info.version.empty()) {
            throw std::runtime_error("Server info request succeeded but version is empty");
        }
        
        connected_ = true;
        DB_INFO("Successfully connected to JIRA server:");
        DB_INFO_FMT("  Profile: {}", profile.name);
        DB_INFO_FMT("  Server: {}", profile.server_url);
        DB_INFO_FMT("  Version: {}", server_info.version);
        DB_INFO_FMT("  Title: {}", server_info.server_title);
        
        if (!profile.is_environment) {
            auto& saved = jira_model::get_saved_profiles_ref();
            if (std::find_if(saved.begin(), saved.end(),
                           [&profile](const auto& p) { return p.name == profile.name; }) == saved.end()) {
                std::lock_guard<std::mutex> const lock(jira_model::get_profiles_mutex());
                saved.push_back(profile);
                jira_model::get_profiles_modified() = true;
                DB_INFO_FMT("Saved profile '{}' to disk", profile.name);
            }
        }
        
    } catch (const std::exception& e) {
        connected_ = false;
        std::string const error_msg = e.what();
        std::string detailed_error = std::format(
            "Failed to connect to JIRA server.\nProfile: '{}'\nServer URL: '{}'\nUsername: '{}'\nError: {}\n",
            profile.name, profile.server_url, profile.username, error_msg
        );
        
        DB_ERROR_FMT("{}", detailed_error);
        JIRA_ERROR_FMT("Connection failed: {}", detailed_error);
        throw std::runtime_error(detailed_error);
    }
}

bool jira_model::disconnect() {
    connected_ = false;
    current_profile_ = jira_connection_profile();
    JIRA_INFO("Disconnected from JIRA server");
    return true;
}

} // namespace rouen::models
