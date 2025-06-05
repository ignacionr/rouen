#include "jira_model.hpp"
#include "../helpers/glaze_include.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <format>
#include <regex>
#include "../helpers/fetch.hpp"
#include "../helpers/api_keys.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/platform_utils.hpp"
#include "../helpers/config_service.hpp"

namespace fs = std::filesystem;

namespace rouen::models {

// Helper struct for JIRA server information
struct jira_server_info {
    std::string version;
    double build_number;
    std::string base_url;
    std::string server_title;
};

// jira_comment is now defined in the header file

// Priority structure declaration used in implementation but not in header
struct jira_priority {
    std::string id;
    std::string name;
    std::string icon_url;
};

// Static mutex for thread safety with profiles
static std::mutex profiles_mutex;
// Flag to track if profiles have been modified
static bool profiles_modified = false;
// Cached profiles loaded from environment variables
static std::vector<jira_connection_profile> environment_profiles;
// Cached profiles loaded from disk
static std::vector<jira_connection_profile> saved_profiles;

// Forward declarations of helper methods
static std::string strip_trailing_slash(const std::string& url);
static std::string base64_encode(const std::string& input);
static void load_profiles_from_env();
static bool save_profiles(const std::vector<jira_connection_profile>& profiles);

// Implementation of the get_profiles_path method defined in the header
std::filesystem::path jira_model::get_profiles_path() {
    // Get app data directory using centralized configuration service
    auto config_service = rouen::helpers::ConfigService::instance();
    fs::path app_data_dir;
    
    #ifdef _WIN32
    std::string appdata = config_service->get_env("APPDATA");
    if (!appdata.empty()) {
        app_data_dir = appdata;
        app_data_dir /= "Rouen";
    } else {
        // Fallback to USERPROFILE
        std::string userprofile = config_service->get_env("USERPROFILE");
        if (!userprofile.empty()) {
            app_data_dir = std::filesystem::path(userprofile) / "AppData" / "Roaming" / "Rouen";
        } else {
            app_data_dir = std::filesystem::path(".") / ".rouen";
        }
    }
    #elif defined(__APPLE__)
    std::string home_dir = config_service->get_env("HOME");
    if (!home_dir.empty()) {
        app_data_dir = std::filesystem::path(home_dir) / "Library" / "Application Support" / "Rouen";
    } else {
        app_data_dir = std::filesystem::path(".") / ".rouen";
    }
    #else
    std::string home_dir = config_service->get_env("HOME");
    if (!home_dir.empty()) {
        app_data_dir = std::filesystem::path(home_dir) / ".config" / "rouen";
    } else {
        app_data_dir = std::filesystem::path(".") / ".rouen";
    }
    #endif
    
    return app_data_dir / "jira_profiles.json";
}

jira_model::jira_model() {
    // Load profiles from environment variables
    load_profiles_from_env();
}

jira_model::~jira_model() {
    // Save profiles if modified
    if (profiles_modified) {
        save_profiles(saved_profiles);
    }
}

// Load any profiles defined in environment variables
static void load_profiles_from_env() {
    // Use centralized configuration service for environment variable access
    auto config_service = rouen::helpers::ConfigService::instance();
    
    // Process environment variables to find JIRA profile groups
    environment_profiles.clear();
    
    // Get discovered JIRA profiles from configuration service
    auto discovered_profiles = config_service->get_jira_profiles();
    
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
            
            environment_profiles.push_back(profile);
            
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
            bool already_exists = std::any_of(environment_profiles.begin(), environment_profiles.end(),
                [&legacy_profile_name](const auto& p) { return p.name == legacy_profile_name; });
            
            if (!already_exists) {
                jira_connection_profile profile;
                profile.name = legacy_profile_name;
                profile.server_url = url_env;
                profile.username = user_env;
                profile.api_token = token_env;
                profile.is_environment = true;
                profile.organization = legacy_profile_name;
                
                environment_profiles.push_back(profile);
                
                DB_INFO_FMT("Loaded legacy JIRA profile '{}' from environment variables", legacy_profile_name);
            }
        }
    }
    
    DB_INFO_FMT("Loaded {} JIRA profiles from environment variables", environment_profiles.size());
}

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
                error_msg += "Messages: " + json["errorMessages"].dump().value_or("[]");
            }
            if (json.contains("errors")) {
                error_msg += " Errors: " + json["errors"].dump().value_or("{}");
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
            throw std::runtime_error("Failed to extract server info from JSON: " + std::string(e.what()) + ". JSON: " + json.dump().value_or("{}"));
        }
        
    } catch (const std::exception& e) {
        DB_ERROR_FMT("JIRA serverInfo request failed - Profile: '{}', URL: '{}', Username: '{}', Error: {}", 
                     profile.name, profile.server_url, profile.username, e.what());
        throw; // Re-throw to preserve the detailed error
    }
    
    return info;
}

