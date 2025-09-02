#include "voice_notes.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/debug.hpp"
#include "../../registrar.hpp"
#include <SDL.h>
#include <format>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace rouen::cards::productivity {

voice_notes::voice_notes() {
    // Initialize colors
    colors[0] = ImVec4(0.1f, 0.2f, 0.4f, 0.9f);       // window background
    colors[1] = ImVec4(0.05f, 0.1f, 0.2f, 0.8f);      // secondary elements
    colors[2] = ImVec4(0.2f, 0.3f, 0.5f, 0.8f);       // recording indicator
    colors[3] = ImVec4(0.8f, 0.2f, 0.2f, 0.9f);       // stop button
    colors[4] = ImVec4(0.2f, 0.8f, 0.2f, 0.9f);       // play button
    colors[5] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);       // text
    colors[6] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);       // secondary text
    colors[7] = ImVec4(0.3f, 0.3f, 0.3f, 0.7f);       // input background
    colors[8] = ImVec4(0.4f, 0.4f, 0.6f, 0.8f);       // button normal
    colors[9] = ImVec4(0.5f, 0.5f, 0.7f, 0.9f);       // button hover
    colors[10] = ImVec4(0.6f, 0.6f, 0.8f, 1.0f);      // button active

    requested_fps = 30; // Higher FPS for responsive recording UI

    // Initialize database
    initialize_database();

    // Load existing notes
    load_notes();

    // Initialize audio player
    audio_player_ = std::make_unique<media_player>();
}

voice_notes::~voice_notes() {
    // Stop any ongoing recording
    if (recording_active_) {
        recording_active_ = false;
        if (recording_thread_.joinable()) {
            recording_thread_.join();
        }
    }

    // Terminate any running recording process
    if (recording_process_id_ > 0) {
        std::string kill_command = std::format("kill {} 2>/dev/null || true", recording_process_id_);
        system(kill_command.c_str());
        SYS_INFO_FMT("Terminated recording process {} in destructor", recording_process_id_);
        recording_process_id_ = -1;
    }

    // Stop any ongoing transcription
    if (transcription_thread_.joinable()) {
        transcription_thread_.join();
    }

    // Stop audio playback
    stop_audio();
}

void voice_notes::initialize_database() {
    try {
        // Create data directory if it doesn't exist
        fs::path data_dir = rouen::platform::get_user_data_path() / "voice_notes";
        fs::create_directories(data_dir);

        // Initialize database
        fs::path db_path = data_dir / "voice_notes.db";
        db_ = std::make_unique<hosting::db::sqlite>(db_path.string());

        // Create tables
        db_->exec(R"(
            CREATE TABLE IF NOT EXISTS voice_notes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                transcription TEXT,
                audio_path TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                duration REAL DEFAULT 0.0,
                language TEXT DEFAULT 'en'
            )
        )");

        db_->exec(R"(
            CREATE TABLE IF NOT EXISTS voice_note_tags (
                note_id INTEGER,
                tag TEXT,
                FOREIGN KEY (note_id) REFERENCES voice_notes (id) ON DELETE CASCADE,
                PRIMARY KEY (note_id, tag)
            )
        )");

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to initialize voice notes database: {}", e.what());
    }
}

