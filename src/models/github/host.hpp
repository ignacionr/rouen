#pragma once

#include <chrono>
#include <format>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include <glaze/json.hpp>
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/fetch.hpp"
#include "login_host.hpp"
#include "../../registrar.hpp"

namespace rouen::models::github {
    struct host {
        // Renamed from login_host to set_login_host to avoid conflict with type name
        void set_login_host(std::shared_ptr<login_host> host) {
            login_host_ = host;
        }

        glz::json_t const& user() const {
            if (user_.empty()) {
                user_ = fetch_user();
            }
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

        glz::json_t fetch_all(const std::string &url) const {
            static auto constexpr page_size {100};
            glz::json_t::array_t result;
            for (int page = 1; ; ++page) {
                auto source = fetch(std::format("{}?per_page={}&page={}", url, page_size, page));
                if (source.empty()) {
                    break;
                }
                auto& source_array = source.get_array();
                result.insert(result.end(), source_array.begin(), source_array.end());
                if (source_array.size() < page_size) {
                    break;
                }
            }
            return result;
        }

        void open_url(const std::string &url) const {
            // Use platform-specific function to open the URL
            system(rouen::platform::open_file(url).c_str());
        }
        
    private:
        std::shared_ptr<login_host> login_host_;
        mutable glz::json_t user_;
    };
}