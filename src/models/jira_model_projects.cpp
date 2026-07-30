#include "debug.hpp"
#include "jira_model.hpp"
#include <exception>
#include <format>
#include <future>
#include <glaze/json/json_t.hpp>
#include <glaze/json/read.hpp>
#include <string>
#include <vector>

namespace rouen::models {

// Get projects from JIRA
std::future<std::vector<jira_project>> jira_model::get_projects() {
    return std::async(std::launch::async, [this]() { // NOLINT(bugprone-exception-escape)
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
                    if (json.contains("description")) {
                        try {
                            project.description = json["description"].get<std::string>();
                        } catch (const std::exception& e) {
                            // Handle null or invalid description
                            (void)e;
                        }
                    }
                    
                    projects.push_back(project);
                }
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA projects: {}", e.what());
        } catch (...) {
            DB_ERROR_FMT("{}", "Unknown error getting JIRA projects");
        }
        
        return projects;
    });
}

// Get a specific project by key
std::future<jira_project> jira_model::get_project(const std::string& project_key) {
    return std::async(std::launch::async, [this, project_key]() { // NOLINT(bugprone-exception-escape)
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
                if (json.contains("description")) {
                    try {
                        project.description = json["description"].get<std::string>();
                    } catch (const std::exception& e) {
                        // Handle null or invalid description
                        (void)e;
                    }
                }
                
                // Process issue types if available
                if (json.contains("issueTypes")) {
                    for (const auto& type_json : json["issueTypes"].get<std::vector<glz::json_t>>()) {
                        jira_issue_type type;
                        type.id = type_json["id"].get<std::string>();
                        type.name = type_json["name"].get<std::string>();
                        
                        if (type_json.contains("description")) {
                            try {
                                type.description = type_json["description"].get<std::string>();
                            } catch (const std::exception& e) {
                                // Handle null or invalid description
                                (void)e;
                            }
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
        } catch (...) {
            DB_ERROR_FMT("{}", "Unknown error getting JIRA project");
        }
        
        return project;
    });
}

} // namespace rouen::models
