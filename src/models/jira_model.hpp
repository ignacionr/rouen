#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include "../helpers/debug.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/platform_utils.hpp"
#include "../helpers/glaze_include.hpp"

// Define JIRA-specific logging macros
#define JIRA_ERROR(message) LOG_COMPONENT("JIRA", LOG_LEVEL_ERROR, message)
#define JIRA_WARN(message) LOG_COMPONENT("JIRA", LOG_LEVEL_WARN, message)
#define JIRA_INFO(message) LOG_COMPONENT("JIRA", LOG_LEVEL_INFO, message)
#define JIRA_DEBUG(message) LOG_COMPONENT("JIRA", LOG_LEVEL_DEBUG, message)
#define JIRA_TRACE(message) LOG_COMPONENT("JIRA", LOG_LEVEL_TRACE, message)

// Format-enabled macros
#define JIRA_ERROR_FMT(fmt, ...) JIRA_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define JIRA_WARN_FMT(fmt, ...) JIRA_WARN(debug::format_log(fmt, __VA_ARGS__))
#define JIRA_INFO_FMT(fmt, ...) JIRA_INFO(debug::format_log(fmt, __VA_ARGS__))
#define JIRA_DEBUG_FMT(fmt, ...) JIRA_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define JIRA_TRACE_FMT(fmt, ...) JIRA_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::models {

// Structure to store JIRA connection profile
struct jira_connection_profile {
    std::string name;          // Profile name for display and identification
    std::string server_url;    // JIRA server URL
    std::string username;      // Username or email
    std::string api_token;     // API token for authentication
    bool is_cloud = true;      // Whether this is a cloud instance (vs. server)
    std::string organization;  // Organization identifier (optional)
    bool is_environment = false; // Whether this profile is from environment variables

    // For JSON serialization
    struct glaze {
        using T = jira_connection_profile;
        
        static constexpr auto value = glz::object(
            "name", &T::name,
            "server_url", &T::server_url,
            "username", &T::username,
            "api_token", &T::api_token,
            "is_cloud", &T::is_cloud,
            "organization", &T::organization
        );
    };
};

// JIRA user representation
struct jira_user {
    std::string account_id;
    std::string email;
    std::string display_name;
    std::string avatar_url;
    
    struct glaze {
        using T = jira_user;
        
        static constexpr auto value = glz::object(
            "accountId", &T::account_id,
            "emailAddress", &T::email,
            "displayName", &T::display_name,
            "avatarUrls", [](auto& self, auto& val) {
                // Handle nested avatar URLs if present
                if (val.contains("48x48")) {
                    self.avatar_url = val["48x48"].template get<std::string>();
                }
                return true;
            }
        );
    };
};

// JIRA issue type representation
struct jira_issue_type {
    std::string id;
    std::string name;
    std::string description;
    std::string icon_url;
    bool is_subtask = false;
    
    struct glaze {
        using T = jira_issue_type;
        
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "iconUrl", &T::icon_url,
            "subtask", &T::is_subtask
        );
    };
};

// JIRA status representation
struct jira_status {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::string color;
    
    struct glaze {
        using T = jira_status;
        
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "statusCategory", [](auto& self, auto& val) {
                if (val.contains("name")) {
                    self.category = val["name"].template get<std::string>();
                }
                if (val.contains("colorName")) {
                    self.color = val["colorName"].template get<std::string>();
                }
                return true;
            }
        );
    };
};

// JIRA project representation
struct jira_project {
    std::string id;
    std::string key;
    std::string name;
    std::string description;
    std::string lead;
    std::string url;
    std::string avatar_url;
    std::vector<jira_issue_type> issue_types;
    
    struct glaze {
        using T = jira_project;
        
        static constexpr auto value = glz::object(
            "id", &T::id,
            "key", &T::key,
            "name", &T::name,
            "description", &T::description,
            "lead", [](auto& self, auto& val) {
                if (val.contains("displayName")) {
                    self.lead = val["displayName"].template get<std::string>();
                }
                return true;
            },
            "self", &T::url,
            "avatarUrls", [](auto& self, auto& val) {
                if (val.contains("48x48")) {
                    self.avatar_url = val["48x48"].template get<std::string>();
                }
                return true;
            },
            "issueTypes", &T::issue_types
        );
    };
};

// Forward declarations for missing structures
struct jira_comment {
    std::string id;
    std::string body;
    std::string created;
    std::string updated;
    jira_user author;
};

struct jira_attachment {
    std::string id;
    std::string filename;
    std::string url;
    std::string content_type;
    long size = 0;
    std::string created;
    jira_user author;
};

// JIRA issue representation
struct jira_issue {
    std::string id;
    std::string key;
    std::string summary;
    std::string description;
    jira_issue_type issue_type;
    jira_status status;
    jira_user assignee;
    jira_user reporter;
    jira_project project;  // Add missing project field
    std::string created;
    std::string updated;
    std::vector<std::string> labels;
    std::vector<jira_comment> comments;  // Add missing comments field
    std::vector<jira_attachment> attachments;  // Add missing attachments field
    
    struct glaze {
        using T = jira_issue;
        