// Connect to JIRA with the specified profile
bool jira_model::connect(const jira_connection_profile& profile) {
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
        
        return true;
        
    } catch (const std::exception& e) {
        connected_ = false;
        std::string detailed_error = std::format(
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
            "5. For self-hosted JIRA, verify the API endpoint is available",
            profile.name, profile.server_url, profile.username, e.what()
        );
        
        DB_ERROR_FMT("{}", detailed_error);
        JIRA_ERROR_FMT("Connection failed: {}", detailed_error);
        
        // Throw the detailed error so the UI can display it
        throw std::runtime_error(detailed_error);
    }
    
    return false;
}

// Implementation of disconnect method
bool jira_model::disconnect() {
    connected_ = false;
    current_profile_ = jira_connection_profile();
    JIRA_INFO("Disconnected from JIRA server");
    return true;
}

// Check if currently connected
bool jira_model::is_connected() const {
    return connected_;
}

// Get the current server URL
std::string jira_model::get_server_url() const {
    return connected_ ? current_profile_.server_url : "";
}

// Get the current profile name
std::string jira_model::get_current_profile_name() const {
    return connected_ ? current_profile_.name : "";
}

// Get projects from JIRA
std::future<std::vector<jira_project>> jira_model::get_projects() {
    return std::async(std::launch::async, [this]() {
        std::vector<jira_project> projects;
        
        try {
            // Make API request
            std::string response = make_request("project");
            
            // Parse JSON response
            auto json_result = glz::read_json<std::vector<glz::json_t>>(response);
            if (json_result.has_value()) {
                auto& json_array = json_result.value();
                
                for (const auto& json : json_array) {
                    jira_project project;
                    project.id = json["id"].get<std::string>();
                    project.key = json["key"].get<std::string>();
                    project.name = json["name"].get<std::string>();
                    
                    // Description might be null
                    if (json.contains("description") && !json["description"].is_null()) {
                        project.description = json["description"].get<std::string>();
                    }
                    
                    projects.push_back(project);
                }
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA projects: {}", e.what());
        }
        
        return projects;
    });
}

// Get a specific project by key
std::future<jira_project> jira_model::get_project(const std::string& project_key) {
    return std::async(std::launch::async, [this, project_key]() {
        jira_project project;
        
        try {
            // Make API request
            std::string response = make_request(std::format("project/{}", project_key));
            
            // Parse JSON response
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                project.id = json["id"].get<std::string>();
                project.key = json["key"].get<std::string>();
                project.name = json["name"].get<std::string>();
                
                // Description might be null
                if (json.contains("description") && !json["description"].is_null()) {
                    project.description = json["description"].get<std::string>();
                }
                
                // Process issue types if available
                if (json.contains("issueTypes")) {
                    for (const auto& type_json : json["issueTypes"].get<std::vector<glz::json_t>>()) {
                        jira_issue_type type;
                        type.id = type_json["id"].get<std::string>();
                        type.name = type_json["name"].get<std::string>();
                        
                        if (type_json.contains("description") && !type_json["description"].is_null()) {
                            type.description = type_json["description"].get<std::string>();
                        }
                        
                        if (type_json.contains("iconUrl")) {
                            type.icon_url = type_json["iconUrl"].get<std::string>();
                        }
                        
                        if (type_json.contains("subtask")) {
                            type.is_subtask = type_json["subtask"].get<bool>();
                        }
                        
                        project.issue_types.push_back(type);
                    }
                }
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA project: {}", e.what());
        }
        
        return project;
    });
}

// Get issues from a project
std::future<std::vector<jira_issue>> jira_model::get_issues(const std::string& project_key, int max_results) {
    return std::async(std::launch::async, [this, project_key, max_results]() {
        std::vector<jira_issue> issues;
        
        try {
            // Construct JQL query that includes all issues including backlog items
            std::string jql = std::format("project = {} ORDER BY updated DESC", project_key);
            
            // Use search API with JQL
            auto search_result = search_issues(jql, 0, max_results).get();
            
            // If no issues found, try with a more explicit query that includes all statuses
            if (search_result.issues.empty()) {
                JIRA_INFO_FMT("No issues found with basic query, trying with expanded query for project {}", project_key);
                jql = std::format("project = {} AND status in (Open, \"In Progress\", Reopened, \"To Do\", Backlog, \"Selected for Development\", New, \"In Review\", Done, Closed) ORDER BY updated DESC", project_key);
                search_result = search_issues(jql, 0, max_results).get();
            }
            
            issues = search_result.issues;
            JIRA_INFO_FMT("Retrieved {} issues for project {}", issues.size(), project_key);
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA issues: {}", e.what());
        }
        
        return issues;
    });
}

// Get details for a specific issue
std::future<jira_issue> jira_model::get_issue(const std::string& issue_key) {
    return std::async(std::launch::async, [this, issue_key]() {
        jira_issue issue;
        
        try {
            // Make API request
            std::string response = make_request(std::format("issue/{}", issue_key));
            
            // Parse JSON response
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                // Extract issue details
                issue.id = json["id"].get<std::string>();
                issue.key = json["key"].get<std::string>();
                
                auto& fields = json["fields"];
                
                // Extract basic fields
                issue.summary = fields["summary"].get<std::string>();
                
                // Description might be null
                if (fields.contains("description") && !fields["description"].is_null()) {
                    if (fields["description"].is_string()) {
                        issue.description = fields["description"].get<std::string>();
                    } else {
                        // Handle Atlassian Document Format
                        issue.description = "ADF document - view in browser";
                    }
                }
                
                // Created and updated dates
                issue.created = fields["created"].get<std::string>();
                issue.updated = fields["updated"].get<std::string>();
                
                // Status
                auto& status = fields["status"];
                issue.status.id = status["id"].get<std::string>();
                issue.status.name = status["name"].get<std::string>();
                issue.status.category = status["statusCategory"]["name"].get<std::string>();
                if (status["statusCategory"].contains("colorName")) {
                    issue.status.color = status["statusCategory"]["colorName"].get<std::string>();
                }
                
                // Issue type
                auto& issue_type = fields["issuetype"];
                issue.issue_type.id = issue_type["id"].get<std::string>();
                issue.issue_type.name = issue_type["name"].get<std::string>();
                if (issue_type.contains("iconUrl")) {
                    issue.issue_type.icon_url = issue_type["iconUrl"].get<std::string>();
                }
                if (issue_type.contains("subtask")) {
                    issue.issue_type.is_subtask = issue_type["subtask"].get<bool>();
                }
                if (issue_type.contains("description") && !issue_type["description"].is_null()) {
                    issue.issue_type.description = issue_type["description"].get<std::string>();
                }
                
                // Assignee (might be null)
                if (fields.contains("assignee") && !fields["assignee"].is_null()) {
                    auto& assignee = fields["assignee"];
                    issue.assignee.account_id = assignee["accountId"].get<std::string>();
                    issue.assignee.display_name = assignee["displayName"].get<std::string>();
                    if (assignee.contains("emailAddress")) {
                        issue.assignee.email = assignee["emailAddress"].get<std::string>();
                    }
                    if (assignee.contains("avatarUrls") && 
                        assignee["avatarUrls"].contains("48x48")) {
                        issue.assignee.avatar_url = assignee["avatarUrls"]["48x48"].get<std::string>();
                    }
                }
                
                // Reporter (might be null)
                if (fields.contains("reporter") && !fields["reporter"].is_null()) {
                    auto& reporter = fields["reporter"];
                    issue.reporter.account_id = reporter["accountId"].get<std::string>();
                    issue.reporter.display_name = reporter["displayName"].get<std::string>();
                    if (reporter.contains("emailAddress")) {
                        issue.reporter.email = reporter["emailAddress"].get<std::string>();
                    }
                    if (reporter.contains("avatarUrls") && 
                        reporter["avatarUrls"].contains("48x48")) {
                        issue.reporter.avatar_url = reporter["avatarUrls"]["48x48"].get<std::string>();
                    }
                }
                
                // Labels
                if (fields.contains("labels") && !fields["labels"].is_null()) {
                    for (const auto& label : fields["labels"].get<std::vector<glz::json_t>>()) {
                        issue.labels.push_back(label.get<std::string>());
                    }
                }
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA issue: {}", e.what());
        }
        
        return issue;
    });
}

// Create a new JIRA issue
std::future<jira_issue> jira_model::create_issue(const jira_issue_create& issue_data) {
    return std::async(std::launch::async, [this, issue_data]() {
        jira_issue created_issue;
        
        try {
            // Construct request payload
            glz::json_t payload;
            glz::json_t fields;
            
            // Set up fields using proper JSON object construction
            glz::json_t project_obj;
            project_obj["key"] = issue_data.project_key;
            fields["project"] = project_obj;
            
            fields["summary"] = issue_data.summary;
            fields["description"] = issue_data.description;
            
            glz::json_t issuetype_obj;
            issuetype_obj["name"] = issue_data.issue_type;
            fields["issuetype"] = issuetype_obj;
            
            // Set assignee if provided
            if (!issue_data.assignee_account_id.empty()) {
                glz::json_t assignee_obj;
                assignee_obj["accountId"] = issue_data.assignee_account_id;
                fields["assignee"] = assignee_obj;
            }
            
            // Set priority if provided
            if (!issue_data.priority_id.empty()) {
                glz::json_t priority_obj;
                priority_obj["id"] = issue_data.priority_id;
                fields["priority"] = priority_obj;
            }
            
            payload["fields"] = fields;
            
            std::string json_payload;
            auto result = glz::write_json(payload, json_payload);
            if (!result) {
                throw std::runtime_error("Failed to serialize JSON payload");
            }
            
            // Make API request
            std::string response = make_request("issue", "POST", json_payload);
            
            // Parse JSON response
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                // Extract the created issue key
                created_issue.id = json["id"].get<std::string>();
                created_issue.key = json["key"].get<std::string>();
                
                // Get full issue details
                if (!created_issue.key.empty()) {
                    created_issue = get_issue(created_issue.key).get();
                }
            }
        } catch (const std::exception& e) {
            JIRA_ERROR_FMT("Error creating JIRA issue: {}", e.what());
        }
        
        return created_issue;
    });
}

