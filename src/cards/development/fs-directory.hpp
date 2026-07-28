#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../interface/card.hpp"

namespace rouen::cards {

    // Helper function to resolve environment variables in path
    [[nodiscard]] std::string resolve_env_variables(std::string_view path_with_vars);

    struct fs_directory : public card {
        explicit fs_directory(std::string_view path);

        [[nodiscard]] std::string get_uri() const override;
        void receive_keystrokes();
        bool render() override;

    private:
        std::filesystem::path path_;
        std::string filter_;
        bool is_git_repo_{false};
        std::vector<std::filesystem::directory_entry> cached_entries_;
        std::chrono::steady_clock::time_point last_refresh_{};

        // Cmd+F search state
        bool search_active_{false};
        bool focus_search_input_{false};
        char search_query_buf_[256]{""};
        std::string last_typed_query_;
        std::string last_searched_query_;
        std::chrono::steady_clock::time_point last_type_time_{};
        bool search_pending_{false};
        std::vector<std::filesystem::directory_entry> search_results_;

        static std::string to_lower(std::string_view s);
        void refresh_cache();
        void perform_search(std::string_view query);
        void render_entry(const std::filesystem::directory_entry& entry, const std::string& display_label);
    };

} // namespace rouen::cards
