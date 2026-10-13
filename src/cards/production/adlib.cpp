#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <iostream>

#include <imgui.h>

#include "adlib.hpp"
#include "../../helpers/platform_utils.hpp"
#include "IconsMaterialDesign.h"
#include "adlib_engine.hpp"
#include "audio_capture.hpp"
#include "config_service.hpp"
#include "../../fonts.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"

namespace rouen::cards {

adlib_card::adlib_card(std::string_view locator) {
    (void)locator;
    width = 640.0f;
    name("Ad-Lib Broadcasting Studio");

    // Vibrant Production Theme Colors (Indigo / Violet / Electric Cyan)
    colors[0] = ImVec4(0.55f, 0.35f, 0.95f, 1.0f); // Primary Accent (Indigo)
    colors[1] = ImVec4(0.70f, 0.45f, 0.98f, 0.7f); // Secondary Accent
    // colors[2] = ImVec4(0.12f, 0.10f, 0.18f, 1.0f); // Background

    refresh_audio_devices();
    load_config_from_ini();

    soundbank_name_ = "Broadcast Cues & FX";
    master_volume_ = 1.0f;
    any_playing_ = false;
    last_triggered_pad_ = 0;

    pads_ = {
        sound_pad_entry{1, "Applause", "Applause & Cheering (3.2s)", 3.2, false},
        sound_pad_entry{2, "Drum Roll", "Drum Roll & Cymbal Hit (4.5s)", 4.5, false},
        sound_pad_entry{3, "Air Horn", "Air Horn Stager (1.8s)", 1.8, false},
        sound_pad_entry{4, "Record Scratch", "Record Scratch (0.9s)", 0.9, false}
    };
}

void adlib_card::on_close() {
    auto& engine = rouen::helpers::AdLibEngine::instance();
    if (engine.is_active()) {
        engine.stop();
    }
}

void adlib_card::refresh_audio_devices() {
    audio_devices_ = rouen::helpers::AudioCapture::get_input_devices();
}

static std::string get_adlib_config_file() {
    const char* home = std::getenv("HOME");
    std::filesystem::path const dir = home ? std::filesystem::path(home) / ".config" / "rouen" : std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / "adlib_card.json").string();
}

void adlib_card::load_config_from_ini() {
    std::string intro, bg, outro, output, mic_name, trans_str;
    int mode = 0;

    std::string const cfg_file = get_adlib_config_file();
    std::ifstream ifs(cfg_file);
    if (ifs.is_open()) {
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto extract_json = [&](const std::string& key) -> std::string {
            auto pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            auto q1 = content.find('"', pos + key.length() + 2);
            if (q1 == std::string::npos) return "";
            auto q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return content.substr(q1 + 1, q2 - q1 - 1);
        };
        intro = extract_json("intro_video_path");
        bg = extract_json("background_path");
        outro = extract_json("outro_video_path");
        output = extract_json("output_mp4_path");
        mic_name = extract_json("mic_device_name");
        std::string const mode_str = extract_json("mode");
        if (!mode_str.empty()) {
            try { mode = std::stoi(mode_str); } catch (...) { /* ignore conversion error */ }
        }
        trans_str = extract_json("stage2_transition_seconds");
        if (!trans_str.empty()) {
            try { stage2_transition_seconds_ = std::stof(trans_str); } catch (...) { /* ignore conversion error */ }
        }
    }

    auto cfg = rouen::helpers::ConfigService::instance();
    if (intro.empty()) intro = cfg->get_env_optional("ROUEN_ADLIB_INTRO_PATH").value_or("");
    if (bg.empty()) bg = cfg->get_env_optional("ROUEN_ADLIB_BG_PATH").value_or("");
    if (outro.empty()) outro = cfg->get_env_optional("ROUEN_ADLIB_OUTRO_PATH").value_or("");
    if (output.empty()) output = cfg->get_env_optional("ROUEN_ADLIB_OUTPUT_PATH").value_or("");
    if (mic_name.empty()) mic_name = cfg->get_env_optional("ROUEN_ADLIB_MIC_NAME").value_or("");
    if (trans_str.empty()) {
        auto env_trans = cfg->get_env_optional("ROUEN_ADLIB_STAGE2_TRANSITION_SECONDS");
        if (env_trans.has_value()) {
            try { stage2_transition_seconds_ = std::stof(*env_trans); } catch (...) { /* ignore conversion error */ }
        }
    }

    std::strncpy(intro_path_buf_, intro.c_str(), sizeof(intro_path_buf_) - 1);
    std::strncpy(bg_path_buf_, bg.c_str(), sizeof(bg_path_buf_) - 1);
    std::strncpy(outro_path_buf_, outro.c_str(), sizeof(outro_path_buf_) - 1);
    std::strncpy(output_path_buf_, output.c_str(), sizeof(output_path_buf_) - 1);
    selected_mode_ = mode;

    refresh_audio_devices();
    selected_mic_idx_ = 0;
    if (!mic_name.empty()) {
        for (int i = 0; i < static_cast<int>(audio_devices_.size()); ++i) {
            if (audio_devices_[static_cast<size_t>(i)].name == mic_name) {
                selected_mic_idx_ = i;
                break;
            }
        }
    }
}