// Get available transitions for an issue
std::future<std::vector<jira_transition>> jira_model::get_transitions(const std::string& issue_key) {
    return std::async(std::launch::async, [this, issue_key]() {
        std::vector<jira_transition> transitions;
        
        try {
            // Make API request
            std::string response = make_request(std::format("issue/{}/transitions", issue_key));
            
            // Parse JSON response
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                // Extract transitions
                for (const auto& transition_json : json["transitions"].get<std::vector<glz::json_t>>()) {
                    jira_transition transition;
                    transition.id = transition_json["id"].get<std::string>();
                    transition.name = transition_json["name"].get<std::string>();
                    
                    // Extract to status
                    auto& to = transition_json["to"];
                    transition.to_status.id = to["id"].get<std::string>();
                    transition.to_status.name = to["name"].get<std::string>();
                    if (to.contains("statusCategory")) {
                        transition.to_status.category = to["statusCategory"]["name"].get<std::string>();
                        if (to["statusCategory"].contains("colorName")) {
                            transition.to_status.color = to["statusCategory"]["colorName"].get<std::string>();
                        }
                    }
                    
                    transitions.push_back(transition);
                }
            }
        } catch (const std::exception& e) {
            JIRA_ERROR_FMT("Error getting JIRA transitions: {}", e.what());
        }
        
        return transitions;
    });
}

