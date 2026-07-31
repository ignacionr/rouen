#include "adlib.hpp"
#include "../../helpers/platform_utils.hpp"
#include "IconsMaterialDesign.h"
#include "adlib_engine.hpp"
#include "audio_capture.hpp"
#include "config_service.hpp"
#include <cstdlib>
#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace rouen::cards {

adlib_card::adlib_card(std::string_view locator) {
    (void)locator;
    width = 640.0f;
    name("Ad-Lib Broadcasting Studio");

    // Vibrant Production Theme Colors (Indigo / Violet / Electric Cyan)
    colors[0] = ImVec4(0.55f, 0.35f, 0.95f, 1.0f); // Primary Accent (Indigo)
    colors[1] = ImVec4(0.70f, 0.45f, 0.98f, 0.7f); // Secondary Accent
    get_color(2, ImVec4(0.12f, 0.10f, 0.18f, 1.0f)); // Background

    refresh_audio_devices();
    load_config_from_ini();
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
        ofs << "{\n"
            << "  \"intro_video_path\": \"" << intro_path_buf_ << "\",\n"
            << "  \"background_path\": \"" << bg_path_buf_ << "\",\n"
            << "  \"outro_video_path\": \"" << outro_path_buf_ << "\",\n"
            << "  \"output_mp4_path\": \"" << output_path_buf_ << "\",\n"
            << "  \"mic_device_name\": \"" << mic_name << "\",\n"
            << "  \"mode\": \"" << std::to_string(selected_mode_) << "\",\n"
            << "  \"stage2_transition_seconds\": \"" << std::to_string(stage2_transition_seconds_) << "\"\n"
            << "}\n";
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
    return render_window([this]() {
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

} // namespace rouen::cards
