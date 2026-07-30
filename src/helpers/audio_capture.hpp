#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <memory>

namespace rouen::helpers {

struct AudioInputDevice {
    SDL_AudioDeviceID id{0};
    std::string name;
};

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    // Get list of available audio input devices (microphones)
    static std::vector<AudioInputDevice> get_input_devices();
    static SDL_AudioDeviceID find_device_id_by_name(const std::string& name_query);

    bool start(SDL_AudioDeviceID device_id = 0, int sample_rate = 44100, int channels = 2);
    void stop();
    bool is_recording() const;

    // Read available captured audio bytes (S16LE PCM format)
    std::vector<uint8_t> read_audio_data();
    float get_current_peak() const;

private:
    static void SDLCALL audio_stream_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);

    SDL_AudioStream* stream_{nullptr};
    std::atomic<bool> recording_{false};
    std::atomic<float> current_peak_{0.0f};
    int sample_rate_{44100};
    int channels_{2};

    std::mutex buffer_mutex_;
    std::vector<uint8_t> pcm_buffer_;
};

} // namespace rouen::helpers