        static constexpr auto value = glz::object(
            "id", &T::id,
            "key", &T::key,
            "fields", [](auto& self, auto& val) {
                // Replace glz::is_input with direct property access
                if (auto it = val.find("summary"); it != val.end()) {
                    self.summary = it->second.template get<std::string>();
                }
                if (auto it = val.find("description"); it != val.end()) {
                    if (!it->second.is_null()) {
                        // Handle various description formats (plain text, Atlassian Document Format)
                        if (it->second.is_string()) {
                            self.description = it->second.template get<std::string>();
                        } else if (it->second.is_object()) {
                            // Try to extract content from Atlassian Document Format
                            self.description = "ADF document - view in browser";
                        }
                    }
                }
                if (auto it = val.find("issuetype"); it != val.end()) {
                    glz::read_json(self.issue_type, it->second);
                }
                if (auto it = val.find("status"); it != val.end()) {
                    glz::read_json(self.status, it->second);
                }
                if (auto it = val.find("assignee"); it != val.end()) {
                    if (!it->second.is_null()) {
                        glz::read_json(self.assignee, it->second);
                    }
                }
                if (auto it = val.find("reporter"); it != val.end()) {
                    if (!it->second.is_null()) {
                        glz::read_json(self.reporter, it->second);
                    }
                }
                if (auto it = val.find("created"); it != val.end()) {
                    self.created = it->second.template get<std::string>();
                }
                if (auto it = val.find("updated"); it != val.end()) {
                    self.updated = it->second.template get<std::string>();
                }
                if (auto it = val.find("labels"); it != val.end()) {
                    self.labels.clear();
                    for (const auto& label : it->second) {
                        self.labels.push_back(label.template get<std::string>());
                    }
                }
                return true;
            }
        );
    };
};

// Structure for creating new issues
struct jira_issue_create {
    std::string project_key;
    std::string issue_type;
    std::string summary;
    std::string description;
    std::string assignee_account_id;
    std::string priority_id;
    
    struct glaze {
        using T = jira_issue_create;
        
        static constexpr auto value = glz::object(
            "project_key", &T::project_key,
            "issue_type", &T::issue_type,
            "summary", &T::summary,
            "description", &T::description,
            "assignee_account_id", &T::assignee_account_id,
            "priority_id", &T::priority_id
        );
    };
};

// Add the missing jira_transition structure
struct jira_transition {
    std::string id;
    std::string name;
    jira_status to_status;
    
    struct glaze {
        using T = jira_transition;
        
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "to", &T::to_status
        );
    };
};

// Search result structure
struct jira_search_result {
    int start_at = 0;
    int max_results = 0;
    int total = 0;
    std::vector<jira_issue> issues;
    
    struct glaze {
        using T = jira_search_result;
        
        static constexpr auto value = glz::object(
            "startAt", &T::start_at,
            "maxResults", &T::max_results,
            "total", &T::total,
            "issues", &T::issues
        );
        
        // Configure options to ignore unknown keys
        static constexpr auto opt = glz::opts{
            .error_on_unknown_keys = false
        };
    };
};

// JIRA model class to manage API connections and data
class jira_model {
public:
    jira_model();
    ~jira_model();
    
    // Connection management
    void connect(const jira_connection_profile& profile);
    bool disconnect();
    bool is_connected() const;
    std::string get_server_url() const;
    std::string get_current_profile_name() const;
    
    // Project methods
    std::future<std::vector<jira_project>> get_projects();
    std::future<jira_project> get_project(const std::string& project_key);
    
    // Issue methods
    std::future<jira_issue> get_issue(const std::string& issue_key);
    std::future<std::vector<jira_issue>> get_issues(const std::string& project_key, int max_results = 50);
    std::future<jira_issue> create_issue(const jira_issue_create& issue_data);
    std::future<std::vector<jira_transition>> get_transitions(const std::string& issue_key);
    bool transition_issue(const std::string& issue_key, const std::string& transition_id);
    
    // Search methods
    std::future<jira_search_result> search_issues(const std::string& jql,
                                                 int start_at = 0,
                                                 int max_results = 50);
    
    // Connection profile management
    static std::vector<jira_connection_profile> load_profiles();
    static void save_profile(const jira_connection_profile& profile);
    static void save_profiles(const std::vector<jira_connection_profile>& profiles);  // Add missing method
    static void delete_profile(const std::string& profile_name);
    static std::vector<jira_connection_profile> detect_environment_profiles();
    static std::vector<jira_connection_profile> get_env_profiles();  // Add missing method
    
    // Issue comment methods
    std::future<bool> add_comment(const std::string& issue_key, const std::string& comment_text);  // Add missing method
    
    // Profile management
    jira_connection_profile get_current_profile() const;  // Add missing method
    
    // Helper method to get the profiles file path (public to allow external utilities to work with it)
    static std::filesystem::path get_profiles_file_path() {
        return get_profiles_path();
    }
    
    // Global profile state encapsulation to avoid non-const global variables
    static std::mutex& get_profiles_mutex();
    static bool& get_profiles_modified();
    static std::vector<jira_connection_profile>& get_environment_profiles_ref();
    static std::vector<jira_connection_profile>& get_saved_profiles_ref();
    
private:
    // Current connection state
    bool connected_ = false;
    jira_connection_profile current_profile_;
    
    // API request helpers
    std::string make_request(const std::string& endpoint, 
                            const std::string& method = "GET",
                            const std::string& payload = "") const;
    
    // Path to stored profiles
    static std::filesystem::path get_profiles_path();
};

} // namespace rouen::models