// Transition an issue to a new status
bool jira_model::transition_issue(const std::string& issue_key, const std::string& transition_id) {
    try {
        // Construct request payload
        glz::json_t payload;
        glz::json_t transition_obj;
        transition_obj["id"] = transition_id;
        payload["transition"] = transition_obj;
        
        std::string json_payload;
        auto result = glz::write_json(payload, json_payload);
        if (!result) {
            throw std::runtime_error("Failed to serialize JSON payload");
        }
        
        // Make API request
        make_request(std::format("issue/{}/transitions", issue_key), "POST", json_payload);
        
        return true;
    } catch (const std::exception& e) {
        JIRA_ERROR_FMT("Error transitioning JIRA issue: {}", e.what());
        return false;
    }
}

// Search for issues using JQL
std::future<jira_search_result> jira_model::search_issues(const std::string& jql, int start_at, int max_results) {
    return std::async(std::launch::async, [this, jql, start_at, max_results]() {
        jira_search_result result;
        
        try {
            // Construct request payload
            glz::json_t payload;
            payload["jql"] = jql;
            payload["startAt"] = start_at;
            payload["maxResults"] = max_results;
            
            // Create array of fields
            glz::json_t::array_t fields_array;
            fields_array.push_back("summary");
            fields_array.push_back("description");
            fields_array.push_back("status");
            fields_array.push_back("assignee");
            fields_array.push_back("reporter");
            fields_array.push_back("issuetype");
            fields_array.push_back("created");
            fields_array.push_back("updated");
            fields_array.push_back("labels");
            
            payload["fields"] = fields_array;
            
            std::string json_payload;
            auto write_error = glz::write_json(payload, json_payload);
            if (write_error) {
                throw std::runtime_error(std::format("Failed to serialize JSON payload: {}", glz::format_error(write_error)));
            }
            
            // Make API request
            std::string response = make_request("search", "POST", json_payload);
            
            // Parse JSON response
            auto result_error = glz::read<glz::opts{.error_on_unknown_keys = false}>(result, response);

            if (result_error) {
                std::cerr << "Failed to parse search result JSON: " << glz::format_error(result_error) << std::endl;
                throw std::runtime_error("Failed to parse search result JSON");
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error searching JIRA issues: {}", e.what());
        }
        
        return result;
    });
}

// Static method to load saved connection profiles
std::vector<jira_connection_profile> jira_model::load_profiles() {
    std::lock_guard<std::mutex> lock(profiles_mutex);
    
    // If profiles already loaded, return them
    if (!saved_profiles.empty()) {
        return saved_profiles;
    }
    
    // Get path to saved profiles file
    fs::path profiles_path = jira_model::get_profiles_path();
    
    // Check if file exists
    if (fs::exists(profiles_path)) {
        try {
            // Read file content
            std::ifstream file(profiles_path);
            std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            
            // Parse JSON
            auto json_result = glz::read_json<std::vector<jira_connection_profile>>(json_str);
            if (json_result.has_value()) {
                saved_profiles = json_result.value();
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error loading JIRA profiles: {}", e.what());
        }
    }
    
    return saved_profiles;
}

// Static method to save a profile
void jira_model::save_profile(const jira_connection_profile& profile) {
    std::lock_guard<std::mutex> lock(profiles_mutex);
    
    // Check if profile already exists
    auto it = std::find_if(saved_profiles.begin(), saved_profiles.end(),
                         [&profile](const auto& p) { return p.name == profile.name; });
    
    if (it != saved_profiles.end()) {
        // Update existing profile
        *it = profile;
    } else {
        // Add new profile
        saved_profiles.push_back(profile);
    }
    
    // Save to disk
    save_profiles(saved_profiles);
}

// Static method to delete a profile
void jira_model::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> lock(profiles_mutex);
    
    // Remove profile if it exists
    auto it = std::remove_if(saved_profiles.begin(), saved_profiles.end(),
                           [&profile_name](const auto& p) { return p.name == profile_name; });
    
    if (it != saved_profiles.end()) {
        saved_profiles.erase(it, saved_profiles.end());
        
        // Save to disk
        save_profiles(saved_profiles);
    }
}

// Static method to detect environment profiles
std::vector<jira_connection_profile> jira_model::detect_environment_profiles() {
    // Load profiles from environment if needed
    if (environment_profiles.empty()) {
        load_profiles_from_env();
    }
    
    return environment_profiles;
}

// Internal method to make JIRA API requests
std::string jira_model::make_request(const std::string& endpoint, 
                                   const std::string& method, 
                                   const std::string& payload) {
    if (!connected_) {
        throw std::runtime_error("Not connected to JIRA");
    }
    
    // Don't append /rest/api/X since it's already in the base URL from environment variables
    std::string url = strip_trailing_slash(current_profile_.server_url) + "/" + endpoint;
    
    // Log the URL being requested (for debugging)
    DB_INFO_FMT("JIRA API Request: {} {}", method.empty() ? "GET" : method, url);
    
    // Create authentication string for Basic Auth
    std::string auth_string = current_profile_.username + ":" + current_profile_.api_token;
    std::string base64_auth = base64_encode(auth_string);
    
    // Create HTTP client
    http::fetch fetcher;
    
    // Set up headers and authentication
    auto headers = [base64_auth](auto set_header) {
        set_header("Authorization: Basic " + base64_auth);
        set_header("Content-Type: application/json");
        set_header("Accept: application/json");
    };
    
    // Make the request
    std::string response;
    try {
        if (method == "POST") {
            response = fetcher.post(url, payload, headers);
        } else {
            response = fetcher(url, headers);
        }
        DB_INFO_FMT("JIRA API request successful to {}", url);
    } catch (const std::exception& e) {
        JIRA_ERROR_FMT("JIRA API {} request to '{}' failed: {}", 
                      method.empty() ? "GET" : method, url, e.what());
        throw; // Re-throw to allow the caller to handle it
    }
    
    return response;
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
        if (!result) {
            throw std::runtime_error("Failed to serialize JSON profiles");
        }
        
        // Write to file
        std::ofstream file(profiles_path);
        file << json_str;
        file.close();
        
        profiles_modified = false;
        return true;
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Error saving JIRA profiles: {}", e.what());
        return false;
    }
}

