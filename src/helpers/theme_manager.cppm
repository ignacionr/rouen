module;

#include <concepts>
#include <utility>
#include <array>
#include <string>
#include <vector>
#include <memory>
export module rouen.helpers.theme_manager;

struct card;

export namespace rouen::theme {

    struct theme_palette {
        std::string name;
        bool draw_card_outline = true;
        
        std::array<float, 4> window_bg;
        std::array<float, 4> text;
        std::array<float, 4> text_disabled;
        std::array<float, 4> title_bg;
        std::array<float, 4> title_bg_active;
        std::array<float, 4> menu_bar_bg;
        std::array<float, 4> button;
        std::array<float, 4> button_hovered;
        std::array<float, 4> button_active;
        std::array<float, 4> frame_bg;
        std::array<float, 4> frame_bg_hovered;
        std::array<float, 4> frame_bg_active;
        std::array<float, 4> check_mark;
        std::array<float, 4> slider_grab;
        
        std::array<std::array<float, 4>, 16> card_colors;
    };

    class theme_manager {
    public:
        static theme_manager& get();

        const std::vector<theme_palette>& get_themes() const { return themes_; }
        std::vector<theme_palette>& get_themes() { return themes_; }

        size_t get_active_theme_index() const { return active_theme_index_; }

        const theme_palette& get_active_theme() const { return themes_[active_theme_index_]; }
        theme_palette& get_active_theme() { return themes_[active_theme_index_]; }

        void select_theme(size_t index);
        void select_theme(const std::string& name);
        void save_or_update_theme(const theme_palette& theme);
        void delete_theme(size_t index);

        void apply_theme_to_card(card* c) const;
        void apply_theme_to_imgui() const;
        void load_themes();
        void save_themes() const;

    private:
        theme_manager();
        ~theme_manager() = default;

        theme_manager(const theme_manager&) = delete;
        theme_manager& operator=(const theme_manager&) = delete;

        void setup_default_themes();

        std::vector<theme_palette> themes_;
        size_t active_theme_index_ = 0;
    };

} // namespace rouen::theme