void adlib_card::save_config_to_ini() {
    auto cfg = rouen::helpers::ConfigService::instance();
    cfg->set_env_value("ROUEN_ADLIB_INTRO_PATH", intro_path_buf_, true);
    cfg->set_env_value("ROUEN_ADLIB_BG_PATH", bg_path_buf_, true);
    cfg->set_env_value("ROUEN_ADLIB_OUTRO_PATH", outro_path_buf_, true);
    cfg->set_env_value("ROUEN_ADLIB_OUTPUT_PATH", output_path_buf_, true);
    cfg->set_env_value("ROUEN_ADLIB_MODE", std::to_string(selected_mode_), true);
    cfg->set_env_value("ROUEN_ADLIB_STAGE2_TRANSITION_SECONDS", std::to_string(stage2_transition_seconds_), true);

    std::string mic_name = "";
    if (selected_mic_idx_ >= 0 && static_cast<size_t>(selected_mic_idx_) < audio_devices_.size()) {
        mic_name = audio_devices_[static_cast<size_t>(selected_mic_idx_)].name;
        cfg->set_env_value("ROUEN_ADLIB_MIC_NAME", mic_name, true);
    }

    std::string const cfg_file = get_adlib_config_file();
    std::ofstream ofs(cfg_file);
    if (ofs.is_open()) {
        ofs << std::format(R"({{
  "intro_video_path": "{}",
  "background_path": "{}",
  "outro_video_path": "{}",
  "output_mp4_path": "{}",
  "mic_device_name": "{}",
  "mode": "{}",
  "stage2_transition_seconds": "{:.1f}"
}}
)", intro_path_buf_, bg_path_buf_, outro_path_buf_, output_path_buf_, mic_name, selected_mode_, stage2_transition_seconds_);
    }
}

bool adlib_card::validate_config(std::string& out_error_msg) {
    out_error_msg.clear();

    // 1. Validate Intro Video path if supplied
    if (std::strlen(intro_path_buf_) > 0) {
        if (!std::filesystem::exists(intro_path_buf_)) {
            out_error_msg = std::format("Intro video file not found: {}", intro_path_buf_);
            return false;
        }
    }

    // 2. Validate Background Image path if supplied
    if (std::strlen(bg_path_buf_) > 0) {
        if (!std::filesystem::exists(bg_path_buf_)) {
            out_error_msg = std::format("Background image file not found: {}", bg_path_buf_);
            return false;
        }
    }

    // 3. Validate Outro Video path if supplied
    if (std::strlen(outro_path_buf_) > 0) {
        if (!std::filesystem::exists(outro_path_buf_)) {
            out_error_msg = std::format("Transition-Out video file not found: {}", outro_path_buf_);
            return false;
        }
    }

    // 4. Validate Recorded Mode settings
    if (selected_mode_ == 1) { // Recorded mode
        if (std::strlen(output_path_buf_) == 0) {
            out_error_msg = "Please specify an output target MP4 file path for Recorded Mode.";
            return false;
        }

        std::filesystem::path const p(output_path_buf_);
        if (p.has_parent_path() && !p.parent_path().empty()) {
            if (!std::filesystem::exists(p.parent_path())) {
                out_error_msg = std::format("Target output folder does not exist: {}", p.parent_path().string());
                return false;
            }
        }
    }

    return true;
}

bool adlib_card::render() {
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
}

bool adlib_card::render(rouen::ui::ui_context& ui) {
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
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        render_content();
    });
}

