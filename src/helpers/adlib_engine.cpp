#include "adlib_engine.hpp"
#include "media_player.hpp"
#include "texture_helper.hpp"
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <iostream>

namespace rouen::helpers {

bool AdLibEngine::prepare(const AdLibConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(engine_mutex_);
    stop();

    config_ = config;

    auto audio_cb = [this](const uint8_t* pcm_s16_data, size_t size_in_bytes) {
        if (pcm_s16_data && size_in_bytes > 0) {
            std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
            video_audio_buf_.insert(video_audio_buf_.end(), pcm_s16_data, pcm_s16_data + size_in_bytes);
        }
    };

    // Load intro video item if provided
    if (!config_.intro_video_path.empty() && std::filesystem::exists(config_.intro_video_path)) {
        intro_item_ = std::make_shared<media_player_item>();
        intro_item_->is_adlib_item.store(true);
        intro_item_->url = config_.intro_video_path;
        intro_item_->item_title = "Ad-Lib Intro";
        intro_item_->on_audio_pcm_cb = audio_cb;
        intro_item_->playMedia(this);
        intro_item_->pauseMedia(); // Start paused-played for scene setup
    }

    // Load outro video item if provided
    if (!config_.outro_video_path.empty() && std::filesystem::exists(config_.outro_video_path)) {
        outro_item_ = std::make_shared<media_player_item>();
        outro_item_->is_adlib_item.store(true);
        outro_item_->url = config_.outro_video_path;
        outro_item_->item_title = "Ad-Lib Outro";
        outro_item_->on_audio_pcm_cb = audio_cb;
        outro_item_->duration.store(10.005);
    }

    // Prepare background image surface & GPU texture if path is provided
    if (bg_texture_) {
        delete bg_texture_;
        bg_texture_ = nullptr;
    }
    if (bg_surface_) {
        SDL_DestroySurface(bg_surface_);
        bg_surface_ = nullptr;
    }
    if (!config_.background_path.empty() && std::filesystem::exists(config_.background_path)) {
        bg_surface_ = IMG_Load(config_.background_path.c_str());
        SDL_GPUDevice* device = TextureHelper::g_gpu_device;
        if (device && bg_surface_) {
            bg_texture_ = TextureHelper::createTextureFromSurface(device, bg_surface_);
        }
    }

    stage_.store(AdLibStage::Prepared);
    is_paused_.store(true);
    recording_active_.store(false);
    auto_stop_seconds_.store(config_.presentation_duration_seconds);
    accumulated_paused_duration_ = std::chrono::milliseconds(0);

    // Set initial item on detached window so user sees paused intro frame or background
    if (intro_item_) {
        media_player::set_detached_item(intro_item_);
    } else {
        media_player::clear_detached_item();
    }

    std::cout << "[AdLibEngine] Prepared Ad-Lib scene in paused state. Ready for overlay testing." << std::endl;
    return true;
}

bool AdLibEngine::start() {
    std::lock_guard<std::recursive_mutex> lock(engine_mutex_);

    if (stage_.load() == AdLibStage::Idle) {
        return false;
    }

    if (config_.mode == AdLibMode::Recorded) {
        // Initialize MP4 recording file
        std::string out_path = config_.output_mp4_path;
        if (out_path.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            char buf[64];
            std::strftime(buf, sizeof(buf), "adlib_recording_%Y%m%d_%H%M%S.mp4", std::localtime(&time_t_now));
            out_path = buf;
        }
        if (mp4_writer_.open(out_path, 1280, 720, 30, 44100)) {
            {
                std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
                last_rendered_pixels_.clear();
            }
            has_new_ui_frame_.store(false);
            recording_active_.store(true);
            std::cout << "[AdLibEngine] MP4 recording initialized successfully on file: " << out_path << std::endl;
            SDL_AudioDeviceID dev_id = config_.mic_device_id;
            if (!config_.mic_device_name.empty()) {
                SDL_AudioDeviceID found = audio_capture_.find_device_id_by_name(config_.mic_device_name);
                if (found != 0) dev_id = found;
            }
            audio_capture_.start(dev_id, 44100, 2);
        } else {
            std::cerr << "[AdLibEngine] CRITICAL: Failed to initialize MP4 recording on file: " << out_path << std::endl;
        }
    }

    start_time_ = std::chrono::steady_clock::now();
    stage_start_time_ = start_time_;
    stage_start_pts_ = 0;
    accumulated_paused_duration_ = std::chrono::milliseconds(0);
    last_rendered_frame_time_ = start_time_;
    last_written_mp4_frame_time_ = start_time_;
    {
        std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
        last_rendered_pixels_.clear();
    }
    has_new_ui_frame_.store(false);
    {
        std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
        video_audio_buf_.clear();
    }
    if (intro_item_) {
        intro_item_->resumeMedia();
    }
    is_paused_.store(false);

    rec_thread_ = std::thread([this]() {
        double pres_dur = auto_stop_seconds_.load();
        double max_total_dur = (pres_dur > 0.0) ? (7.958 + pres_dur + 8.0 + 3.0) : 0.0;
        auto start_tp = std::chrono::steady_clock::now();
        auto next_tick = start_tp;
        while (recording_active_.load()) {
            next_tick += std::chrono::microseconds(33333);
            update_frame_tick();
            if (max_total_dur > 0.0) {
                double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_tp).count();
                if (elapsed >= max_total_dur) {
                    std::cout << "[AdLibEngine] Total safety timeout (" << max_total_dur << "s) reached. Stopping recording." << std::endl;
                    transition_to_stage(AdLibStage::Finished);
                    break;
                }
            }
            std::this_thread::sleep_until(next_tick);
        }
    });

