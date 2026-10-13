#pragma once

#include "../interface/card.hpp"
#include "../../helpers/adlib_engine.hpp"
#include "../../helpers/audio_capture.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <string>
#include <vector>

namespace rouen::cards {

struct adlib_card : public card {
    adlib_card(std::string_view locator = "");
    std::string get_uri() const override { return "adlib"; }
    bool has_video_overlay() const override { return false; }
    void render_video_ui() override {}
    bool render() override;
    bool render(rouen::ui::ui_context& ui) override;
    void render_content();
    void on_close() override;

    struct sound_pad_entry {
        int id{1};
        std::string name;
        std::string value;
        double duration_seconds{0.0};
        bool is_playing{false};
    };

    [[nodiscard]] std::string get_adaptive_card_json() const override;
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] const std::string& get_soundbank_name() const noexcept { return soundbank_name_; }
    [[nodiscard]] const std::vector<sound_pad_entry>& get_pads() const noexcept { return pads_; }
    [[nodiscard]] float get_master_volume() const noexcept { return master_volume_; }
    [[nodiscard]] bool is_any_playing() const noexcept { return any_playing_; }
    [[nodiscard]] int get_last_triggered_pad() const noexcept { return last_triggered_pad_; }

private:
    std::string soundbank_name_{"Broadcast Cues & FX"};
    std::vector<sound_pad_entry> pads_{};
    float master_volume_{1.0f};
    bool any_playing_{false};
    int last_triggered_pad_{0};
    void draw_template_config();
    void draw_execution_deck();
    void refresh_audio_devices();
    void load_config_from_ini();
    void save_config_to_ini();
    bool validate_config(std::string& out_error_msg);

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    char intro_path_buf_[512]{0};
    char bg_path_buf_[512]{0};
    char outro_path_buf_[512]{0};
    char output_path_buf_[512]{0};

    int selected_mode_{0}; // 0 = Live, 1 = Recorded
    int selected_mic_idx_{0};
    float stage2_transition_seconds_{0.0f}; // 0.0 = Manual, >0 = auto-transition after N seconds
    std::string validation_error_;
    std::vector<rouen::helpers::AudioInputDevice> audio_devices_;
};

} // namespace rouen::cards