void adlib_card::render_content() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));

    draw_execution_deck();
    ImGui::Separator();
    draw_template_config();

    ImGui::PopStyleVar(2);
}

void adlib_card::draw_execution_deck() {
    auto& engine = rouen::helpers::AdLibEngine::instance();
    auto stage = engine.get_stage();
    bool const is_active = engine.is_active();
    bool const is_paused = engine.is_paused();
    bool const is_recording = engine.is_recording();

    // Stage Badge & Title
    ImGui::TextColored(colors[0], "%s Ad-Lib Production Control", ICON_MD_VIDEO_CAMERA_FRONT);
    ImGui::SameLine(ImGui::GetWindowWidth() - 210.0f);

    const char* stage_str = "IDLE";
    ImVec4 badge_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    if (stage == rouen::helpers::AdLibStage::Prepared) {
        stage_str = "PREPARED (PAUSED)";
        badge_color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
    } else if (stage == rouen::helpers::AdLibStage::Intro) {
        stage_str = "STAGE 1: INTRO (AUTO-NEXT)";
        badge_color = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
    } else if (stage == rouen::helpers::AdLibStage::Middle) {
        stage_str = (engine.get_auto_stop_seconds() > 0.0) ? "STAGE 2: PRESENTATION (AUTO)" : "STAGE 2: PRESENTATION (MANUAL)";
        badge_color = ImVec4(0.9f, 0.2f, 0.3f, 1.0f);
    } else if (stage == rouen::helpers::AdLibStage::Outro) {
        stage_str = "STAGE 3: OUTRO (AUTO-STOP)";
        badge_color = ImVec4(0.9f, 0.6f, 0.1f, 1.0f);
    } else if (stage == rouen::helpers::AdLibStage::Finished) {
        stage_str = "FINISHED";
        badge_color = ImVec4(0.3f, 0.8f, 0.4f, 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, badge_color);
    ImGui::Button(stage_str, ImVec2(190, 0));
    ImGui::PopStyleColor();

    if (!validation_error_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped(ICON_MD_ERROR " %s", validation_error_.c_str());
        ImGui::PopStyleColor();
    }

    if (is_active) {
        double const elapsed = engine.get_elapsed_seconds();
        int const mins = static_cast<int>(elapsed) / 60;
        int const secs = static_cast<int>(elapsed) % 60;
        if (stage == rouen::helpers::AdLibStage::Middle) {
            double const stage_elapsed = engine.get_stage_elapsed_seconds();
            double const target_dur = engine.get_auto_stop_seconds();
            if (target_dur > 0.0) {
                ImGui::Text(ICON_MD_TIMER " Stage 2: %.1f / %.1f s (Total: %02d:%02d)", stage_elapsed, target_dur, mins, secs);
            } else {
                ImGui::Text(ICON_MD_TIMER " Stage 2: %.1f s (Manual Mode) (Total: %02d:%02d)", stage_elapsed, mins, secs);
            }
        } else {
            ImGui::Text(ICON_MD_TIMER " Elapsed Time: %02d:%02d", mins, secs);
        }

        if (is_recording) {
            ImGui::SameLine(250.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_MD_FIBER_MANUAL_RECORD " REC");

            // Mic VU Peak Meter
            float const mic_peak = engine.get_mic_peak();
            ImGui::SameLine(320.0f);
            ImGui::Text(ICON_MD_MIC " Mic:");
            ImGui::SameLine();
            ImGui::ProgressBar(mic_peak, ImVec2(100, 16), "");
        } else if (stage == rouen::helpers::AdLibStage::Prepared) {
            ImGui::SameLine(250.0f);
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.1f, 1.0f), ICON_MD_PREVIEW " Prepared (Paused)");
        }
    }

    // Direct Stage 2 Transition Settings on Execution Deck
    ImGui::Spacing();
    ImGui::Text(ICON_MD_TIMER " Stage 2 Transition:");
    ImGui::SameLine();
    bool const is_manual_deck = (stage2_transition_seconds_ <= 0.0f);
    if (ImGui::RadioButton("Manual##deck", is_manual_deck)) {
        stage2_transition_seconds_ = 0.0f;
        save_config_to_ini();
        engine.set_auto_stop_seconds(0.0);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Timed##deck", !is_manual_deck)) {
        if (stage2_transition_seconds_ <= 0.0f) {
            stage2_transition_seconds_ = 10.0f;
        }
        save_config_to_ini();
        engine.set_auto_stop_seconds(static_cast<double>(stage2_transition_seconds_));
    }
    if (stage2_transition_seconds_ > 0.0f) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputFloat("sec##deck_trans_val", &stage2_transition_seconds_, 1.0f, 5.0f, "%.1f s")) {
            stage2_transition_seconds_ = std::max(stage2_transition_seconds_, 0.5f);
            save_config_to_ini();
            engine.set_auto_stop_seconds(static_cast<double>(stage2_transition_seconds_));
        }
    }

    ImGui::Spacing();

    // Transport Action Buttons
    if (stage == rouen::helpers::AdLibStage::Idle) {
        if (ImGui::Button(ICON_MD_SETTINGS " Prepare Scene & Open Detached Window", ImVec2(320, 36))) {
            if (std::strlen(output_path_buf_) > 0) {
                selected_mode_ = 1; // Recorded mode
            }
            save_config_to_ini();
            if (validate_config(validation_error_)) {
                rouen::helpers::AdLibConfig cfg;
                cfg.intro_video_path = intro_path_buf_;
                cfg.background_path = bg_path_buf_;
                cfg.outro_video_path = outro_path_buf_;
                cfg.output_mp4_path = output_path_buf_;
                cfg.mode = (selected_mode_ == 0) ? rouen::helpers::AdLibMode::Live : rouen::helpers::AdLibMode::Recorded;
                cfg.presentation_duration_seconds = static_cast<double>(stage2_transition_seconds_);
                if (audio_devices_.empty()) {
                    refresh_audio_devices();
                }
                if (selected_mic_idx_ >= 0 && static_cast<size_t>(selected_mic_idx_) < audio_devices_.size()) {
                    cfg.mic_device_id = audio_devices_[static_cast<size_t>(selected_mic_idx_)].id;
                    cfg.mic_device_name = audio_devices_[static_cast<size_t>(selected_mic_idx_)].name;
                }
                engine.prepare(cfg);
            }
        }
    } else if (stage == rouen::helpers::AdLibStage::Prepared) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_MD_PLAY_CIRCLE_FILLED " Go Live / Begin Ad-Lib", ImVec2(220, 36))) {
            engine.start();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button(ICON_MD_STOP " Reset", ImVec2(100, 36))) {
            engine.stop();
        }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button(is_paused ? ICON_MD_PLAY_ARROW " Resume" : ICON_MD_PAUSE " Pause", ImVec2(130, 36))) {
            engine.toggle_pause();
        }
        ImGui::SameLine();

        if (ImGui::Button(ICON_MD_SKIP_NEXT " Next Stage", ImVec2(140, 36))) {
            engine.next_stage();
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button(ICON_MD_STOP " Stop / Finish", ImVec2(140, 36))) {
            engine.stop();
        }
        ImGui::PopStyleColor();
    }
}

