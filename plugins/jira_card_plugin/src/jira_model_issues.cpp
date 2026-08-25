#include "helpers/debug.hpp"
#include "jira_model.hpp"
#include <exception>
#include <format>
#include <future>
#include <glaze/json/json_t.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace rouen::models {

std::future<std::vector<jira_issue>> jira_model::get_issues(const std::string& project_key, int max_results) {
    return std::async(std::launch::async, [this, project_key, max_results]() {
        std::vector<jira_issue> issues;
        
        try {
            std::string jql = std::format("project = {} ORDER BY updated DESC", project_key);
            auto search_result = search_issues(jql, 0, max_results).get();
            
            if (search_result.issues.empty()) {
                JIRA_INFO_FMT("No issues found with basic query, trying with expanded query for project {}", project_key);
                jql = std::format(R"(project = {} AND status in (Open, "In Progress", Reopened, "To Do", Backlog, "Selected for Development", New, "In Review", Done, Closed) ORDER BY updated DESC)", project_key);
                search_result = search_issues(jql, 0, max_results).get();
            }
            
            issues = search_result.issues;
            JIRA_INFO_FMT("Retrieved {} issues for project {}", issues.size(), project_key);
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA issues: {}", e.what());
        } catch (...) {
            DB_ERROR_FMT("{}", "Unknown error getting JIRA issues");
        }
        
        return issues;
    });
}

std::future<jira_issue> jira_model::get_issue(const std::string& issue_key) {
    return std::async(std::launch::async, [this, issue_key]() {
        jira_issue issue;
        
        try {
            std::string response = make_request(std::format("issue/{}", issue_key));
            
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                issue.id = json["id"].get<std::string>();
                issue.key = json["key"].get<std::string>();
                
                auto& fields = json["fields"];
                issue.summary = fields["summary"].get<std::string>();
                
                if (fields.contains("description")) {
                    try {
                        issue.description = fields["description"].get<std::string>();
                    } catch (const std::exception&) {
                        issue.description = "ADF document - view in browser";
                    }
                }
                
                issue.created = fields["created"].get<std::string>();
                issue.updated = fields["updated"].get<std::string>();
                
                auto& status = fields["status"];
                issue.status.id = status["id"].get<std::string>();
                issue.status.name = status["name"].get<std::string>();
                issue.status.category = status["statusCategory"]["name"].get<std::string>();
                if (status["statusCategory"].contains("colorName")) {
                    issue.status.color = status["statusCategory"]["colorName"].get<std::string>();
                }
                
                auto& issue_type = fields["issuetype"];
                issue.issue_type.id = issue_type["id"].get<std::string>();
                issue.issue_type.name = issue_type["name"].get<std::string>();
                if (issue_type.contains("iconUrl")) {
                    issue.issue_type.icon_url = issue_type["iconUrl"].get<std::string>();
                }
                if (issue_type.contains("subtask")) {
                    issue.issue_type.is_subtask = issue_type["subtask"].get<bool>();
                }
                if (issue_type.contains("description")) {
                    try {
                        issue.issue_type.description = issue_type["description"].get<std::string>();
                    } catch (const std::exception& e) {
                        (void)e;
                    }
                }
                
                if (fields.contains("assignee")) {
                    try {
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
                    } catch (const std::exception& e) {
                        (void)e;
                    }
                }
                
                if (fields.contains("reporter")) {
                    try {
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
                    } catch (const std::exception& e) {
                        (void)e;
                    }
                }
                
                if (fields.contains("labels")) {
                    try {
                        for (const auto& label : fields["labels"].get<std::vector<glz::json_t>>()) {
                            issue.labels.push_back(label.get<std::string>());
                        }
                    } catch (const std::exception& e) {
                        (void)e;
                    }
                }
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Error getting JIRA issue: {}", e.what());
        } catch (...) {
            DB_ERROR_FMT("{}", "Unknown error getting JIRA issue");
        }
        
        return issue;
    });
}

