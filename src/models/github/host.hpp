#pragma once

#include <chrono>
#include <format>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/fetch.hpp"
#include "login_host.hpp"
#include "../../registrar.hpp"

namespace rouen::models::github {
    struct host {
        // Renamed from login_host to set_login_host to avoid conflict with type name
        void set_login_host(std::shared_ptr<login_host> login) {
            login_host_ = login;
        }

        glz::json_t const& user() const {
            // For the new Glaze API, we can't check if JSON is empty the same way
            // We'll just fetch user info each time for simplicity
            user_ = fetch_user();
            return user_;
        }

        glz::json_t organizations() const {
            return fetch("https://api.github.com/user/orgs");
        }

        glz::json_t fetch_user() const {
            return fetch("https://api.github.com/user");
        }

        glz::json_t org_repos(std::string_view org) const {
            return fetch(std::format("https://api.github.com/orgs/{}/repos", org));
        }

        glz::json_t user_repos() const {
            return fetch_all("https://api.github.com/user/repos");
        }

        glz::json_t find_repo(std::string_view full_name) const {
            return fetch(std::format("https://api.github.com/repos/{}", full_name));
        }

        glz::json_t repo_workflows(std::string_view full_name) const {
            return fetch(std::format("https://api.github.com/repos/{}/actions/workflows", full_name));
        }

        glz::json_t workflow_runs(std::string_view url) const {
            // Fix: convert string_view to string before concatenation
            std::string url_str(url);
            return fetch(url_str + "/runs");
        }

        // Updated to match the type used in the fetch.hpp implementation
        auto header_client() const {
            // Fix: store the bearer header in a variable before capturing it
            std::string bearer = std::format("Authorization: Bearer {}", login_host_->personal_token());
            return [bearer](auto setheader) {
                setheader(bearer);
            };
        }

        bool dispatch_workflow(std::string_view repo_full_name, std::string_view workflow_id, std::string_view ref) const {
            std::string url = std::format("https://api.github.com/repos/{}/actions/workflows/{}/dispatches", 
                                        repo_full_name, workflow_id);
            
            // Create JSON body
            std::string body = std::format(R"({{"ref":"{}"}})", ref);
            
            try {
                http::fetch fetcher;
                fetcher.post(url, body, header_client());
                return true;
            } catch (const std::exception& e) {
                LOG_COMPONENT("GITHUB", LOG_LEVEL_ERROR, debug::format_log("Failed to dispatch workflow: {}", e.what()));
                return false;
            }
        }

        std::string fetch_string(const std::string &url) const {
            http::fetch fetch;
            auto const source { fetch(url, header_client()) };
            return source;
        }

        glz::json_t fetch(const std::string &url) const {
            std::string json_str = fetch_string(url);
            glz::json_t result;
            auto ec = glz::read_json(result, json_str);
            if (ec) {
                // Handle error - return empty json object on error
                return glz::json_t{};
            }
            return result;
        }

        std::vector<glz::json_t> fetch_page_vector(const std::string &url) const {
            std::string json_str = fetch_string(url);
            std::vector<glz::json_t> result;
            auto ec = glz::read_json(result, json_str);
            if (ec) {
                return {};
            }
            return result;
        }

        glz::json_t fetch_all(const std::string &url) const {
            std::vector<glz::json_t> all_items;
            int page = 1;
            char separator = (url.find('?') != std::string::npos) ? '&' : '?';
            
            while (true) {
                std::string page_url = std::format("{}{}per_page=100&page={}", url, separator, page);
                std::vector<glz::json_t> page_items = fetch_page_vector(page_url);
                
                if (page_items.empty()) {
                    break;
                }
                
                all_items.insert(all_items.end(), page_items.begin(), page_items.end());
                
                if (page_items.size() < 100) {
                    break;
                }
                
                page++;
            }
            
            return all_items;
        }

        void open_url(const std::string &url) const {
            // Use platform-specific function to open the URL
            [[maybe_unused]] int system_result = system(rouen::platform::open_file(url).c_str());
        }
        
    private:
        std::shared_ptr<login_host> login_host_;
        mutable glz::json_t user_;
    };
}