    if (intro_item_) {
        transition_to_stage(AdLibStage::Intro);
    } else {
        transition_to_stage(AdLibStage::Middle);
    }

    return true;
}

void AdLibEngine::transition_to_stage(AdLibStage new_stage) {
    stage_.store(new_stage);
    stage_start_time_ = std::chrono::steady_clock::now();
    stage_start_pts_ = mp4_writer_.get_video_pts();

    if (new_stage == AdLibStage::Intro) {
        {
            std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
            video_audio_buf_.clear();
        }
        {
            std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
            last_rendered_pixels_.clear();
        }
        has_new_ui_frame_.store(false);
        intro_presented_count_.store(0);
        if (intro_item_) {
            intro_item_->has_presented_first_frame.store(true);
            intro_item_->seekTo(0.0);
            intro_item_->resumeMedia();
            if (intro_item_->local_audio_stream) {
                SDL_PauseAudioStreamDevice(intro_item_->local_audio_stream);
            }
            intro_item_->playback_start_time = stage_start_time_;
            intro_item_->start_offset.store(0.0);
            intro_item_->position.store(0.0);
            media_player::set_detached_item(intro_item_);
        }
    } else if (new_stage == AdLibStage::Middle) {
        {
            std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
            last_rendered_pixels_.clear();
            if (bg_surface_) {
                SDL_Surface* converted = SDL_ConvertSurface(bg_surface_, SDL_PIXELFORMAT_RGBA32);
                if (converted) {
                    SDL_Surface* dst_surf = SDL_ScaleSurface(converted, 1280, 720, SDL_SCALEMODE_LINEAR);
                    if (dst_surf) {
                        last_rendered_pixels_.resize(1280 * 720 * 4);
                        std::memcpy(last_rendered_pixels_.data(), dst_surf->pixels, 1280 * 720 * 4);
                        SDL_DestroySurface(dst_surf);
                    }
                    SDL_DestroySurface(converted);
                }
            }
        }
        {
            std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
            if (!video_audio_buf_.empty() && mp4_writer_.is_open()) {
                int64_t target_audio_pts = mp4_writer_.get_video_pts() * 1470;
                int64_t current_audio_pts = mp4_writer_.get_audio_pts();
                if (target_audio_pts > current_audio_pts) {
                    size_t max_allowed_bytes = static_cast<size_t>(target_audio_pts - current_audio_pts) * 4;
                    if (video_audio_buf_.size() > max_allowed_bytes) {
                        video_audio_buf_.resize(max_allowed_bytes);
                    }
                    mp4_writer_.write_audio_samples(video_audio_buf_.data(), video_audio_buf_.size());
                }
            }
            video_audio_buf_.clear();
        }
        mp4_writer_.flush_audio();
        audio_capture_.read_audio_data(); // Drain mic buffer so Stage 2 starts clean
        if (intro_item_) intro_item_->pauseMedia();
        if (outro_item_) outro_item_->pauseMedia();
        // Middle stage: detach player item to draw fixed background + overlays
        media_player::clear_detached_item();
    } else if (new_stage == AdLibStage::Outro) {
        std::vector<uint8_t> mic_pcm = audio_capture_.read_audio_data();
        if (!mic_pcm.empty() && mp4_writer_.is_open()) {
            int64_t target_audio_pts = mp4_writer_.get_video_pts() * 1470;
            int64_t current_audio_pts = mp4_writer_.get_audio_pts();
            if (target_audio_pts > current_audio_pts) {
                size_t max_allowed_bytes = static_cast<size_t>(target_audio_pts - current_audio_pts) * 4;
                if (mic_pcm.size() > max_allowed_bytes) {
                    mic_pcm.resize(max_allowed_bytes);
                }
                mp4_writer_.write_audio_samples(mic_pcm.data(), mic_pcm.size());
            }
        }
        mp4_writer_.flush_audio();
        {
            std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
            video_audio_buf_.clear();
        }
        if (outro_item_) {
            outro_item_->playMedia(this);
            if (outro_item_->local_audio_stream) {
                SDL_PauseAudioStreamDevice(outro_item_->local_audio_stream);
            }
            outro_item_->playback_start_time = std::chrono::steady_clock::now();
            outro_item_->start_offset.store(0.0);
            outro_item_->position.store(0.0);
            outro_item_->has_presented_first_frame.store(false);
            media_player::set_detached_item(outro_item_);
        }
    } else if (new_stage == AdLibStage::Finished) {
        audio_capture_.stop();
        {
            std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
            video_audio_buf_.clear();
        }
        if (intro_item_) intro_item_->pauseMedia();
        if (outro_item_) outro_item_->pauseMedia();
        media_player::clear_detached_item();
        stop();
    }
}