void voice_notes::save_note(const voice_note& note) {
    if (!db_) {
        SYS_ERROR("Database not initialized");
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(notes_mutex_);

        SYS_INFO_FMT("Saving voice note: id={}, title='{}', audio_path='{}', duration={}", note.id, note.title, note.audio_path, note.duration);

        if (note.id == 0) {
            SYS_INFO("Performing INSERT operation");
            // Insert new note
            std::string sql = R"(
                INSERT INTO voice_notes (title, transcription, audio_path, duration, language)
                VALUES (?, ?, ?, ?, ?)
            )";
            db_->exec(sql, {}, note.title, note.transcription, note.audio_path, note.duration, note.language);

            // Get the inserted ID
            long long new_id = db_->last_insert_rowid();
            SYS_INFO_FMT("INSERT completed, new ID: {}", new_id);

            // Save tags
            for (const auto& tag : note.tags) {
                std::string tag_sql = R"(
                    INSERT INTO voice_note_tags (note_id, tag) VALUES (?, ?)
                )";
                db_->exec(tag_sql, {}, new_id, tag);
            }
        } else {
            SYS_INFO("Performing UPDATE operation");
            // Update existing note
            std::string sql = R"(
                UPDATE voice_notes
                SET title = ?, transcription = ?, audio_path = ?, updated_at = CURRENT_TIMESTAMP,
                    duration = ?, language = ?
                WHERE id = ?
            )";
            db_->exec(sql, {}, note.title, note.transcription, note.audio_path, note.duration, note.language, note.id);

            // Update tags (delete old, insert new)
            std::string delete_sql = "DELETE FROM voice_note_tags WHERE note_id = ?";
            db_->exec(delete_sql, {}, note.id);

            for (const auto& tag : note.tags) {
                std::string tag_sql = R"(
                    INSERT INTO voice_note_tags (note_id, tag) VALUES (?, ?)
                )";
                db_->exec(tag_sql, {}, note.id, tag);
            }
        }

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to save voice note: {}", e.what());
    }
}

void voice_notes::load_notes() {
    if (!db_) return;

    try {
        std::lock_guard<std::mutex> lock(notes_mutex_);

        notes_.clear();

        std::string sql = R"(
            SELECT n.id, n.title, n.transcription, n.audio_path, n.created_at, n.updated_at,
                   n.duration, n.language
            FROM voice_notes n
            ORDER BY n.created_at DESC
        )";

        // First, load all notes without tags
        std::vector<voice_note> temp_notes;
        db_->exec(sql, [&temp_notes](sqlite3_stmt *stmt) {
            voice_note note;
            note.id = sqlite3_column_int(stmt, 0);
            
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            note.title = title ? title : "";
            
            const char* transcription = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            note.transcription = transcription ? transcription : "";
            
            const char* audio_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            note.audio_path = audio_path ? audio_path : "";
            
            // For now, use current time (timestamps will be properly parsed later)
            note.created_at = std::chrono::system_clock::now();
            note.updated_at = std::chrono::system_clock::now();
            
            note.duration = sqlite3_column_double(stmt, 6);
            
            const char* language = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            note.language = language ? language : "en";

            temp_notes.push_back(note);
        });

        // Now load tags for each note (outside the main query callback to avoid deadlock)
        for (auto& note : temp_notes) {
            std::string tag_sql = "SELECT tag FROM voice_note_tags WHERE note_id = ?";
            db_->exec(tag_sql, [&note](sqlite3_stmt *tag_stmt) {
                const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(tag_stmt, 0));
                if (tag) {
                    note.tags.push_back(tag);
                }
            }, note.id);
        }

        // Move temp_notes to notes_
        notes_ = std::move(temp_notes);

        SYS_INFO_FMT("Loaded {} voice notes from database", notes_.size());

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to load voice notes: {}", e.what());
    }
}

void voice_notes::delete_note(int id) {
    if (!db_) return;

    try {
        std::lock_guard<std::mutex> lock(notes_mutex_);

        // Delete from database
        std::string sql = "DELETE FROM voice_notes WHERE id = ?";
        db_->exec(sql, {}, id);

        // Remove from local list
        notes_.erase(
            std::remove_if(notes_.begin(), notes_.end(),
                [id](const voice_note& note) { return note.id == id; }),
            notes_.end()
        );

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to delete voice note: {}", e.what());
    }
}