// Utility method to strip trailing slash from URL
static std::string strip_trailing_slash(const std::string& url) {
    if (!url.empty() && url.back() == '/') {
        return url.substr(0, url.size() - 1);
    }
    return url;
}

// Base64 encoding utility
static std::string base64_encode(const std::string& input) {
    // Implementation of base64 encoding
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string encoded;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    for (char ch : input) {
        char_array_3[i++] = static_cast<unsigned char>(ch);
        if (i == 3) {
            char_array_4[0] = static_cast<unsigned char>((char_array_3[0] & 0xfc) >> 2);
            char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
            char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));
            char_array_4[3] = static_cast<unsigned char>(char_array_3[2] & 0x3f);
            
            for(i = 0; i < 4; i++) {
                encoded += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i > 0) {
        for(j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = static_cast<unsigned char>((char_array_3[0] & 0xfc) >> 2);
        char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
        char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));
        
        for (j = 0; j < i + 1; j++) {
            encoded += base64_chars[char_array_4[j]];
        }
        
        while((i++ < 3)) {
            encoded += '=';
        }
    }
    
    return encoded;
}

// Static method to save profiles (wrapper for the static function)
void jira_model::save_profiles(const std::vector<jira_connection_profile>& profiles) {
    std::lock_guard<std::mutex> lock(profiles_mutex);
    saved_profiles = profiles;
    ::rouen::models::save_profiles(profiles);
}

// Static method to get environment profiles
std::vector<jira_connection_profile> jira_model::get_env_profiles() {
    std::lock_guard<std::mutex> lock(profiles_mutex);
    return environment_profiles;
}

// Get current profile
jira_connection_profile jira_model::get_current_profile() const {
    return current_profile_;
}

// Add comment to an issue
std::future<bool> jira_model::add_comment(const std::string& issue_key, const std::string& comment_text) {
    return std::async(std::launch::async, [this, issue_key, comment_text]() {
        try {
            // Construct request payload
            glz::json_t payload;
            payload["body"] = comment_text;
            
            std::string json_payload;
            auto result = glz::write_json(payload, json_payload);
            if (!result) {
                throw std::runtime_error("Failed to serialize JSON payload");
            }
            
            // Make API request
            make_request(std::format("issue/{}/comment", issue_key), "POST", json_payload);
            
            return true;
        } catch (const std::exception& e) {
            JIRA_ERROR_FMT("Error adding comment to JIRA issue: {}", e.what());
            return false;
        }
    });
}

} // namespace rouen::models