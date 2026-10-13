#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../interface/card.hpp"
#include "../../../helpers/adaptive_cards/parser.hpp"
#include "../../../helpers/adaptive_cards/renderer.hpp"
#include "../../../helpers/glaze_include.hpp"
#include "../../../helpers/ui_context.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "login_screen.hpp"
#include "repo_screen.hpp"

namespace rouen::models::github {
    struct host;
    struct login_host;
}

namespace rouen::cards {

    struct github_issue {
        int number{0};
        std::string title{};
        std::string body{};
        std::string state{"open"};
    };

    struct github_pr {
        int number{0};
        std::string title{};
        std::string body{};
        std::string state{"open"};
        std::string branch{"main"};
    };

    struct github_card : public card {
        explicit github_card(std::string_view config_name = "default");

        [[nodiscard]] std::string get_uri() const override;
        bool render() override;
        bool render(rouen::ui::ui_context& ui) override;

        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override;

        /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
        void handle_action(std::string_view action_json) override;

        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }
        [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
        [[nodiscard]] const std::string& get_last_created_issue_title() const noexcept { return last_created_issue_title_; }
        [[nodiscard]] const std::string& get_last_created_issue_body() const noexcept { return last_created_issue_body_; }
        [[nodiscard]] const std::string& get_current_repo() const noexcept { return current_repo; }
        void set_current_repo(std::string_view repo) { current_repo = repo; }
        [[nodiscard]] const std::vector<github_issue>& get_open_issues() const noexcept { return open_issues; }
        [[nodiscard]] const std::vector<github_pr>& get_open_prs() const noexcept { return open_prs; }
        [[nodiscard]] const std::string& get_filter_query() const noexcept { return filter_query; }
        [[nodiscard]] const std::string& get_filter_type() const noexcept { return filter_type; }
        [[nodiscard]] bool is_repo_refreshed() const noexcept { return repo_refreshed_; }
        [[nodiscard]] bool is_authenticated() const noexcept;
        [[nodiscard]] bool is_api_sync_enabled() const noexcept { return enable_api_sync; }
        void set_api_sync_enabled(bool enabled) noexcept { enable_api_sync = enabled; }

        // Synchronized card state
        std::string current_repo{"ignacionr/rouen"};
        std::vector<github_issue> open_issues{};
        std::vector<github_pr> open_prs{};
        std::string filter_query{};
        std::string filter_type{"all"};
        std::string default_branch{"main"};
        std::string latest_release{"v2.4.0 (2 days ago)"};
        int stars_count{128};
        bool enable_api_sync{false};

    private:
        void save_token_to_file();
        void load_token_from_file();

        [[nodiscard]] std::filesystem::path get_config_directory() const;
        [[nodiscard]] std::string get_config_filename() const;

        void render_main_interface();
        void render_overview_tab();
        void render_repositories_tab();
        void render_profile_tab();
        void render_settings_tab();

        std::string config_name_;
        std::shared_ptr<models::github::host> host_;
        std::shared_ptr<models::github::login_host> login_host_;
        std::unique_ptr<github::login_screen> login_screen_;
        bool config_mode_ = false;
        std::string selected_org_login_;
        std::vector<github::repo_screen> repos_;
        std::string repo_name_;
        char repo_filter_[256] = {0};
        std::string latest_error_;
        glz::json_t organizations_{};
        glz::json_t user_info_{};
        [[maybe_unused]] helpers::views::json_view json_view_;
        std::string debug_repos_json_;

        // Adaptive Card presentation override state
        bool adaptive_view_mode{false};
        std::string last_action_{};
        std::string last_created_issue_title_{};
        std::string last_created_issue_body_{};
        bool repo_refreshed_{false};
        mutable helpers::adaptive_cards::parser adaptive_parser_{};
        mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
        mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
        mutable helpers::adaptive_cards::card_document adaptive_bound_{};
        std::string adaptive_error_{};
        std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
    };

} // namespace rouen::cards
