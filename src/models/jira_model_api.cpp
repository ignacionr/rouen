#include "jira_model.hpp"
#include "../helpers/glaze_include.hpp"

namespace rouen::models {

// External declarations from core
std::string strip_trailing_slash(const std::string& url);
std::string base64_encode(const std::string& input);

// Internal method to make JIRA API requests
std::string jira_model::make_request(const std::string& endpoint, 
                                   const std::string& method, 
                                   const std::string& payload) const {
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

// Search for issues using JQL
std::future<jira_search_result> jira_model::search_issues(const std::string& jql, int start_at, int max_results) {
    return std::async(std::launch::async, [this, jql, start_at, max_results]() { // NOLINT(bugprone-exception-escape)
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
            auto write_result = glz::write_json(payload, json_payload);
            if (write_result) {
                throw std::runtime_error("Failed to serialize search payload");
            }
            
            // Make API request
            std::string response = make_request("search", "POST", json_payload);
            
            // Parse JSON response
            auto result_error = glz::read<glz::opts{.error_on_unknown_keys = false}>(result, response);

            if (result_error) {
                std::cerr << "Failed to parse search result JSON: " << glz::format_error(result_error, response) << '\n';
                throw std::runtime_error("Failed to parse search result JSON");
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error searching JIRA issues: {}", e.what());
        } catch (...) {
            DB_ERROR_FMT("{}", "Unknown error searching JIRA issues");
        }
        
        return result;
    });
}

} // namespace rouen::models