void voice_notes::start_recording() {
    if (is_recording_) return;

    // Join any existing recording thread before starting a new one
    if (recording_thread_.joinable()) {
        recording_thread_.join();
    }

    try {
        is_recording_ = true;
        recording_active_ = true;
        current_recording_path_ = generate_audio_filename();
        recording_start_time_ = std::chrono::system_clock::now();

        SYS_INFO_FMT("Starting recording to: {}", current_recording_path_);

        // Start recording thread
        recording_thread_ = std::thread([this]() {
            SYS_INFO("Recording thread started");
            
            // For macOS, we'll simulate recording for now since 'rec' might not be available
            // In a real implementation, you'd use AVFoundation or similar
            std::string command;
            bool use_dummy_recording = false;
            
            #ifdef __linux__
                command = std::format("arecord -f cd -t wav -d 0 {} & echo $!", current_recording_path_);
            #elif defined(__APPLE__)
                // Try using ffmpeg if available (preferred), otherwise sox, otherwise dummy recording
                if (system("which ffmpeg >/dev/null 2>&1") == 0 || system("/opt/homebrew/bin/ffmpeg -version >/dev/null 2>&1") == 0) {
                    // Use ffmpeg for recording on macOS - try multiple audio devices
                    // Try system ffmpeg first, then Homebrew ffmpeg
                    std::string ffmpeg_path = (system("which ffmpeg >/dev/null 2>&1") == 0) ? "ffmpeg" : "/opt/homebrew/bin/ffmpeg";

                    // Try different audio devices in order of preference
                    // Device :1 is QCY H3 headphones, :2 is USB Microphone, :0 is system default
                    std::string selected_device = ":1"; // Try QCY H3 headphones
                    SYS_INFO_FMT("Using audio device: {}", selected_device);
                    command = std::format("{} -f avfoundation -i \"{}\" -acodec pcm_s16le -ar 16000 -ac 1 -y {} 2>/dev/null & echo $!", ffmpeg_path, selected_device, current_recording_path_);
                } else if (system("which sox >/dev/null 2>&1") == 0) {
                    command = std::format("sox -d {} & echo $!", current_recording_path_);
                } else {
                    use_dummy_recording = true;
                    command = "echo 'Using dummy recording - no audio tools available'";
                }
            #else
                // Windows - use PowerShell to record
                command = std::format("powershell -Command \"Start-Job -ScriptBlock { param($path); $recorder = New-Object -ComObject 'WMPlayer.OCX'; $recorder.URL = $null; $recorder.controls.stop(); Start-Sleep -Milliseconds 100; $recorder = $null; } -ArgumentList '{}'\"", current_recording_path_);
            #endif

            SYS_INFO_FMT("Recording command: {}", command);

            if (use_dummy_recording) {
                // Create a dummy file with silence - NOT actual recording
                std::ofstream dummy_file(current_recording_path_, std::ios::binary);
                if (dummy_file) {
                    // Generate silence (all zeros) instead of fake audio
                    const int sample_rate = 16000;
                    const int duration_seconds = 2;
                    const int num_samples = sample_rate * duration_seconds;
                    const int data_size = num_samples * sizeof(int16_t);  // 2 bytes per sample
                    
                    // WAV header for silence
                    unsigned char wav_header[] = {
                        'R', 'I', 'F', 'F',  // RIFF
                        0x00, 0x00, 0x00, 0x00,  // File size (to be calculated)
                        'W', 'A', 'V', 'E',  // WAVE
                        'f', 'm', 't', ' ',  // fmt
                        0x10, 0x00, 0x00, 0x00,  // Format chunk size (16)
                        0x01, 0x00,  // Audio format (PCM)
                        0x01, 0x00,  // Channels (mono)
                        0x80, 0x3E, 0x00, 0x00,  // Sample rate (16000)
                        0x00, 0x7D, 0x00, 0x00,  // Byte rate (32000)
                        0x02, 0x00,  // Block align (2)
                        0x10, 0x00,  // Bits per sample (16)
                        'd', 'a', 't', 'a',  // Data chunk
                        0x00, 0x00, 0x00, 0x00   // Data size (to be calculated)
                    };
                    
                    // Calculate file sizes
                    const int header_size = sizeof(wav_header);
                    const int file_size = header_size + data_size - 8;  // RIFF chunk size excludes first 8 bytes
                    
                    // Update file size in header (little-endian)
                    wav_header[4] = (file_size >> 0) & 0xFF;
                    wav_header[5] = (file_size >> 8) & 0xFF;
                    wav_header[6] = (file_size >> 16) & 0xFF;
                    wav_header[7] = (file_size >> 24) & 0xFF;
                    
                    // Update data size in header (little-endian)
                    wav_header[40] = (data_size >> 0) & 0xFF;
                    wav_header[41] = (data_size >> 8) & 0xFF;
                    wav_header[42] = (data_size >> 16) & 0xFF;
                    wav_header[43] = (data_size >> 24) & 0xFF;
                    
                    // Write header
                    dummy_file.write(reinterpret_cast<const char*>(wav_header), sizeof(wav_header));
                    
                    // Generate silence (all zeros) - NOT a tone, just empty audio
                    for (int i = 0; i < num_samples; ++i) {
                        int16_t silence_sample = 0;  // Silence
                        dummy_file.write(reinterpret_cast<const char*>(&silence_sample), sizeof(silence_sample));
                    }
                    
                    dummy_file.close();
                    SYS_WARN_FMT("⚠️  AUDIO RECORDING NOT AVAILABLE - Created dummy file with {} seconds of SILENCE: {}", duration_seconds, current_recording_path_);
                    SYS_WARN("⚠️  To enable real recording, install 'ffmpeg' or 'sox' or implement platform-specific audio APIs");
                } else {
                    SYS_ERROR_FMT("Failed to create audio file: {}", current_recording_path_);
                }
            } else {
                // Execute the actual recording command
                SYS_INFO_FMT("Starting recording command: {}", command);

                // Start the recording process in background
                int result = system(command.c_str());
                if (result == 0) {
                    SYS_INFO("Recording command executed successfully");
                } else {
                    SYS_ERROR_FMT("Recording command failed with exit code: {}", result);
                }

                // For now, we'll use a simple approach - the process will run until stopped
                // We'll find the ffmpeg process and terminate it when recording stops
                recording_process_id_ = -1;  // We'll find it later when stopping
            }

            // Wait while recording is active
            while (recording_active_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Clean up recording process if it's still running
            if (recording_process_id_ > 0) {
                std::string kill_command = std::format("kill {} 2>/dev/null || true", recording_process_id_);
                system(kill_command.c_str());
                SYS_INFO_FMT("Terminated recording process {}", recording_process_id_);

                // Give ffmpeg a moment to finalize the WAV file
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                recording_process_id_ = -1;
            }

            SYS_INFO("Recording thread finished");
        });

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to start recording: {}", e.what());
        is_recording_ = false;
        recording_active_ = false;
    }
}

void voice_notes::stop_recording() {
    if (!is_recording_) return;

    SYS_INFO("Stopping recording...");

    recording_active_ = false;

    // Find and terminate any ffmpeg recording processes
    // Use pgrep to find ffmpeg processes
    std::string pgrep_command = "pgrep -f 'ffmpeg.*avfoundation' || true";
    FILE* pipe = popen(pgrep_command.c_str(), "r");
    if (pipe) {
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            int pid = std::atoi(buffer);
            if (pid > 0) {
                std::string kill_command = std::format("kill {} 2>/dev/null || true", pid);
                system(kill_command.c_str());
                SYS_INFO_FMT("Terminated ffmpeg process {}", pid);
            }
        }
        pclose(pipe);
    }

    // Give ffmpeg a moment to finalize the WAV file
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (recording_thread_.joinable()) {
        recording_thread_.join();
    }

    is_recording_ = false;

    // Calculate duration
    auto end_time = std::chrono::system_clock::now();
    double duration = std::chrono::duration<double>(end_time - recording_start_time_).count();

    // Create new note
    voice_note new_note;
    new_note.title = "New Voice Note";
    new_note.audio_path = current_recording_path_;
    new_note.duration = duration;
    new_note.created_at = recording_start_time_;
    new_note.updated_at = end_time;

    // Process the recording (transcription will happen asynchronously)
    process_recording(current_recording_path_);

    // Save to database
    save_note(new_note);

    // Reload notes
    load_notes();
}

