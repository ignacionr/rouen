#pragma once

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../helpers/fetch.hpp"
#include "login_host.hpp"
#include "../../registrar.hpp"

namespace rouen::models::github {
    struct host {
        // Renamed from login_host to set_login_host to avoid conflict with type name
        void set_login_host(std::shared_ptr<login_host> host) {
            login_host_ = host;
        }

        nlohmann::json const& user() const {
            if (user_.empty()) {
                user_ = fetch_user();
            }
            return user_;
        }

        nlohmann::json organizations() const {
            return fetch("https://api.github.com/user/orgs");
        }

        nlohmann::json fetch_user() const {
            return fetch("https://api.github.com/user");
        }

        nlohmann::json org_repos(std::string_view org) const {
            return fetch(std::format("https://api.github.com/orgs/{}/repos", org));
        }

        nlohmann::json user_repos() const {
            return fetch_all("https://api.github.com/user/repos");
        }

        nlohmann::json find_repo(std::string_view full_name) const {
            return fetch(std::format("https://api.github.com/repos/{}", full_name));
        }

        nlohmann::json repo_workflows(std::string_view full_name) const {
            return fetch(std::format("https://api.github.com/repos/{}/actions/workflows", full_name));
        }

        nlohmann::json workflow_runs(std::string_view url) const {
            // Fix: convert string_view to string before concatenation
            std::string url_str(url);
            return fetch(url_str + "/runs");
        }

        http::fetch::header_client_t header_client() const {
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

        nlohmann::json fetch(const std::string &url) const {
            return nlohmann::json::parse(fetch_string(url));
        }

        nlohmann::json fetch_all(const std::string &url) const {
            static auto constexpr page_size {100};
            nlohmann::json::array_t result;
            for (int page = 1; ; ++page) {
                auto const source { fetch(std::format("{}?per_page={}&page={}", url, page_size, page)) };
                if (source.empty()) {
                    break;
                }
                result.insert(result.end(), source.begin(), source.end());
                if (source.size() < page_size) {
                    break;
                }
            }
            return result;
        }

        void open_url(const std::string &url) const {
            // Use system browser to open the URL
            #ifdef _WIN32
                system(std::format("start {}", url).c_str());
            #elif __APPLE__
                system(std::format("open {}", url).c_str());
            #else
                system(std::format("xdg-open {}", url).c_str());
            #endif
        }
        
    private:
        std::shared_ptr<login_host> login_host_;
        mutable nlohmann::json user_;
    };
}