#pragma once

#include "../../helpers/imgui_include.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <filesystem>
#include <chrono>
#include <future>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/string_helper.hpp"
#include "../interface/card.hpp"

namespace fs = std::filesystem;

namespace rouen::cards::productivity {

struct voice_note {
    int id = 0;
    std::string title;
    std::string transcription;
    std::string audio_path;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::vector<std::string> tags;
    bool is_recording = false;
    double duration = 0.0;
    std::string language = "en"; // Default to English
};

class voice_notes : public card {
public:
    voice_notes();
    ~voice_notes() override;

    bool render() override;
    std::string get_uri() const override { return "voice-notes"; }

private:
    // Database operations
    void initialize_database();
    void save_note(const voice_note& note);
    void load_notes();
    void delete_note(int id);
    void update_note(const voice_note& note);

    // Audio recording
    void start_recording();
    void stop_recording();
    void process_recording(const std::string& audio_file);

    // Transcription
    void transcribe_audio(const std::string& audio_file, voice_note& note);
    void run_whisper_transcribe(const std::string& audio_file);

    // UI rendering
    void render_recording_controls();
    void render_notes_list();
    void render_note_editor(voice_note& note);
    void render_search_bar();
    void render_meeting_mode();

    // Playback
    void play_audio(const std::string& audio_path);
    void stop_audio();
    void open_in_finder(const std::string& file_path);

    // Utility functions
    std::string generate_audio_filename() const;
    std::string format_timestamp(const std::chrono::system_clock::time_point& time) const;
    std::string format_duration(double seconds) const;
    void add_tag_to_note(voice_note& note, const std::string& tag);
    // AVFoundation device helpers (macOS)
    std::string avf_device_spec() const;           // returns ":<index>"
    const char* avf_device_label(int idx) const;   // returns human label

    // State
    std::vector<voice_note> notes_;
    std::string search_query_;
    int selected_note_id_ = -1;
    bool is_recording_ = false;
    bool meeting_mode_ = false;
    std::string current_recording_path_;
    std::chrono::system_clock::time_point recording_start_time_;
    int avf_device_index_ = 2; // Default to USB Microphone on macOS (:2)

    // Threading
    std::thread recording_thread_;
    std::thread transcription_thread_;
    std::atomic<bool> recording_active_;
    std::mutex notes_mutex_;
    int recording_process_id_ = -1;  // Store the recording process ID for proper termination

    // Database
    std::unique_ptr<hosting::db::sqlite> db_;

    // Audio playback
    std::unique_ptr<media_player> audio_player_;

    // Whisper process
    std::string whisper_output_;

    // UI state
    char title_buffer_[256] = {0};
    char tag_buffer_[64] = {0};
    bool show_meeting_mode_ = false;
    float playback_speed_ = 1.0f;
};

} // namespace rouen::cards::productivity