void voice_notes::process_recording(const std::string& audio_file) {
    // Join any existing transcription thread before starting a new one
    if (transcription_thread_.joinable()) {
        transcription_thread_.join();
    }
    
    // Start transcription in background thread
    transcription_thread_ = std::thread([this, audio_file]() {
        voice_note note;
        note.audio_path = audio_file;
        transcribe_audio(audio_file, note);
    });
}

void voice_notes::transcribe_audio(const std::string& audio_file, voice_note& note) {
    try {
        // Find the existing note by audio_path
        voice_note* existing_note = nullptr;
        {
            std::lock_guard<std::mutex> lock(notes_mutex_);
            for (auto& n : notes_) {
                if (n.audio_path == audio_file) {
                    existing_note = &n;
                    break;
                }
            }
        }

        if (existing_note) {
            SYS_INFO_FMT("Found existing note with ID {} for audio file: {}", existing_note->id, audio_file);
            // Update the existing note with transcription
            existing_note->transcription = "This is a simulated transcription of the recorded audio. The actual transcription would be generated by the Whisper AI model.";
            save_note(*existing_note);
        } else {
            SYS_WARN_FMT("Could not find existing note for audio file: {}", audio_file);
            // Fallback: create a new note (this shouldn't happen in normal flow)
            note.transcription = "This is a simulated transcription of the recorded audio. The actual transcription would be generated by the Whisper AI model.";
            save_note(note);
        }

        // Reload notes to reflect changes
        load_notes();

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to transcribe audio: {}", e.what());
    }
}

