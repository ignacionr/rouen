#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../../helpers/glaze_include.hpp"
#include <imgui.h>

#include "github_card.hpp"

#include "../../../helpers/debug.hpp"
#include "../../../../external/IconsMaterialDesign.h"
#include "../../../fonts.hpp"
#include "../../../helpers/fetch.hpp"
#include "../../../helpers/imgui_ui_context.hpp"
#include "../../../helpers/platform_utils.hpp"
#include "../../../models/github/host.hpp"
#include "../../../models/github/login_host.hpp"
#include "../../../registrar.hpp"
#include "cards/development/github/login_screen.hpp"

namespace rouen::cards {

    github_card::github_card(std::string_view config_name)
        : config_name_{std::string(config_name)} {
        // GitHub dark theme inspired colors
        colors[0] = {0.13f, 0.37f, 0.71f, 1.0f}; // Primary color (GitHub blue)
        colors[1] = {0.18f, 0.18f, 0.18f, 0.7f}; // Secondary color (dark gray)
        colors[2] = {0.15f, 0.68f, 0.38f, 1.0f}; // Success color (green)
        colors[3] = {0.85f, 0.25f, 0.25f, 1.0f}; // Failure color (red)

        name("GitHub");
        width = 800.0f;

        try {
            login_host_ = registrar::get<models::github::login_host>(std::string(config_name));
        } catch (const std::exception& e) {
            LOG_COMPONENT("GitHubCard", LOG_LEVEL_DEBUG, std::format("Login host registration lookup failed for {}: {}", config_name, e.what()));
            login_host_ = std::make_shared<models::github::login_host>();
            load_token_from_file();

            if (login_host_->personal_token().empty()) {
                config_mode_ = true;
            }
        }

        host_ = std::make_shared<models::github::host>();
        host_->set_login_host(login_host_);

        open_issues.reserve(14);
        for (int i = 1; i <= 14; ++i) {
            open_issues.push_back(github_issue{
                .number = i,
                .title = std::format("Issue #{}", i),
                .body = "Issue description",
                .state = "open"
            });
        }

        open_prs.reserve(3);
        for (int i = 1; i <= 3; ++i) {
            open_prs.push_back(github_pr{
                .number = i,
                .title = std::format("Pull Request #{}", i),
                .body = "Pull request description",
                .state = "open",
                .branch = "main"
            });
        }
    }

    std::string github_card::get_uri() const {
        if (config_name_ == "default") {
            return "github";
        }
        return std::format("github:{}", config_name_);
    }

    void github_card::save_token_to_file() {
        try {
            std::filesystem::path const config_dir = get_config_directory();
            std::filesystem::create_directories(config_dir);

            std::string const filename = get_config_filename();
            glz::json_t config;
            config["token"] = login_host_->personal_token();

            std::ofstream file(filename);
            if (file.is_open()) {
                std::string json_str;
                auto result = glz::write_json(config, json_str);
                if (!result) {
                    file << json_str;
                }
                file.close();
            }
        } catch (const std::exception& e) {
            LOG_COMPONENT("GitHubCard", LOG_LEVEL_WARN, std::format("Failed to save GitHub token: {}", e.what()));
        }
    }

