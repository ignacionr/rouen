#include "jira_model.hpp"
#include "../helpers/glaze_include.hpp"

namespace rouen::models {

// External declarations from core
extern std::mutex profiles_mutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
extern bool profiles_modified; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
extern std::vector<jira_connection_profile> environment_profiles; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
extern std::vector<jira_connection_profile> saved_profiles; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
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
        // Validate profile data first
        if (profile.server_url.empty()) {
            throw std::runtime_error("Server URL is empty");
        }
        if (profile.username.empty()) {
            throw std::runtime_error("Username is empty");
        }
        if (profile.api_token.empty()) {
            throw std::runtime_error("API token is empty");
        }
        
        // Don't append /rest/api/X since it's already in the base URL from environment variables
        std::string url = strip_trailing_slash(profile.server_url) + "/serverInfo";
        
        // Log connection attempt details
        DB_INFO("JIRA Connection Attempt:");
        DB_INFO_FMT("  Profile: {}", profile.name);
        DB_INFO_FMT("  Server URL: {}", profile.server_url);
        DB_INFO_FMT("  Username: {}", profile.username);
        DB_INFO_FMT("  Full serverInfo URL: {}", url);
        DB_INFO_FMT("  Is Cloud: {}", profile.is_cloud ? "true" : "false");
        DB_INFO_FMT("  From Environment: {}", profile.is_environment ? "true" : "false");
        
        // Create authentication string for Basic Auth
        std::string auth_string = profile.username + ":" + profile.api_token;
        std::string base64_auth = base64_encode(auth_string);
        
        // Create HTTP client
        http::fetch fetcher;
        
        // Set up headers and authentication
        auto headers = [base64_auth](auto set_header) {
            set_header("Authorization: Basic " + base64_auth);
            set_header("Content-Type: application/json");
            set_header("Accept: application/json");
        };
        
        DB_INFO_FMT("Making HTTP request to: {}", url);
        
        // Make the request
        std::string response = fetcher(url, headers);
        
        DB_INFO_FMT("HTTP request completed. Response length: {} bytes", response.length());
        
        if (response.empty()) {
            throw std::runtime_error("Empty response from server");
        }
        
        // Log first 200 characters of response for debugging
        std::string response_preview = response.length() > 200 ? response.substr(0, 200) + "..." : response;
        DB_INFO_FMT("Response preview: {}", response_preview);
        
        // Parse JSON response
        auto json_result = glz::read_json<glz::json_t>(response);
        if (!json_result.has_value()) {
            throw std::runtime_error("Failed to parse JSON response. Response: " + response);
        }
        
        glz::json_t& json = json_result.value();
        
        // Check if response contains error information
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
        
        // Extract server info
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
        throw; // Re-throw to preserve the detailed error
    }
    
    return info;
}

// Connect to JIRA with the specified profile
void jira_model::connect(const jira_connection_profile& profile) {
    // Store connection details
    current_profile_ = profile;
    connected_ = false;
    
    DB_INFO_FMT("Attempting to connect to JIRA with profile: {}", profile.name);
    
    try {
        // Validate profile before attempting connection
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
        
        // Test connection by getting the server info
        auto server_info = get_server_info(profile);
        
        if (server_info.version.empty()) {
            throw std::runtime_error("Server info request succeeded but version is empty - this may indicate an authentication or URL issue");
        }
        
        connected_ = true;
        DB_INFO("Successfully connected to JIRA server:");
        DB_INFO_FMT("  Profile: {}", profile.name);
        DB_INFO_FMT("  Server: {}", profile.server_url);
        DB_INFO_FMT("  Version: {}", server_info.version);
        DB_INFO_FMT("  Title: {}", server_info.server_title);
        
        // Check if we need to save this profile
        if (!profile.is_environment && 
            std::find_if(saved_profiles.begin(), saved_profiles.end(),
                       [&profile](const auto& p) { return p.name == profile.name; }) == saved_profiles.end()) {
            // Add to saved profiles
            std::lock_guard<std::mutex> lock(profiles_mutex);
            saved_profiles.push_back(profile);
            profiles_modified = true;
            DB_INFO_FMT("Saved profile '{}' to disk", profile.name);
        }
        
    } catch (const std::exception& e) {
        connected_ = false;
        std::string error_msg = e.what();
        std::string detailed_error;
        
        // Check for specific SSL cipher errors and provide targeted guidance
        if (error_msg.find("Could not use specified SSL cipher") != std::string::npos || 
            error_msg.find("no ciphers available") != std::string::npos ||
            error_msg.find("cipher") != std::string::npos) {
            detailed_error = std::format(
                "SSL Cipher Error - Failed to connect to JIRA server.\n"
                "Profile: '{}'\n"
                "Server URL: '{}'\n"
                "Username: '{}'\n"
                "Error: {}\n"
                "\nSSL Cipher Troubleshooting:\n"
                "1. For Atlassian Cloud (*.atlassian.net), try setting:\n"
                "   export ROUEN_SSL_MODE=atlassian\n"
                "2. For maximum compatibility, try:\n"
                "   export ROUEN_SSL_MODE=compatible\n"
                "3. For corporate environments, try:\n"
                "   export ROUEN_SSL_MODE=relaxed\n"
                "4. Only for testing/debugging (insecure):\n"
                "   export ROUEN_SSL_MODE=insecure\n"
                "5. Current SSL mode can be checked in application logs\n"
                "6. Restart the application after changing SSL mode",
                profile.name, profile.server_url, profile.username, e.what()
            );
        } else {
            detailed_error = std::format(
                "Failed to connect to JIRA server.\n"
                "Profile: '{}'\n"
                "Server URL: '{}'\n"
                "Username: '{}'\n"
                "Error: {}\n"
                "\nTroubleshooting tips:\n"
                "1. Verify the server URL is correct and includes the API version (e.g., /rest/api/2 or /rest/api/3)\n"
                "2. Check that your username and API token are valid\n"
                "3. Ensure the JIRA server is accessible from your network\n"
                "4. For Atlassian Cloud, use your email as username\n"
                "5. For self-hosted JIRA, verify the API endpoint is available\n"
                "6. If encountering SSL errors, try: export ROUEN_SSL_MODE=atlassian",
                profile.name, profile.server_url, profile.username, e.what()
            );
        }
        
        DB_ERROR_FMT("{}", detailed_error);
        JIRA_ERROR_FMT("Connection failed: {}", detailed_error);
        
        // Throw the detailed error so the UI can display it
        throw std::runtime_error(detailed_error);
    }
}

// Implementation of disconnect method
bool jira_model::disconnect() {
    connected_ = false;
    current_profile_ = jira_connection_profile();
    JIRA_INFO("Disconnected from JIRA server");
    return true;
}

} // namespace rouen::models