void adlib_card::draw_template_config() {
    ImGui::TextColored(colors[0], ICON_MD_SETTINGS " Template Configuration");

    // Mode Selector
    ImGui::Text("Operation Mode:");
    if (ImGui::RadioButton("Live Mode (Zoom / Teams Detached Window Share)", &selected_mode_, 0)) {
        save_config_to_ini();
    }
    if (ImGui::RadioButton("Recorded Mode (MP4 Writing Pipeline + Mic Audio)", &selected_mode_, 1)) {
        save_config_to_ini();
    }
    ImGui::Spacing();

    // Intro Video File
    bool const intro_error = std::strlen(intro_path_buf_) > 0 && !std::filesystem::exists(intro_path_buf_);
    ImGui::Text(ICON_MD_MOVIE " Intro Video File (Stage 1):");
    if (intro_error) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.1f, 0.1f, 0.6f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::InputText("##intro_path", intro_path_buf_, sizeof(intro_path_buf_))) {
        save_config_to_ini();
        validation_error_.clear();
    }
    if (intro_error) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Browse##intro")) {
        std::string const selected = rouen::platform::select_file_dialog("Select Intro Video", "mp4;mov;mkv;avi");
        if (!selected.empty()) {
            std::strncpy(intro_path_buf_, selected.c_str(), sizeof(intro_path_buf_) - 1);
            save_config_to_ini();
            validation_error_.clear();
        }
    }

    // Fixed Background Image
    bool const bg_error = std::strlen(bg_path_buf_) > 0 && !std::filesystem::exists(bg_path_buf_);
    ImGui::Text(ICON_MD_IMAGE " Fixed Background Image (Stage 2):");
    if (bg_error) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.1f, 0.1f, 0.6f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::InputText("##bg_path", bg_path_buf_, sizeof(bg_path_buf_))) {
        save_config_to_ini();
        validation_error_.clear();
    }
    if (bg_error) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Browse##bg")) {
        std::string const selected = rouen::platform::select_file_dialog("Select Background Image", "png;jpg;jpeg;bmp");
        if (!selected.empty()) {
            std::strncpy(bg_path_buf_, selected.c_str(), sizeof(bg_path_buf_) - 1);
            save_config_to_ini();
            validation_error_.clear();
        }
    }

    // Outro Video File
    bool const outro_error = std::strlen(outro_path_buf_) > 0 && !std::filesystem::exists(outro_path_buf_);
    ImGui::Text(ICON_MD_MOVIE " Transition-Out Video File (Stage 3):");
    if (outro_error) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.1f, 0.1f, 0.6f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::InputText("##outro_path", outro_path_buf_, sizeof(outro_path_buf_))) {
        save_config_to_ini();
        validation_error_.clear();
    }
    if (outro_error) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Browse##outro")) {
        std::string const selected = rouen::platform::select_file_dialog("Select Outro Video", "mp4;mov;mkv;avi");
        if (!selected.empty()) {
            std::strncpy(outro_path_buf_, selected.c_str(), sizeof(outro_path_buf_) - 1);
            save_config_to_ini();
            validation_error_.clear();
        }
    }

    // Microphone & Output MP4 Configuration (Recorded Mode Only)
    if (selected_mode_ == 1) {
        ImGui::Spacing();
        ImGui::Text(ICON_MD_MIC " Microphone Audio Input (Stage 2 Presentation):");
        if (audio_devices_.empty()) {
            refresh_audio_devices();
        }

        std::string current_mic_label = "Default System Microphone";
        if (selected_mic_idx_ >= 0 && static_cast<size_t>(selected_mic_idx_) < audio_devices_.size()) {
            current_mic_label = audio_devices_[static_cast<size_t>(selected_mic_idx_)].name;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        if (ImGui::BeginCombo("##mic_combo", current_mic_label.c_str())) {
            for (int i = 0; i < static_cast<int>(audio_devices_.size()); ++i) {
                bool const is_selected = (selected_mic_idx_ == i);
                if (ImGui::Selectable(audio_devices_[static_cast<size_t>(i)].name.c_str(), is_selected)) {
                    selected_mic_idx_ = i;
                    save_config_to_ini();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            refresh_audio_devices();
        }

        bool const output_error = (std::strlen(output_path_buf_) == 0);
        ImGui::Text(ICON_MD_FOLDER " Output MP4 Filename / Target Path:");
        if (output_error) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.1f, 0.1f, 0.6f));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
        if (ImGui::InputText("##output_path", output_path_buf_, sizeof(output_path_buf_))) {
            save_config_to_ini();
            validation_error_.clear();
        }
        if (output_error) ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Browse##output")) {
            std::string const selected = rouen::platform::save_file_dialog("Save Recorded MP4 As", "mp4");
            if (!selected.empty()) {
                std::strncpy(output_path_buf_, selected.c_str(), sizeof(output_path_buf_) - 1);
                save_config_to_ini();
                validation_error_.clear();
            }
        }
    }
}

