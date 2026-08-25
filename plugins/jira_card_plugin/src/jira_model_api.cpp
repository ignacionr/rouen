#include "helpers/debug.hpp"
#include "helpers/fetch.hpp"
#include "jira_model.hpp"
#include <exception>
#include <future>
#include <glaze/core/opts.hpp>
#include <glaze/core/read.hpp>
#include <glaze/core/reflect.hpp>
#include <glaze/json/json_t.hpp>
#include <glaze/json/write.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace rouen::models {

std::string strip_trailing_slash(const std::string& url);
std::string base64_encode(const std::string& input);

std::string jira_model::make_request(const std::string& endpoint, 
                                   const std::string& method, 
                                   const std::string& payload) const {
    if (!connected_) {
        throw std::runtime_error("Not connected to JIRA");
    }
    
    std::string url = strip_trailing_slash(current_profile_.server_url) + "/" + endpoint;
    DB_INFO_FMT("JIRA API Request: {} {}", method.empty() ? "GET" : method, url);
    
    std::string const auth_string = current_profile_.username + ":" + current_profile_.api_token;
    std::string const base64_auth = base64_encode(auth_string);
    
    http::fetch fetcher;
    
    auto headers = [base64_auth](auto set_header) {
        set_header("Authorization: Basic " + base64_auth);
        set_header("Content-Type: application/json");
        set_header("Accept: application/json");
    };
    
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
        throw;
    }
    
    return response;
}

std::future<jira_search_result> jira_model::search_issues(const std::string& jql, int start_at, int max_results) {
    return std::async(std::launch::async, [this, jql, start_at, max_results]() {
        jira_search_result result;
        
        try {
            glz::json_t payload;
            payload["jql"] = jql;
            payload["startAt"] = start_at;
            payload["maxResults"] = max_results;
            
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
            
            std::string response = make_request("search", "POST", json_payload);
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