void voice_notes::run_whisper_transcribe(const std::string& audio_file) {
    try {
        // Run whisper-transcribe via nix
        std::string command = std::format(
            "nix run github:blargg/ai-utils#whisper-transcribe -- -m tiny.en -f {} -o /tmp/transcription.txt",
            audio_file
        );

        whisper_output_ = ProcessHelper::executeCommand(command);

    } catch (const std::exception& e) {
        SYS_ERROR_FMT("Failed to run whisper-transcribe: {}", e.what());
    }
}

void voice_notes::play_audio(const std::string& audio_path) {
    if (audio_path.empty()) {
        SYS_ERROR("Audio path is empty");
        return;
    }

    // Check if file exists
    if (!fs::exists(audio_path)) {
        SYS_ERROR_FMT("Audio file does not exist: {}", audio_path);
        return;
    }

    SYS_INFO_FMT("Playing audio file: {}", audio_path);

    // Stop any currently playing audio
    stop_audio();

    // Create a media player item and play the audio
    static media_player_item player_item;
    player_item.url = audio_path;
    player_item.playMedia();
}

void voice_notes::stop_audio() {
    if (audio_player_) {
        audio_player_->stopAll();
    }
}

void voice_notes::open_in_finder(const std::string& file_path) {
    if (file_path.empty()) return;
    
    // On macOS, use the 'open' command to reveal file in Finder
    std::string command = std::format("open -R \"{}\"", file_path);
    int result = system(command.c_str());
    
    if (result != 0) {
        SYS_ERROR_FMT("Failed to open file in Finder: {}", file_path);
    } else {
        SYS_INFO_FMT("Opened file in Finder: {}", file_path);
    }
}

std::string voice_notes::generate_audio_filename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "voice_note_" << time_t << ".wav";

    fs::path data_dir = rouen::platform::get_user_data_path() / "voice_notes";
    return (data_dir / ss.str()).string();
}

std::string voice_notes::format_timestamp(const std::chrono::system_clock::time_point& time) const {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string voice_notes::format_duration(double seconds) const {
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    return std::format("{:02d}:{:02d}", minutes, secs);
}

void voice_notes::add_tag_to_note(voice_note& note, const std::string& tag) {
    if (!tag.empty() && std::find(note.tags.begin(), note.tags.end(), tag) == note.tags.end()) {
        note.tags.push_back(tag);
    }
}

// Build an AVFoundation device spec based on selected index (macOS only)
std::string voice_notes::avf_device_spec() const {
#if defined(__APPLE__)
    return std::format(":{}", avf_device_index_);
#else
    return "";
#endif
}

const char* voice_notes::avf_device_label(int idx) const {
    switch (idx) {
        case 0: return "System Default (:0)";
        case 1: return "QCY H3 (:1)";
        case 2: return "USB Microphone (:2)";
        default: return "Unknown";
    }
}

bool voice_notes::render() {
    name("Voice Notes");
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors[0]);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[1]);

    if (!render_window([this]() {
        // Header
        ImGui::PushFont(ImGui::GetFont());
        ImGui::TextColored(colors[5], "🎙️ Voice Notes");
        ImGui::PopFont();
        ImGui::Separator();

        // Main layout
        ImGui::Columns(2, "voice_notes_layout", true);

        // Left column - Recording controls and notes list
        ImGui::BeginChild("left_panel", ImVec2(0, 0), true);
        render_recording_controls();
        ImGui::Separator();
        render_search_bar();
        ImGui::Separator();
        render_notes_list();
        ImGui::EndChild();

        ImGui::NextColumn();

        // Right column - Note editor
        ImGui::BeginChild("right_panel", ImVec2(0, 0), true);
        if (selected_note_id_ >= 0) {
            auto it = std::find_if(notes_.begin(), notes_.end(), [this](const voice_note& n){ return n.id == selected_note_id_; });
            if (it != notes_.end()) {
                render_note_editor(*it);
            }
        } else {
            ImGui::TextColored(colors[6], "Select a voice note to view and edit");
        }
        ImGui::EndChild();

        ImGui::Columns(1);
    })) {
        ImGui::PopStyleColor(2);
        return false;
    }

    ImGui::PopStyleColor(2);
    return true;
}