std::future<jira_issue> jira_model::create_issue(const jira_issue_create& issue_data) {
    return std::async(std::launch::async, [this, issue_data]() {
        jira_issue created_issue;
        
        try {
            glz::json_t payload;
            glz::json_t fields;
            
            glz::json_t project_obj;
            project_obj["key"] = issue_data.project_key;
            fields["project"] = project_obj;
            
            fields["summary"] = issue_data.summary;
            fields["description"] = issue_data.description;
            
            glz::json_t issuetype_obj;
            issuetype_obj["name"] = issue_data.issue_type;
            fields["issuetype"] = issuetype_obj;
            
            if (!issue_data.assignee_account_id.empty()) {
                glz::json_t assignee_obj;
                assignee_obj["accountId"] = issue_data.assignee_account_id;
                fields["assignee"] = assignee_obj;
            }
            
            if (!issue_data.priority_id.empty()) {
                glz::json_t priority_obj;
                priority_obj["id"] = issue_data.priority_id;
                fields["priority"] = priority_obj;
            }
            
            payload["fields"] = fields;
            
            std::string json_payload;
            auto write_result = glz::write_json(payload, json_payload);
            if (write_result) {
                throw std::runtime_error("Failed to serialize issue creation payload");
            }
            
            std::string response = make_request("issue", "POST", json_payload);
            
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                created_issue.id = json["id"].get<std::string>();
                created_issue.key = json["key"].get<std::string>();
                
                if (!created_issue.key.empty()) {
                    created_issue = get_issue(created_issue.key).get();
                }
            }
        } catch (const std::exception& e) {
            JIRA_ERROR_FMT("Error creating JIRA issue: {}", e.what());
        } catch (...) {
            JIRA_ERROR_FMT("{}", "Unknown error creating JIRA issue");
        }
        
        return created_issue;
    });
}

std::future<std::vector<jira_transition>> jira_model::get_transitions(const std::string& issue_key) {
    return std::async(std::launch::async, [this, issue_key]() {
        std::vector<jira_transition> transitions;
        
        try {
            std::string response = make_request(std::format("issue/{}/transitions", issue_key));
            
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value()) {
                glz::json_t& json = json_result.value();
                
                for (const auto& transition_json : json["transitions"].get<std::vector<glz::json_t>>()) {
                    jira_transition transition;
                    transition.id = transition_json["id"].get<std::string>();
                    transition.name = transition_json["name"].get<std::string>();
                    
                    const auto& to = transition_json["to"];
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
        } catch (...) {
            JIRA_ERROR_FMT("{}", "Unknown error getting JIRA transitions");
        }
        
        return transitions;
    });
}

bool jira_model::transition_issue(const std::string& issue_key, const std::string& transition_id) {
    try {
        glz::json_t payload;
        glz::json_t transition_obj;
        transition_obj["id"] = transition_id;
        payload["transition"] = transition_obj;
        
        std::string json_payload;
        auto result = glz::write_json(payload, json_payload);
        if (result) {
            throw std::runtime_error("Failed to serialize transition payload");
        }
        
        make_request(std::format("issue/{}/transitions", issue_key), "POST", json_payload);
        
        return true;
    } catch (const std::exception& e) {
        JIRA_ERROR_FMT("Error transitioning JIRA issue: {}", e.what());
        return false;
    }
}

std::future<bool> jira_model::add_comment(const std::string& issue_key, const std::string& comment_text) {
    return std::async(std::launch::async, [this, issue_key, comment_text]() {
        try {
            glz::json_t payload;
            payload["body"] = comment_text;
            
            std::string json_payload;
            auto result = glz::write_json(payload, json_payload);
            if (result) {
                throw std::runtime_error("Failed to serialize comment payload");
            }
            
            make_request(std::format("issue/{}/comment", issue_key), "POST", json_payload);
            
            return true;
        } catch (const std::exception& e) {
            JIRA_ERROR_FMT("Error adding comment to JIRA issue: {}", e.what());
            return false;
        } catch (...) {
            JIRA_ERROR_FMT("{}", "Unknown error adding comment to JIRA issue");
            return false;
        }
    });
}

} // namespace rouen::models
