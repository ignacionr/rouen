#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#include "../interface/card.hpp"
#include "../../helpers/diff_engine.hpp"
#include "../../helpers/code_editor_service.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/ui_context.hpp"

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
    bool render(rouen::ui::ui_context& ui) override;

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] size_t get_current_hunk_idx() const noexcept { return current_hunk_idx_; }
    [[nodiscard]] DiffViewMode get_view_mode() const noexcept { return view_mode_; }
    [[nodiscard]] const std::string& get_file_path() const noexcept { return file_path_; }
    [[nodiscard]] const rouen::helpers::DiffFile& get_diff() const noexcept { return diff_; }

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

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
    size_t current_hunk_idx_{0};

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