void voice_notes::render_recording_controls() {
    ImGui::TextColored(colors[5], "Recording Controls");
    ImGui::Separator();

    // Top utilities row
    if (ImGui::Button("Open Voice Notes Folder")) {
        auto folder = (rouen::platform::get_user_data_path() / "voice_notes").string();
        open_in_finder(folder);
    }
#if defined(__APPLE__)
    ImGui::SameLine();
    ImGui::Text("Input:");
    ImGui::SameLine();
    const char* items[] = { avf_device_label(0), avf_device_label(1), avf_device_label(2) };
    int temp = avf_device_index_;
    if (ImGui::Combo("##mic", &temp, items, IM_ARRAYSIZE(items))) {
        avf_device_index_ = temp;
    }
#endif

    // Recording status
    if (is_recording_) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
        ImGui::Text("🔴 RECORDING");
        ImGui::PopStyleColor();

        auto elapsed = std::chrono::duration<double>(
            std::chrono::system_clock::now() - recording_start_time_).count();
        ImGui::Text("Duration: %.1f seconds", elapsed);
    } else {
        ImGui::TextColored(colors[6], "Ready to record");
    }

    ImGui::Spacing();

    // Control buttons
    if (!is_recording_) {
        if (ImGui::Button("Start Recording", ImVec2(120, 30))) {
            start_recording();
        }
        ImGui::SameLine();
        if (ImGui::Button("Meeting Mode", ImVec2(100, 30))) {
            show_meeting_mode_ = !show_meeting_mode_;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, colors[3]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[3]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[3]);
        if (ImGui::Button("Stop Recording", ImVec2(120, 30))) {
            stop_recording();
        }
        ImGui::PopStyleColor(3);
    }

    // Show warning if recording is not available
    static bool recording_available = (system("which sox >/dev/null 2>&1") == 0);
    if (!recording_available) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚠️  AUDIO RECORDING NOT AVAILABLE");
        ImGui::TextWrapped("Install 'sox' to enable real audio recording. Currently creating dummy files with silence.");
        if (ImGui::Button("Check Again", ImVec2(100, 25))) {
            recording_available = (system("which sox >/dev/null 2>&1") == 0);
        }
    }

    // Meeting mode toggle
    if (show_meeting_mode_) {
        ImGui::Separator();
        render_meeting_mode();
    }
}

