#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../../../helpers/glaze_include.hpp"
#include "../../../helpers/views/json_view.hpp"

namespace rouen::models::github {
    struct host;
}

namespace rouen::cards::github {

    struct repo_screen {
        repo_screen(glz::json_t repo, std::shared_ptr<models::github::host> host);

        [[nodiscard]] std::string name() const;
        [[nodiscard]] std::string full_name() const;

        void render();

        [[nodiscard]] const glz::json_t& json() const { return repo_; }

    private:
        void render_workflow_status_summary();
        void render_workflows_detailed();
        void load_all_workflow_runs();
        void load_workflow_runs(const std::string& workflow_id);
        void render_workflow_runs(const std::string& workflow_id);

        glz::json_t repo_;
        std::shared_ptr<models::github::host> host_;
        bool show_details_{false};
        bool show_all_workflows_{false};
        glz::json_t workflows_{};
        std::unordered_map<std::string, glz::json_t> workflow_runs_{};
        helpers::views::json_view json_view_;
    };

} // namespace rouen::cards::github
