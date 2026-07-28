#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../interface/card.hpp"
#include "../../../helpers/glaze_include.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "login_screen.hpp"
#include "repo_screen.hpp"

namespace rouen::models::github {
    struct host;
    struct login_host;
}

namespace rouen::cards {

    struct github_card : public card {
        explicit github_card(std::string_view config_name = "default");

        [[nodiscard]] std::string get_uri() const override;
        bool render() override;

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
    };

} // namespace rouen::cards