void AdLibEngine::update_frame_tick() {
    try {
        if (is_paused_.load()) return;
        if (stage_.load() == AdLibStage::Intro && intro_item_ && !intro_item_->has_presented_first_frame.load()) {
            std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
            last_rendered_pixels_.clear();
            has_new_ui_frame_.store(false);
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(engine_mutex_);
        std::cout << "[AdLibEngine] tick: rec_active=" << recording_active_.load() << " writer_open=" << mp4_writer_.is_open() << " elapsed=" << get_elapsed_seconds() << "s" << std::endl;

        auto current_stage = stage_.load();

        // 1. Auto-transition from Intro to Middle (Presentation) when full Intro video frames (239 frames @ 30 FPS = 7.958s) have been written
        if (current_stage == AdLibStage::Intro && intro_item_) {
            double dur = intro_item_->duration.load();
            if (dur < 7.95) dur = 7.958333;
            int target_intro_frames = static_cast<int>(std::round(dur * 30.0));
            int written_intro_frames = static_cast<int>(mp4_writer_.get_video_pts() - stage_start_pts_);
            if (written_intro_frames >= target_intro_frames) {
                std::cout << "[AdLibEngine] Intro video finished (written_intro_frames=" << written_intro_frames << " dur=" << dur << "s). Auto-transitioning to Presentation phase (Middle)." << std::endl;
                transition_to_stage(AdLibStage::Middle);
            }
        }
        // 2. Auto-transition from Middle (Presentation) to Outro when presentation stage_elapsed reaches target duration (if configured)
        else if (current_stage == AdLibStage::Middle) {
            double target_middle_dur = auto_stop_seconds_.load();
            if (target_middle_dur > 0.0) {
                double stage_elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stage_start_time_).count();
                if (stage_elapsed >= target_middle_dur) {
                    std::cout << "[AdLibEngine] Presentation phase (Middle) duration (" << target_middle_dur << "s) completed. Auto-transitioning to Outro phase." << std::endl;
                    if (outro_item_) {
                        transition_to_stage(AdLibStage::Outro);
                    } else {
                        transition_to_stage(AdLibStage::Finished);
                    }
                }
            }
        }
        // 3. Auto-transition from Outro to Finished (completes recording) when Outro video finishes playing full duration
        else if (current_stage == AdLibStage::Outro && outro_item_) {
            double dur = outro_item_->duration.load();
            if (dur <= 0.0) dur = 10.005;
            int target_outro_frames = static_cast<int>(std::round(dur * 30.0));
            int written_outro_frames = static_cast<int>(mp4_writer_.get_video_pts() - stage_start_pts_);
            if (written_outro_frames >= target_outro_frames) {
                std::cout << "[AdLibEngine] Outro video finished (written_outro_frames=" << written_outro_frames << " dur=" << dur << "s). Auto-transitioning to Finished phase." << std::endl;
                transition_to_stage(AdLibStage::Finished);
            }
        }

        auto now = std::chrono::steady_clock::now();

        if (recording_active_.load() && mp4_writer_.is_open()) {
            std::vector<uint8_t> audio_to_write;
            bool video_has_audio = false;

            if (current_stage == AdLibStage::Intro && intro_item_ && !config_.intro_video_path.empty()) {
                video_has_audio = true;
            } else if (current_stage == AdLibStage::Outro && outro_item_ && !config_.outro_video_path.empty()) {
                video_has_audio = true;
            }

            if (video_has_audio) {
                // In Intro/Outro stage with video audio: move available video audio samples
                std::lock_guard<std::mutex> va_lock(video_audio_mutex_);
                if (!video_audio_buf_.empty()) {
                    audio_to_write = std::move(video_audio_buf_);
                    video_audio_buf_.clear();
                }
                
                // Mute/drain live microphone input during video playback
                audio_capture_.read_audio_data();
            } else {
                // In Stage 2 Presentation (or if Intro/Outro video has no audio track): record live microphone
                audio_to_write = audio_capture_.read_audio_data();
            }

            if (!audio_to_write.empty()) {
                mp4_writer_.write_audio_samples(audio_to_write.data(), audio_to_write.size());
            } else if (!video_has_audio) {
                // 20ms @ 44.1kHz 16-bit stereo PCM = 882 samples * 4 bytes = 3528 bytes
                static const std::vector<uint8_t> silent_tick(3528, 0);
                mp4_writer_.write_audio_samples(silent_tick.data(), silent_tick.size());
            }

            // 2. Write video frame if a new UI frame was rendered or as periodic fallback (every 33ms @ 30 FPS)
            bool can_write_video = true;
            if (current_stage == AdLibStage::Intro && intro_item_ && !intro_item_->has_presented_first_frame.load()) {
                can_write_video = false;
            }
            bool is_first_frame = (mp4_writer_.get_video_pts() == 0);
            if (is_first_frame && (last_rendered_pixels_.empty() || !has_new_ui_frame_.load())) {
                can_write_video = false;
            }
            double stage_elapsed = std::chrono::duration<double>(now - stage_start_time_).count();
            int target_frames = static_cast<int>(std::floor(stage_elapsed * 30.0));
            int written_stage_frames = static_cast<int>(mp4_writer_.get_video_pts() - stage_start_pts_);
            if (current_stage == AdLibStage::Intro || current_stage == AdLibStage::Outro || config_.mode == AdLibMode::Recorded) {
                target_frames = written_stage_frames + 1;
            }
            if (can_write_video && (is_first_frame || written_stage_frames < target_frames)) {
                has_new_ui_frame_.store(false);
                std::vector<uint8_t> local_frame;
                if (current_stage == AdLibStage::Intro && intro_item_) {
                    local_frame = intro_item_->get_current_adlib_frame_pixels();
                } else if (current_stage == AdLibStage::Outro && outro_item_) {
                    local_frame = outro_item_->get_current_adlib_frame_pixels();
                }
                if (local_frame.empty()) {
                    std::lock_guard<std::mutex> fb_lock(frame_buffer_mutex_);
                    local_frame = last_rendered_pixels_;
                }
                if (!local_frame.empty()) {
                    mp4_writer_.write_video_frame(local_frame.data());
                    last_written_mp4_frame_time_ = now;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[AdLibEngine] EXCEPTION in update_frame_tick: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[AdLibEngine] UNKNOWN EXCEPTION in update_frame_tick" << std::endl;
    }
}

void AdLibEngine::pause() {
    if (!is_paused_.exchange(true)) {
        pause_start_ = std::chrono::steady_clock::now();
        if (stage_.load() == AdLibStage::Intro && intro_item_) intro_item_->pauseMedia();
        if (stage_.load() == AdLibStage::Outro && outro_item_) outro_item_->pauseMedia();
    }
}

void AdLibEngine::resume() {
    if (is_paused_.exchange(false)) {
        auto now = std::chrono::steady_clock::now();
        accumulated_paused_duration_ += std::chrono::duration_cast<std::chrono::milliseconds>(now - pause_start_);
        if (stage_.load() == AdLibStage::Intro && intro_item_) intro_item_->resumeMedia();
        if (stage_.load() == AdLibStage::Outro && outro_item_) outro_item_->resumeMedia();
    }
}

void AdLibEngine::toggle_pause() {
    if (is_paused_.load()) resume();
    else pause();
}

void AdLibEngine::next_stage() {
    std::lock_guard<std::recursive_mutex> lock(engine_mutex_);
    auto current = stage_.load();

    if (current == AdLibStage::Prepared) {
        start();
    } else if (current == AdLibStage::Intro) {
        transition_to_stage(AdLibStage::Middle);
    } else if (current == AdLibStage::Middle) {
        if (outro_item_) {
            transition_to_stage(AdLibStage::Outro);
        } else {
            transition_to_stage(AdLibStage::Finished);
        }
    } else if (current == AdLibStage::Outro) {
        transition_to_stage(AdLibStage::Finished);
    }
}

void AdLibEngine::stop() {
    {
        std::lock_guard<std::recursive_mutex> lock(engine_mutex_);
        if (stage_.load() == AdLibStage::Idle) return;
        std::cout << "[AdLibEngine] stop() called." << std::endl;
        stage_.store(AdLibStage::Finished);
        is_paused_.store(true);
        recording_active_.store(false);
    }

    if (rec_thread_.joinable()) {
        if (std::this_thread::get_id() != rec_thread_.get_id()) {
            rec_thread_.join();
        } else {
            rec_thread_.detach();
        }
    }

    std::lock_guard<std::recursive_mutex> lock(engine_mutex_);
    audio_capture_.stop();
    mp4_writer_.close();

    if (intro_item_) {
        intro_item_->stopMedia();
        intro_item_ = nullptr;
    }
    if (outro_item_) {
        outro_item_->stopMedia();
        outro_item_ = nullptr;
    }

    if (bg_texture_) {
        delete bg_texture_;
        bg_texture_ = nullptr;
    }
    if (bg_surface_) {
        SDL_DestroySurface(bg_surface_);
        bg_surface_ = nullptr;
    }

    stage_.store(AdLibStage::Idle);
    std::cout << "[AdLibEngine] Ad-Lib stopped cleanly and file closed." << std::endl;
}

void AdLibEngine::reset() {
    stop();
}

std::shared_ptr<media_player_item> AdLibEngine::get_current_detached_item() {
    auto current = stage_.load();
    if (current == AdLibStage::Intro) return intro_item_;
    if (current == AdLibStage::Outro) return outro_item_;
    return nullptr;
}

RouenGPUTexture* AdLibEngine::get_background_texture(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd_buf) {
    if (!device) return nullptr;
    static std::mutex bg_tex_mutex;
    std::lock_guard<std::mutex> lock(bg_tex_mutex);
    if (!bg_texture_) {
        if (bg_surface_) {
            bg_texture_ = TextureHelper::createTextureFromSurface(device, bg_surface_, cmd_buf);
        } else {
            // Elegant dark violet background texture for Ad-Lib presentation mode
            bg_texture_ = TextureHelper::createSolidColorTexture(device, 1280, 720, 30, 25, 45, 255, cmd_buf);
        }
    }
    return bg_texture_;
}

void AdLibEngine::on_detached_frame_rendered(const uint8_t* rgba_pixels, int width, int height, int pitch) {
    if (!recording_active_.load() || config_.mode != AdLibMode::Recorded || !mp4_writer_.is_open() || !rgba_pixels || width <= 0 || height <= 0) return;
    if (stage_.load() == AdLibStage::Intro && intro_item_) {
        if (!intro_item_->has_presented_first_frame.load()) return;
    }
    if (stage_.load() == AdLibStage::Middle && bg_surface_) {
        uint64_t sum_y = 0;
        const uint8_t* pix = static_cast<const uint8_t*>(rgba_pixels);
        size_t total_px = static_cast<size_t>(width * height);
        size_t step = (total_px > 1000) ? (total_px / 1000) * 4 : 4;
        size_t samples = 0;
        for (size_t i = 0; i + 3 < total_px * 4; i += step) {
            uint8_t r = pix[i + 0];
            uint8_t g = pix[i + 1];
            uint8_t b = pix[i + 2];
            sum_y += static_cast<uint64_t>(0.299 * r + 0.587 * g + 0.114 * b);
            samples++;
        }
        if (samples > 0 && (sum_y / samples) < 100) {
            return;
        }
    }

    int line_pitch = (pitch > 0) ? pitch : (width * 4);

    SDL_Surface* src_surf = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<void*>(static_cast<const void*>(rgba_pixels)), line_pitch);
    if (src_surf) {
        SDL_Surface* dst_surf = SDL_ScaleSurface(src_surf, 1280, 720, SDL_SCALEMODE_LINEAR);
        if (dst_surf) {
            {
                std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
                if (last_rendered_pixels_.size() != 1280 * 720 * 4) {
                    last_rendered_pixels_.resize(1280 * 720 * 4);
                }
                const uint8_t* src = static_cast<const uint8_t*>(dst_surf->pixels);
                uint8_t* dst = last_rendered_pixels_.data();
                for (size_t i = 0; i < 1280 * 720; ++i) {
                    dst[i * 4 + 0] = src[i * 4 + 2]; // R <- B
                    dst[i * 4 + 1] = src[i * 4 + 1]; // G <- G
                    dst[i * 4 + 2] = src[i * 4 + 0]; // B <- R
                    dst[i * 4 + 3] = src[i * 4 + 3]; // A <- A
                }
            }
            has_new_ui_frame_.store(true);
            last_rendered_frame_time_ = std::chrono::steady_clock::now();
            SDL_DestroySurface(dst_surf);
        }
        SDL_DestroySurface(src_surf);
    }
}

double AdLibEngine::get_elapsed_seconds() const {
    if (stage_.load() == AdLibStage::Idle || stage_.load() == AdLibStage::Prepared) return 0.0;
    auto now = std::chrono::steady_clock::now();
    auto total = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_) - accumulated_paused_duration_;
    return std::max(0.0, static_cast<double>(total.count()) / 1000.0);
}

double AdLibEngine::get_stage_written_seconds() const {
    if (!mp4_writer_.is_open()) return 0.0;
    int64_t diff = mp4_writer_.get_video_pts() - stage_start_pts_;
    return std::max(0.0, static_cast<double>(diff) * (1.0 / 30.0));
}

double AdLibEngine::get_stage_elapsed_seconds() const {
    if (stage_.load() == AdLibStage::Idle || stage_.load() == AdLibStage::Prepared) return 0.0;
    auto now = std::chrono::steady_clock::now();
    return std::max(0.0, std::chrono::duration<double>(now - stage_start_time_).count());
}

} // namespace rouen::helpers
