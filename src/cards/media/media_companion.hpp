#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

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
    };

} // namespace rouen::cards