std::string adlib_card::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // 1. Header Container
    glz::json_t header;
    header["type"] = "Container";
    header["style"] = "emphasis";

    glz::json_t col_set;
    col_set["type"] = "ColumnSet";
    std::vector<glz::json_t> columns;

    // Column 1: Emoji icon
    glz::json_t icon_col;
    icon_col["type"] = "Column";
    icon_col["width"] = "auto";
    std::vector<glz::json_t> icon_items;
    glz::json_t icon_tb;
    icon_tb["type"] = "TextBlock";
    icon_tb["text"] = "🎛️";
    icon_tb["size"] = "Large";
    icon_items.push_back(std::move(icon_tb));
    icon_col["items"] = std::move(icon_items);
    columns.push_back(std::move(icon_col));

    // Column 2: Title & Soundbank subtitle
    glz::json_t title_col;
    title_col["type"] = "Column";
    title_col["width"] = "stretch";
    std::vector<glz::json_t> title_items;
    glz::json_t title_tb;
    title_tb["type"] = "TextBlock";
    title_tb["text"] = "Ad-Lib Broadcast Soundboard";
    title_tb["weight"] = "Bolder";
    title_tb["size"] = "Large";
    title_items.push_back(std::move(title_tb));

    glz::json_t bank_tb;
    bank_tb["type"] = "TextBlock";
    bank_tb["text"] = "Soundbank: " + soundbank_name_;
    bank_tb["isSubtle"] = true;
    bank_tb["size"] = "Small";
    title_items.push_back(std::move(bank_tb));

    title_col["items"] = std::move(title_items);
    columns.push_back(std::move(title_col));

    // Column 3: Badge "16 PADS"
    glz::json_t count_col;
    count_col["type"] = "Column";
    count_col["width"] = "auto";
    std::vector<glz::json_t> count_items;
    glz::json_t count_tb;
    count_tb["type"] = "TextBlock";
    count_tb["text"] = "16 PADS";
    count_tb["weight"] = "Bolder";
    count_tb["color"] = "Accent";
    count_tb["size"] = "Small";
    count_items.push_back(std::move(count_tb));
    count_col["items"] = std::move(count_items);
    columns.push_back(std::move(count_col));

    col_set["columns"] = std::move(columns);
    std::vector<glz::json_t> header_items;
    header_items.push_back(std::move(col_set));
    header["items"] = std::move(header_items);
    body.push_back(std::move(header));

    // 2. Soundbank Preset Selector Container
    glz::json_t bank_container;
    bank_container["type"] = "Container";
    std::vector<glz::json_t> bank_items;
    glz::json_t choice_set;
    choice_set["type"] = "Input.ChoiceSet";
    choice_set["id"] = "bank";
    choice_set["label"] = "Soundbank Preset";
    choice_set["value"] = soundbank_name_;
    std::vector<glz::json_t> choices;
    choices.push_back(glz::json_t::object_t{{"title", "Broadcast Cues & FX"}, {"value", "Broadcast Cues & FX"}});
    choices.push_back(glz::json_t::object_t{{"title", "Audience Reactions"}, {"value", "Audience Reactions"}});
    choices.push_back(glz::json_t::object_t{{"title", "Music Stingers & Beds"}, {"value", "Music Stingers & Beds"}});
    choice_set["choices"] = std::move(choices);
    bank_items.push_back(std::move(choice_set));
    bank_container["items"] = std::move(bank_items);
    body.push_back(std::move(bank_container));

    // 3. FactSet for soundboard pads
    glz::json_t fact_set;
    fact_set["type"] = "FactSet";
    std::vector<glz::json_t> facts;
    for (const auto& pad : pads_) {
        glz::json_t fact;
        fact["title"] = std::format("Pad {}:", pad.id);
        fact["value"] = pad.value;
        facts.push_back(std::move(fact));
    }
    fact_set["facts"] = std::move(facts);
    body.push_back(std::move(fact_set));

    card["body"] = std::move(body);

    // 4. Actions
    std::vector<glz::json_t> actions;
    auto add_pad_action = [&actions](const std::string& title, int pad_id) {
        glz::json_t a;
        a["type"] = "Action.Execute";
        a["title"] = title;
        a["verb"] = "adlib_trigger";
        glz::json_t data;
        data["pad"] = pad_id;
        a["data"] = std::move(data);
        actions.push_back(std::move(a));
    };

    if (pads_.size() >= 1) add_pad_action("Trigger Pad 1 (" + pads_[0].name + ")", pads_[0].id);
    if (pads_.size() >= 2) add_pad_action("Trigger Pad 2 (" + pads_[1].name + ")", pads_[1].id);
    if (pads_.size() >= 3) add_pad_action("Trigger Pad 3 (" + pads_[2].name + ")", pads_[2].id);

    glz::json_t stop_act;
    stop_act["type"] = "Action.Execute";
    stop_act["title"] = "Stop All Audio";
    stop_act["verb"] = "adlib_stop_all";
    actions.push_back(std::move(stop_act));

    card["actions"] = std::move(actions);

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void adlib_card::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

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

        if (verb == "adlib_trigger") {
            int pad_idx = 0;
            if (data.contains("pad")) {
                if (data["pad"].holds<double>()) {
                    pad_idx = static_cast<int>(data["pad"].get<double>());
                } else if (data["pad"].holds<std::string>()) {
                    try { pad_idx = std::stoi(data["pad"].get<std::string>()); } catch (...) {}
                }
            } else if (data.contains("pad_index")) {
                if (data["pad_index"].holds<double>()) {
                    pad_idx = static_cast<int>(data["pad_index"].get<double>());
                } else if (data["pad_index"].holds<std::string>()) {
                    try { pad_idx = std::stoi(data["pad_index"].get<std::string>()); } catch (...) {}
                }
            } else if (payload.contains("pad")) {
                if (payload["pad"].holds<double>()) {
                    pad_idx = static_cast<int>(payload["pad"].get<double>());
                } else if (payload["pad"].holds<std::string>()) {
                    try { pad_idx = std::stoi(payload["pad"].get<std::string>()); } catch (...) {}
                }
            } else if (payload.contains("pad_index")) {
                if (payload["pad_index"].holds<double>()) {
                    pad_idx = static_cast<int>(payload["pad_index"].get<double>());
                } else if (payload["pad_index"].holds<std::string>()) {
                    try { pad_idx = std::stoi(payload["pad_index"].get<std::string>()); } catch (...) {}
                }
            }

            if (pad_idx > 0) {
                last_triggered_pad_ = pad_idx;
                any_playing_ = true;
                for (auto& pad : pads_) {
                    if (pad.id == pad_idx) {
                        pad.is_playing = true;
                    }
                }
            }
        } else if (verb == "adlib_stop_all") {
            any_playing_ = false;
            for (auto& pad : pads_) {
                pad.is_playing = false;
            }
            auto& engine = rouen::helpers::AdLibEngine::instance();
            engine.stop();
        } else if (verb == "adlib_load_bank") {
            std::string bank;
            if (data.contains("bank") && data["bank"].holds<std::string>()) {
                bank = data["bank"].get<std::string>();
            } else if (data.contains("soundbank") && data["soundbank"].holds<std::string>()) {
                bank = data["soundbank"].get<std::string>();
            } else if (payload.contains("bank") && payload["bank"].holds<std::string>()) {
                bank = payload["bank"].get<std::string>();
            } else if (payload.contains("soundbank") && payload["soundbank"].holds<std::string>()) {
                bank = payload["soundbank"].get<std::string>();
            }

            if (!bank.empty()) {
                soundbank_name_ = bank;
                if (bank == "Audience Reactions") {
                    pads_ = {
                        sound_pad_entry{1, "Applause", "Enthusiastic Applause (4.0s)", 4.0, false},
                        sound_pad_entry{2, "Laughter", "Audience Laughter (3.5s)", 3.5, false},
                        sound_pad_entry{3, "Gasp", "Dramatic Gasp (1.5s)", 1.5, false},
                        sound_pad_entry{4, "Cheering", "Stadium Cheering (5.0s)", 5.0, false}
                    };
                } else if (bank == "Music Stingers & Beds") {
                    pads_ = {
                        sound_pad_entry{1, "Fanfare", "Brass Fanfare (3.0s)", 3.0, false},
                        sound_pad_entry{2, "Suspense", "Low Drone Suspense (6.0s)", 6.0, false},
                        sound_pad_entry{3, "Upbeat", "Upbeat Synth Stinger (2.5s)", 2.5, false},
                        sound_pad_entry{4, "Outro Bed", "Outro Acoustic Bed (8.0s)", 8.0, false}
                    };
                } else {
                    pads_ = {
                        sound_pad_entry{1, "Applause", "Applause & Cheering (3.2s)", 3.2, false},
                        sound_pad_entry{2, "Drum Roll", "Drum Roll & Cymbal Hit (4.5s)", 4.5, false},
                        sound_pad_entry{3, "Air Horn", "Air Horn Stager (1.8s)", 1.8, false},
                        sound_pad_entry{4, "Record Scratch", "Record Scratch (0.9s)", 0.9, false}
                    };
                }
                any_playing_ = false;
            }
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
        }

        last_adaptive_parse_time_ = {};
    } catch (const std::exception& e) {
        std::cerr << "[adlib] Action error: " << e.what() << std::endl;
    } catch (...) {
    }
}

} // namespace rouen::cards