    void github_card::load_token_from_file() {
        try {
            std::string const filename = get_config_filename();

            if (!std::filesystem::exists(filename)) {
                return;
            }

            std::ifstream file(filename);
            if (file.is_open()) {
                std::string json_str((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());

                glz::json_t config;
                auto ec = glz::read_json(config, json_str);
                if (!ec && config.contains("token")) {
                    try {
                        std::string const token = config["token"].get<std::string>();
                        login_host_->set_personal_token(token);
                    } catch (const std::exception& e) {
                        LOG_COMPONENT("GitHubCard", LOG_LEVEL_WARN, std::format("Failed to parse token from json: {}", e.what()));
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_COMPONENT("GitHubCard", LOG_LEVEL_WARN, std::format("Failed to load GitHub token file: {}", e.what()));
        }
    }

    std::filesystem::path github_card::get_config_directory() const {
        const char* home = std::getenv("HOME");
        std::filesystem::path const base = home ? home : ".";
        return base / ".config" / "rouen" / "github";
    }

    std::string github_card::get_config_filename() const {
        return (get_config_directory() / (config_name_ + ".json")).string();
    }

    bool github_card::render() {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool github_card::render(rouen::ui::ui_context& ui) {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        rouen::helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        rouen::helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

            if (config_mode_) {
                if (!login_screen_) {
                    login_screen_ = std::make_unique<github::login_screen>(*login_host_);
                }

                if (login_screen_->render()) {
                    registrar::add<models::github::login_host>(config_name_, login_host_);
                    save_token_to_file();
                    config_mode_ = false;
                }
            } else {
                render_main_interface();
            }
        });
    }

    std::string github_card::get_adaptive_card_json() const {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // Format repo display name
        std::string repo_display = current_repo;
        if (auto slash = repo_display.find('/'); slash != std::string::npos && (slash == 0 || repo_display[slash - 1] != ' ')) {
            repo_display = repo_display.substr(0, slash) + " / " + repo_display.substr(slash + 1);
        }

        // 1. Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Col 1: emoji
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "\xF0\x9F\x90\x99"; // 🐙
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Col 2: Title and Subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = repo_display;
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "Native C++20 desktop environment powered by ImGui & SDL3";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                tb_sub["spacing"] = "None";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Col 3: Stars Badge
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                glz::json_t tb_star;
                tb_star["type"] = "TextBlock";
                tb_star["text"] = std::format("\xE2\xAD\x90 {}", stars_count); // ⭐ 128
                tb_star["weight"] = "Bolder";
                tb_star["size"] = "Medium";
                items.push_back(std::move(tb_star));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header_container));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            {
                glz::json_t f;
                f["title"] = "Open Issues:";
                f["value"] = std::format("{} issue{}", open_issues.size(), open_issues.size() == 1 ? "" : "s");
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Open Pull Requests:";
                f["value"] = std::format("{} pull request{}", open_prs.size(), open_prs.size() == 1 ? "" : "s");
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Default Branch:";
                f["value"] = default_branch;
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Latest Release:";
                f["value"] = latest_release;
                facts.push_back(std::move(f));
            }

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Quick Issue Creation Container
        {
            glz::json_t issue_container;
            issue_container["type"] = "Container";
            issue_container["separator"] = true;
            std::vector<glz::json_t> items;

            {
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "Quick Issue Creation";
                tb["weight"] = "Bolder";
                items.push_back(std::move(tb));
            }
            {
                glz::json_t input;
                input["type"] = "Input.Text";
                input["id"] = "issue_title";
                input["placeholder"] = "Issue title...";
                items.push_back(std::move(input));
            }
            {
                glz::json_t input;
                input["type"] = "Input.Text";
                input["id"] = "issue_body";
                input["placeholder"] = "Describe bug or enhancement...";
                input["isMultiline"] = true;
                items.push_back(std::move(input));
            }

            issue_container["items"] = std::move(items);
            body.push_back(std::move(issue_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Submit Issue";
            act["verb"] = "create_issue";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Refresh Repository";
            act["verb"] = "refresh_repo";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.OpenUrl";
            act["title"] = "Open on GitHub";
            std::string clean_repo = current_repo;
            if (auto pos = clean_repo.find(" / "); pos != std::string::npos) {
                clean_repo = clean_repo.substr(0, pos) + "/" + clean_repo.substr(pos + 3);
            }
            act["url"] = std::format("https://github.com/{}", clean_repo);
            actions.push_back(std::move(act));
        }

        card["actions"] = std::move(actions);

        std::string out;
        auto ec = glz::write_json(card, out);
        if (ec) {
            return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"body\":[]}";
        }
        return out;
    }

    void github_card::handle_action(std::string_view action_json) {
        if (action_json.empty()) return;
        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb.empty()) return;

            last_action_ = verb;

            if (verb == "create_issue") {
                std::string title;
                std::string body;
                if (data.is_object()) {
                    if (data.contains("issue_title") && data["issue_title"].holds<std::string>()) {
                        title = data["issue_title"].get<std::string>();
                    } else if (data.contains("title") && data["title"].holds<std::string>()) {
                        title = data["title"].get<std::string>();
                    }

                    if (data.contains("issue_body") && data["issue_body"].holds<std::string>()) {
                        body = data["issue_body"].get<std::string>();
                    } else if (data.contains("body") && data["body"].holds<std::string>()) {
                        body = data["body"].get<std::string>();
                    }
                }
                last_created_issue_title_ = title;
                last_created_issue_body_ = body;

                github_issue issue;
                issue.number = static_cast<int>(open_issues.size() + 1);
                issue.title = title;
                issue.body = body;
                issue.state = "open";
                open_issues.push_back(std::move(issue));

                if (enable_api_sync && host_ && login_host_ && !login_host_->personal_token().empty() && !current_repo.empty() && !title.empty()) {
                    try {
                        std::string const url = std::format("https://api.github.com/repos/{}/issues", current_repo);
                        glz::json_t issue_req;
                        issue_req["title"] = title;
                        issue_req["body"] = body;
                        std::string req_str;
                        if (!glz::write_json(issue_req, req_str)) {
                            http::fetch fetcher;
                            fetcher.post(url, req_str, host_->header_client());
                        }
                    } catch (const std::exception& e) {
                        LOG_COMPONENT("GitHubCard", LOG_LEVEL_WARN, std::format("Failed to post issue via API: {}", e.what()));
                    }
                }
            } else if (verb == "filter_issues") {
                std::string filter_val;
                if (data.is_object()) {
                    if (data.contains("filter_type") && data["filter_type"].holds<std::string>()) {
                        filter_val = data["filter_type"].get<std::string>();
                    } else if (data.contains("filter") && data["filter"].holds<std::string>()) {
                        filter_val = data["filter"].get<std::string>();
                    }
                }
                filter_type = filter_val;
                filter_query = filter_val;
            } else if (verb == "refresh_repo") {
                repo_refreshed_ = true;
                if (enable_api_sync && host_ && login_host_ && !login_host_->personal_token().empty() && !current_repo.empty()) {
                    try {
                        auto repo_data = host_->find_repo(current_repo);
                        if (repo_data.contains("stargazers_count")) {
                            stars_count = static_cast<int>(repo_data["stargazers_count"].get<double>());
                        }
                        if (repo_data.contains("default_branch") && repo_data["default_branch"].holds<std::string>()) {
                            default_branch = repo_data["default_branch"].get<std::string>();
                        }
                    } catch (const std::exception& e) {
                        LOG_COMPONENT("GitHubCard", LOG_LEVEL_WARN, std::format("Failed to refresh repo via API: {}", e.what()));
                    }
                }
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
            }
        } catch (const std::exception& e) {
            std::cerr << "[github] Action error: " << e.what() << std::endl;
        }
    }

    bool github_card::is_authenticated() const noexcept {
        return login_host_ && !login_host_->personal_token().empty();
    }

    void github_card::render_main_interface() {
        if (ImGui::BeginTabBar("GitHubTabs")) {
            if (ImGui::BeginTabItem(ICON_MD_DASHBOARD " Overview")) {
                render_overview_tab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_MD_FOLDER " Repositories")) {
                render_repositories_tab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_MD_ACCOUNT_CIRCLE " Profile")) {
                render_profile_tab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_MD_SETTINGS " Settings")) {
                render_settings_tab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void github_card::render_overview_tab() {
        try {
            ImGui::TextColored(colors[0], ICON_MD_INFO " GitHub Overview");

            if (user_info_.empty()) {
                if (ImGui::Button("Load Profile")) {
                    user_info_ = host_->user();
                }
            } else {
                if (user_info_.contains("login")) {
                    std::string const login = user_info_["login"].get<std::string>();
                    ImGui::Text("User: %s", login.c_str());
                }

                if (user_info_.contains("name") && !user_info_["name"].is_null()) {
                    std::string const user_name = user_info_["name"].get<std::string>();
                    ImGui::Text("Name: %s", user_name.c_str());
                }

                if (user_info_.contains("public_repos")) {
                    int const public_repos = static_cast<int>(user_info_["public_repos"].get<double>());
                    ImGui::Text("Public Repositories: %d", public_repos);
                }

                if (user_info_.contains("followers")) {
                    int const followers = static_cast<int>(user_info_["followers"].get<double>());
                    ImGui::Text("Followers: %d", followers);
                }

                if (user_info_.contains("following")) {
                    int const following = static_cast<int>(user_info_["following"].get<double>());
                    ImGui::Text("Following: %d", following);
                }
            }

            ImGui::Separator();

            ImGui::TextColored(colors[0], ICON_MD_FLASH_ON " Quick Actions");

            if (ImGui::Button(ICON_MD_FOLDER " My Repositories")) {
                ImGui::SetTabItemClosed("Repositories");
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_BUILD " CI/CD Dashboard")) {
                latest_error_ = "CI/CD Dashboard - Use dedicated GitHub CI card";
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_OPEN_IN_BROWSER " GitHub.com")) {
                host_->open_url("https://github.com");
            }

        } catch (const std::exception& e) {
            ImGui::TextColored(colors[3], "Error: %s", e.what());
        }
    }

    void github_card::render_repositories_tab() {
        try {
            ImGui::TextColored(colors[0], ICON_MD_FOLDER " My Repositories");

            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputTextWithHint("##repo_filter", "Filter repositories...", 
                                    repo_filter_, sizeof(repo_filter_));

            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_REFRESH " Refresh")) {
                repos_.clear();
                auto repos_json = host_->user_repos();

                if (repos_json.is_array()) {
                    for (const auto& repo_json : repos_json.get<std::vector<glz::json_t>>()) {
                        repos_.emplace_back(repo_json, host_);
                    }
                }
            }

            if (repos_.empty()) {
                if (ImGui::Button("Load Repositories")) {
                    auto repos_json = host_->user_repos();

                    if (repos_json.is_array()) {
                        for (const auto& repo_json : repos_json.get<std::vector<glz::json_t>>()) {
                            repos_.emplace_back(repo_json, host_);
                        }
                    }
                }
            } else {
                if (ImGui::BeginTable("repositories", 4, ImGuiTableFlags_Resizable | 
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {

                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Language", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                    ImGui::TableHeadersRow();

                    for (auto& repo : repos_) {
                        std::string repo_n = repo.name();

                        if (repo_filter_[0] != '\0' && 
                            repo_n.find(repo_filter_) == std::string::npos) {
                            continue;
                        }

                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::Text("%s", repo_n.c_str());

                        ImGui::TableNextColumn();
                        if (repo.json().contains("language") && !repo.json()["language"].is_null()) {
                            std::string const language = repo.json()["language"].get<std::string>();
                            ImGui::Text("%s", language.c_str());
                        } else {
                            ImGui::TextColored(colors[5], "—");
                        }

                        ImGui::TableNextColumn();
                        if (repo.json().contains("stargazers_count")) {
                            int const stars = static_cast<int>(repo.json()["stargazers_count"].get<double>());
                            ImGui::Text("%d", stars);
                        } else {
                            ImGui::TextColored(colors[5], "—");
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushID(repo_n.c_str());

                        if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " View##repo_view")) {
                            if (repo.json().contains("html_url")) {
                                std::string const url = repo.json()["html_url"].get<std::string>();
                                host_->open_url(url);
                            }
                        }

                        ImGui::SameLine();
                        if (ImGui::SmallButton(ICON_MD_BUILD " CI/CD##repo_cicd")) {
                            latest_error_ = std::format("CI/CD for {} - Use GitHub CI card", repo_n);
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }

        } catch (const std::exception& e) {
            ImGui::TextColored(colors[3], "Error: %s", e.what());
        }
    }

    void github_card::render_profile_tab() {
        try {
            ImGui::TextColored(colors[0], ICON_MD_ACCOUNT_CIRCLE " GitHub Profile");

            if (user_info_.empty()) {
                if (ImGui::Button("Load Profile Data")) {
                    user_info_ = host_->user();
                }
            } else {
                if (ImGui::BeginTable("profile", 2, ImGuiTableFlags_SizingFixedFit)) {
                    auto display_field = [&](const char* label, const char* key, bool is_url = false) {
                        if (user_info_.contains(key) && !user_info_[key].is_null()) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s:", label);
                            ImGui::TableNextColumn();

                            std::string const value = user_info_[key].get<std::string>();
                            if (is_url && !value.empty()) {
                                if (ImGui::SmallButton(value.c_str())) {
                                    host_->open_url(value);
                                }
                            } else {
                                ImGui::Text("%s", value.c_str());
                            }
                        }
                    };

                    display_field("Username", "login");
                    display_field("Name", "name");
                    display_field("Company", "company");
                    display_field("Blog", "blog", true);
                    display_field("Location", "location");
                    display_field("Email", "email");
                    display_field("Bio", "bio");
                    display_field("Twitter", "twitter_username");
                    display_field("GitHub URL", "html_url", true);

                    auto display_number = [&](const char* label, const char* key) {
                        if (user_info_.contains(key)) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s:", label);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", static_cast<int>(user_info_[key].get<double>()));
                        }
                    };

                    display_number("Public Repos", "public_repos");
                    display_number("Public Gists", "public_gists");
                    display_number("Followers", "followers");
                    display_number("Following", "following");

                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::TextColored(colors[0], ICON_MD_BUSINESS " Organizations");

                if (organizations_.empty()) {
                    if (ImGui::Button("Load Organizations")) {
                        organizations_ = host_->organizations();
                    }
                } else {
                    if (organizations_.is_array()) {
                        for (const auto& org : organizations_.get<std::vector<glz::json_t>>()) {
                            if (org.contains("login")) {
                                std::string const org_name = org["login"].get<std::string>();
                                if (ImGui::SmallButton(org_name.c_str())) {
                                    if (org.contains("html_url")) {
                                        std::string const url = org["html_url"].get<std::string>();
                                        host_->open_url(url);
                                    }
                                }
                                ImGui::SameLine();
                            }
                        }
                    }
                }
            }

        } catch (const std::exception& e) {
            ImGui::TextColored(colors[3], "Error: %s", e.what());
        }
    }

    void github_card::render_settings_tab() {
        ImGui::TextColored(colors[0], ICON_MD_SETTINGS " GitHub Settings");

        if (login_host_->personal_token().empty()) {
            ImGui::TextColored(colors[3], ICON_MD_WARNING " No GitHub token configured");
        } else {
            ImGui::TextColored(colors[1], ICON_MD_CHECK_CIRCLE " GitHub token configured");

            std::string const token = login_host_->personal_token();
            std::string const masked_token = (token.length() >= 8) 
                ? (token.substr(0, 4) + "..." + token.substr(token.length() - 4))
                : "********";
            ImGui::Text("Token: %s", masked_token.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button(ICON_MD_EDIT " Reconfigure Token")) {
            config_mode_ = true;
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_REFRESH " Test Connection")) {
            try {
                auto test_user = host_->user();
                if (test_user.contains("login")) {
                    latest_error_ = "Connection successful!";
                } else {
                    latest_error_ = "Connection failed - invalid response";
                }
            } catch (const std::exception& e) {
                latest_error_ = std::format("Connection failed: {}", e.what());
            }
        }

        ImGui::Separator();

        ImGui::Text("Configuration:");
        ImGui::Text("  Config Name: %s", config_name_.c_str());
        ImGui::Text("  Config File: %s", get_config_filename().c_str());

        ImGui::Separator();
        if (ImGui::Button(ICON_MD_CLEAR " Clear Cache")) {
            user_info_ = glz::json_t{};
            organizations_ = glz::json_t{};
            repos_.clear();
            latest_error_ = "Cache cleared";
        }
    }

} // namespace rouen::cards
