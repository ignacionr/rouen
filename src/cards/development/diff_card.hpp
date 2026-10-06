#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#include "../interface/card.hpp"
#include "../../helpers/diff_engine.hpp"
#include "../../helpers/code_editor_service.hpp"

namespace rouen::cards {

enum class DiffViewMode {
    SideBySide,
    Unified
};

enum class DiffSource {
    StagedAI,
    GitHead,
    CustomFile
};

class diff_card : public card {
public:
    explicit diff_card(std::string_view uri = "");
    ~diff_card() override = default;

    [[nodiscard]] std::string get_uri() const override;
    [[nodiscard]] bool matches_uri(std::string_view uri) const override;
    void handle_uri(std::string_view uri) override;
    [[nodiscard]] std::vector<mcp_function> get_mcp_functions() const override;
    bool render() override;

    // Data loading
    void load_staged_edit(size_t index = 0);
    void load_file_vs_head(const std::string& full_path);
    void load_arbitrary_diff(const std::string& old_str, const std::string& new_str, const std::string& label = "Comparison");
    void refresh_sources();

    // Chunk actions
    void accept_chunk(int chunk_id);
    void discard_chunk(int chunk_id);
    void reset_chunk(int chunk_id);
    void accept_all();
    void discard_all();
    void reset_all();
    bool apply_to_disk();

private:
    void render_toolbar();
    void render_side_by_side();
    void render_unified();
    void render_history_drawer();
    void recompute_diff();

    std::string uri_param_;
    std::string file_path_;
    std::string file_name_;
    std::string base_content_;
    std::string proposed_content_;
    rouen::helpers::DiffFile diff_;

    DiffViewMode view_mode_{DiffViewMode::SideBySide};
    DiffSource source_mode_{DiffSource::StagedAI};

    bool show_history_drawer_{false};
    std::string current_staged_id_;

    std::string status_message_;
    float status_message_timer_{0.0f};
    bool status_message_is_error_{false};

    std::vector<std::string> git_modified_files_;
    int selected_git_file_idx_{0};
    int selected_staged_idx_{0};
    std::string repo_root_;
};

} // namespace rouen::cards
