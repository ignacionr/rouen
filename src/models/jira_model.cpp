#include "jira_model.hpp"
#include <glaze/json.hpp>
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

namespace fs = std::filesystem;

namespace rouen::models {

// Helper struct for JIRA server information
struct jira_server_info {
    std::string version;
    std::string build_number;
    std::string base_url;
    std::string server_title;
};

// Comment structure declaration that's used in implementation but not in header
struct jira_comment {
    std::string id;
    std::string body;
    std::string created;
    std::string updated;
    jira_user author;
};

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
static std::string url_encode(const std::string& str);
static std::string base64_encode(const std::string& input);
static void load_profiles_from_env();
static bool save_profiles(const std::vector<jira_connection_profile>& profiles);

// Implementation of the get_profiles_path method defined in the header
std::filesystem::path jira_model::get_profiles_path() {
    // Get app data directory
    fs::path app_data_dir;
    
    #ifdef _WIN32
    app_data_dir = rouen::platform::get_env("APPDATA");
    app_data_dir /= "Rouen";
    #elif defined(__APPLE__)
    app_data_dir = rouen::platform::get_env("HOME");
    app_data_dir /= "Library/Application Support/Rouen";
    #else
    app_data_dir = rouen::platform::get_env("HOME");
    app_data_dir /= ".config/rouen";
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
    // Process environment variables to find JIRA profile groups
    environment_profiles.clear();
    
    // Look for profiles with specific organization prefixes in env vars
    const std::vector<std::string> env_prefixes = {"", "EYECU_", "VISUALBLASTERS_", "REXI_"};
    
    for (const auto& prefix : env_prefixes) {
        auto url_env = rouen::platform::get_env(prefix + "JIRA_URL");
        auto user_env = rouen::platform::get_env(prefix + "JIRA_USER");
        auto token_env = rouen::platform::get_env(prefix + "JIRA_TOKEN");
        
        if (!url_env.empty() && !user_env.empty() && !token_env.empty()) {
            jira_connection_profile profile;
            profile.name = prefix.empty() ? "Default" : prefix.substr(0, prefix.size() - 1);
            profile.server_url = url_env;
            profile.username = user_env;
            profile.api_token = token_env;
            profile.is_environment = true;
            
            environment_profiles.push_back(profile);
        }
    }
}

// Get server information (helper function) using environment URL directly
static jira_server_info get_server_info(const jira_connection_profile& profile) {
    jira_server_info info;
    
    try {
        // Don't append /rest/api/X since it's already in the base URL from environment variables
        std::string url = strip_trailing_slash(profile.server_url) + "/serverInfo";
        
        // Log the URL being requested (for debugging)
        DB_INFO_FMT("JIRA serverInfo Request: {}", url);
        
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
        
        // Make the request
        std::string response = fetcher(url, headers);
        
        // Parse JSON response
        auto json_result = glz::read_json<glz::json_t>(response);
        if (json_result.has_value()) {
            glz::json_t& json = json_result.value();
            
            // Extract server info
            info.version = json["version"].get<std::string>();
            info.build_number = json["buildNumber"].get<std::string>();
            info.base_url = json["baseUrl"].get<std::string>();
            info.server_title = json["serverTitle"].get<std::string>();
            
            DB_INFO("JIRA serverInfo request successful");
        }
    } catch (const std::exception& e) {
        DB_ERROR_FMT("JIRA serverInfo request failed: {}", e.what());
    }
    
    return info;
}

// Connect to JIRA with the specified profile
bool jira_model::connect(const jira_connection_profile& profile) {
    // Store connection details
    current_profile_ = profile;
    connected_ = false;
    
    try {
        // Test connection by getting the server info
        auto server_info = get_server_info(profile);
        if (!server_info.version.empty()) {
            connected_ = true;
            DB_INFO_FMT("Successfully connected to JIRA server: {}", profile.server_url);
            
            // Check if we need to save this profile
            if (!profile.is_environment && 
                std::find_if(saved_profiles.begin(), saved_profiles.end(),
                           [&profile](const auto& p) { return p.name == profile.name; }) == saved_profiles.end()) {
                // Add to saved profiles
                std::lock_guard<std::mutex> lock(profiles_mutex);
                saved_profiles.push_back(profile);
                profiles_modified = true;
            }
            
            return true;
        }
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Failed to connect to JIRA: {}", e.what());
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

// Utility method to encode URL parameters
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
static std::string url_encode(const std::string& str) {
    // Simple URL encoding
    std::string encoded;
    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }
    return encoded;
}
#pragma clang diagnostic pop

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

} // namespace rouen::models