#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

    class media_companion : public card {
    public:
        struct language_option {
            std::string name;
            std::string code;
        };

        static const std::vector<language_option> supported_languages;

        struct transcript_entry {
            double timestamp_seconds{0.0};
            std::string timestamp_str;
            std::string text;
        };

        struct fact_check_assertion {
            std::string claim;       // Claim / statement evaluated
            double truth_score{0.0}; // Value between -1.0 (complete lie) and +1.0 (certain truth)
            std::string explanation; // Detailed verification / context
        };

        struct dynamic_commentary_point {
            double timestamp_seconds{0.0};
            std::string timestamp_str;
            std::string commentary_md;
            std::vector<fact_check_assertion> assertions;
        };

        struct truth_rating_visual {
            const char* icon;
            const char* label;
            ImVec4 color;
        };

        static truth_rating_visual get_truth_rating_visual(double score);

        struct shared_state {
            std::mutex mutex;
            bool is_fetching{false};
            bool is_generating{false};
            std::string status_message;
            std::string plain_transcript;
            std::string timestamped_transcript;
            std::vector<transcript_entry> entries;
            std::vector<dynamic_commentary_point> commentary_points;
            std::string video_title;
            std::string video_url;
            double video_duration{0.0};
            bool card_alive{true};
        };

        media_companion();
        ~media_companion() override;

        std::string get_uri() const override;
        bool matches_uri(std::string_view uri) const override;

        void request_transcript();
        void generate_commentary_for_full_video();

        bool render(rouen::ui::ui_context& ui) override;
        bool has_video_overlay() const override { return true; }
        void render_video_ui() override;
        void paint_video_surface(SDL_Surface* surface, int surface_w, int surface_h) override;

        std::vector<card::mcp_function> get_mcp_functions() const override;

        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override;

        /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
        void handle_action(std::string_view action_json) override;

        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        [[nodiscard]] bool is_playing() const noexcept { return is_playing_; }
        [[nodiscard]] bool is_shuffle_enabled() const noexcept { return shuffle_enabled; }
        [[nodiscard]] const std::string& get_track_title() const noexcept { return track_title; }
        [[nodiscard]] const std::string& get_artist_name() const noexcept { return artist_name; }
        [[nodiscard]] const std::string& get_album_title() const noexcept { return album_title; }
        [[nodiscard]] const std::string& get_track_progress() const noexcept { return track_progress; }
        [[nodiscard]] const std::string& get_playlist_name() const noexcept { return playlist_name; }
        [[nodiscard]] const std::vector<std::string>& get_queue_items() const noexcept { return queue_items; }
        [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }

    private:
        static void seek_to(double seconds);

        std::shared_ptr<shared_state> state;
        std::string selected_llm_config;
        std::string selected_language{"English"};
        bool enable_fact_check{false};
        bool enable_web_search{true};
        int selected_point_index{0};
        int last_synced_point_index{-1};
        bool auto_sync_enabled{true};

        // Adaptive Card presentation override state
        bool adaptive_view_mode{false};
        mutable helpers::adaptive_cards::parser adaptive_parser_{};
        mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
        mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
        mutable helpers::adaptive_cards::card_document adaptive_bound_{};
        std::string adaptive_error_{};
        std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

        // State Synchronization
        std::string track_title{"Clair de Lune"};
        std::string artist_name{"Claude Debussy"};
        std::string album_title{"Suite bergamasque"};
        std::string audio_format{"Lossless FLAC"};
        std::string track_progress{"02:14 / 05:02 (44%)"};
        std::string playlist_name{"Focus & Code Classical"};
        int playlist_total_tracks{24};
        std::vector<std::string> queue_items{
            "Gymnopédie No. 1 - Erik Satie",
            "Nocturne Op. 9 No. 2 - Frédéric Chopin",
            "Canon in D - Johann Pachelbel"
        };
        std::vector<std::string> history_items{};
        bool shuffle_enabled{false};
        bool is_playing_{false};
        std::string last_action_{};
    };

} // namespace rouen::cards