void voice_notes::render_notes_list() {
    ImGui::TextColored(colors[5], "Voice Notes");
    ImGui::Separator();

    ImGui::BeginChild("notes_list", ImVec2(0, 0), true);

    std::lock_guard<std::mutex> lock(notes_mutex_);

    for (auto& note : notes_) {
        // Filter by search query
        if (!search_query_.empty()) {
            bool matches = ::helpers::StringHelper::contains_case_insensitive(note.title, search_query_) ||
                          ::helpers::StringHelper::contains_case_insensitive(note.transcription, search_query_);
            if (!matches) {
                for (const auto& tag : note.tags) {
                    if (::helpers::StringHelper::contains_case_insensitive(tag, search_query_)) {
                        matches = true;
                        break;
                    }
                }
            }
            if (!matches) continue;
        }

        // Note item
        bool is_selected = (selected_note_id_ == note.id);
        if (ImGui::Selectable(std::format("{}##{}", note.title, note.id).c_str(), is_selected)) {
            selected_note_id_ = note.id;
        }

        ImGui::SameLine();
        if (!note.audio_path.empty() && ImGui::SmallButton(std::format("Open##{}", note.id).c_str())) {
            open_in_finder(note.audio_path);
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                delete_note(note.id);
                if (selected_note_id_ == note.id) {
                    selected_note_id_ = -1;
                }
            }
            if (ImGui::MenuItem("Play Audio") && !note.audio_path.empty()) {
                play_audio(note.audio_path);
            }
            if (ImGui::MenuItem("Open in Finder") && !note.audio_path.empty()) {
                open_in_finder(note.audio_path);
            }
            ImGui::EndPopup();
        }

        // Note info
        if (ImGui::IsItemHovered()) {
            ImGui::PushID(note.id);
            ImGui::BeginTooltip();
            ImGui::Text("Created: %s", format_timestamp(note.created_at).c_str());
            if (note.duration > 0) {
                ImGui::Text("Duration: %s", format_duration(note.duration).c_str());
            }
            if (!note.tags.empty()) {
                ImGui::Text("Tags:");
                for (const auto& tag : note.tags) {
                    ImGui::BulletText("%s", tag.c_str());
                }
            }
            ImGui::EndTooltip();
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

void voice_notes::render_note_editor(voice_note& note) {
    ImGui::TextColored(colors[5], "Note Editor");
    ImGui::Separator();

    // Title
    ImGui::Text("Title:");
    ImGui::SameLine();
    strcpy(title_buffer_, note.title.c_str());
    if (ImGui::InputText("##title", title_buffer_, sizeof(title_buffer_))) {
        note.title = title_buffer_;
        save_note(note);
    }

    ImGui::Spacing();

    // Tags
    ImGui::Text("Tags:");
    ImGui::SameLine();
    if (ImGui::InputText("##new_tag", tag_buffer_, sizeof(tag_buffer_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (strlen(tag_buffer_) > 0) {
            add_tag_to_note(note, tag_buffer_);
            save_note(note);
            memset(tag_buffer_, 0, sizeof(tag_buffer_));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Tag")) {
        if (strlen(tag_buffer_) > 0) {
            add_tag_to_note(note, tag_buffer_);
            save_note(note);
            memset(tag_buffer_, 0, sizeof(tag_buffer_));
        }
    }

    // Display tags
    if (!note.tags.empty()) {
        ImGui::Text("Current tags:");
        for (size_t i = 0; i < note.tags.size(); ++i) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(("x##" + std::to_string(i)).c_str())) {
                note.tags.erase(note.tags.begin() + static_cast<ptrdiff_t>(i));
                save_note(note);
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("%s", note.tags[i].c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Transcription
    ImGui::Text("Transcription:");
    ImGui::InputTextMultiline("##transcription", title_buffer_, sizeof(title_buffer_),
        ImVec2(-1, ImGui::GetContentRegionAvail().y - 60), ImGuiInputTextFlags_AllowTabInput);

    // Save transcription changes
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        save_note(note);
    }

    ImGui::Spacing();

    // Audio controls
    if (!note.audio_path.empty()) {
        if (ImGui::Button("Play Audio", ImVec2(80, 25))) {
            play_audio(note.audio_path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(50, 25))) {
            stop_audio();
        }
        ImGui::SameLine();
        ImGui::Text("Duration: %s", format_duration(note.duration).c_str());
    }
}

void voice_notes::render_search_bar() {
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::InputText("##search", title_buffer_, sizeof(title_buffer_));
    if (!search_query_.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            search_query_.clear();
        }
    }
}

void voice_notes::render_meeting_mode() {
    ImGui::TextColored(colors[5], "Meeting Mode");
    ImGui::TextWrapped("Meeting mode provides specialized features for recording meetings, including speaker identification and automatic meeting notes generation.");

    ImGui::Checkbox("Enable Meeting Mode", &meeting_mode_);

    if (meeting_mode_) {
        ImGui::Text("Meeting features:");
        ImGui::BulletText("Automatic speaker identification");
        ImGui::BulletText("Meeting summary generation");
        ImGui::BulletText("Action item extraction");
        ImGui::BulletText("Timestamp navigation");
    }
}

} // namespace rouen::cards
