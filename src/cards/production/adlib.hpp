#pragma once

#include "../interface/card.hpp"
#include "../../helpers/adlib_engine.hpp"
#include "../../helpers/audio_capture.hpp"
#include "../../helpers/config_service.hpp"
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
    void render_content();
    void on_close() override;

private:
    void draw_template_config();
    void draw_execution_deck();
    void refresh_audio_devices();
    void load_config_from_ini();
    void save_config_to_ini();
    bool validate_config(std::string& out_error_msg);

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
