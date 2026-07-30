#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <SDL3/SDL.h>
#include "media_player_item.hpp"
#include "audio_capture.hpp"
#include "mp4_writer.hpp"
#include "texture_helper.hpp"

namespace rouen::helpers {

enum class AdLibStage {
    Idle,
    Prepared, // Detached window open & paused for overlay testing, NOT recording yet
    Intro,    // Playing intro video (Recording active in Recorded mode)
    Middle,   // Presentation (Fixed background + card overlays + mic capture)
    Outro,    // Playing transition-out video
    Finished  // Recording completed & finalized
};

enum class AdLibMode {
    Live,
    Recorded
};

struct AdLibConfig {
    std::string intro_video_path;
    std::string background_path; // Image file path or color hex
    std::string outro_video_path;
    SDL_AudioDeviceID mic_device_id{0};
    std::string mic_device_name;
    std::string output_mp4_path;
    AdLibMode mode{AdLibMode::Live};
    double presentation_duration_seconds{0.0}; // 0.0 means Manual transition
};

class AdLibEngine {
public:
    static AdLibEngine& instance() {
        static AdLibEngine engine;
        return engine;
    }

    bool prepare(const AdLibConfig& config);
    bool start(); // Go Live / Start recording & playback
    void pause();
    void resume();
    void toggle_pause();
    void next_stage();
    void stop();
    void reset();

    void update_frame_tick(); // Check auto-transitions (EOF detection)

    AdLibStage get_stage() const { return stage_.load(); }
    AdLibMode get_mode() const { return config_.mode; }
    bool is_paused() const { return is_paused_.load(); }
    bool is_active() const { return stage_.load() != AdLibStage::Idle && stage_.load() != AdLibStage::Finished; }
    bool is_recording() const { return recording_active_.load(); }
    bool is_headless_recording() const { return auto_stop_seconds_.load() > 0.0; }
    
    std::shared_ptr<media_player_item> get_current_detached_item();
    std::shared_ptr<media_player_item> get_intro_item() const { return intro_item_; }
    std::shared_ptr<media_player_item> get_outro_item() const { return outro_item_; }
    RouenGPUTexture* get_background_texture(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd_buf = nullptr);

    // Recording & Frame Grabber
    void on_detached_frame_rendered(const uint8_t* rgba_pixels, int width, int height, int pitch = 0);
    float get_mic_peak() const { return audio_capture_.get_current_peak(); }
    void set_auto_stop_seconds(double sec) { auto_stop_seconds_.store(sec); }
    double get_auto_stop_seconds() const { return auto_stop_seconds_.load(); }
    double get_elapsed_seconds() const;
    double get_stage_written_seconds() const;
    double get_stage_elapsed_seconds() const;

private:
    AdLibEngine() = default;
    ~AdLibEngine() { stop(); }

    void transition_to_stage(AdLibStage new_stage);

    std::recursive_mutex engine_mutex_;
    std::mutex frame_buffer_mutex_;
    AdLibConfig config_;
    std::atomic<AdLibStage> stage_{AdLibStage::Idle};
    std::atomic<bool> is_paused_{true};
    std::atomic<bool> recording_active_{false};
    std::atomic<bool> has_new_ui_frame_{false};
    std::atomic<double> auto_stop_seconds_{0.0};

    std::shared_ptr<media_player_item> intro_item_{nullptr};
    std::shared_ptr<media_player_item> outro_item_{nullptr};
    
    RouenGPUTexture* bg_texture_{nullptr};
    SDL_Surface* bg_surface_{nullptr};

    AudioCapture audio_capture_;
    NativeMP4Writer mp4_writer_;
    std::thread rec_thread_;

    std::chrono::steady_clock::time_point start_time_{};
    std::chrono::steady_clock::time_point stage_start_time_{};
    std::chrono::steady_clock::time_point last_written_mp4_frame_time_{};
    int64_t stage_start_pts_{0};
    std::atomic<int> intro_presented_count_{0};
    std::chrono::steady_clock::time_point pause_start_{};
    std::chrono::steady_clock::time_point last_rendered_frame_time_{};
    std::chrono::milliseconds accumulated_paused_duration_{0};
    std::vector<uint8_t> last_rendered_pixels_;
    std::mutex video_audio_mutex_;
    std::vector<uint8_t> video_audio_buf_;
};

} // namespace rouen::helpers
