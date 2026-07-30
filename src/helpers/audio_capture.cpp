#include "audio_capture.hpp"
#include "mac_mic_permissions.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rouen::helpers {

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    stop();
}

std::vector<AudioInputDevice> AudioCapture::get_input_devices() {
    rouen::platform::request_mac_microphone_permission();
    std::vector<AudioInputDevice> devices;
    int count = 0;
    SDL_AudioDeviceID* dev_ids = SDL_GetAudioRecordingDevices(&count);
    if (dev_ids) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetAudioDeviceName(dev_ids[i]);
            devices.push_back({
                dev_ids[i],
                name ? name : "Default Microphone"
            });
        }
        SDL_free(dev_ids);
    }
    return devices;
}

SDL_AudioDeviceID AudioCapture::find_device_id_by_name(const std::string& name_query) {
    if (name_query.empty()) return 0;
    auto devices = get_input_devices();
    std::string lower_query = name_query;
    for (auto& c : lower_query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& dev : devices) {
        std::string lower_dev = dev.name;
        for (auto& c : lower_dev) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_dev.find(lower_query) != std::string::npos) {
            std::cout << "[AudioCapture] Matched device '" << dev.name << "' (ID: " << dev.id << ") for query '" << name_query << "'" << std::endl;
            return dev.id;
        }
    }
    std::cout << "[AudioCapture] No matching device found for query '" << name_query << "'. Using default recording device." << std::endl;
    return 0;
}

bool AudioCapture::start(SDL_AudioDeviceID device_id, int sample_rate, int channels) {
    stop();
    rouen::platform::request_mac_microphone_permission();

    sample_rate_ = sample_rate;
    channels_ = channels;

    SDL_AudioSpec wanted_spec{};
    wanted_spec.format = SDL_AUDIO_S16LE;
    wanted_spec.channels = channels;
    wanted_spec.freq = sample_rate;

    SDL_AudioDeviceID target_dev = (device_id == 0) ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : device_id;

    stream_ = SDL_OpenAudioDeviceStream(
        target_dev,
        &wanted_spec,
        audio_stream_callback,
        this
    );

    if (!stream_ && target_dev != SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
        std::cerr << "[AudioCapture] Failed to open specified device " << device_id << ", falling back to default recording device..." << std::endl;
        target_dev = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        stream_ = SDL_OpenAudioDeviceStream(
            target_dev,
            &wanted_spec,
            audio_stream_callback,
            this
        );
    }

    if (!stream_) {
        std::cerr << "[AudioCapture] WARNING: Could not open audio input stream: " << SDL_GetError() << ". Using silent PCM fallback." << std::endl;
        recording_.store(true);
        return true;
    }

    recording_.store(true);
    SDL_ResumeAudioStreamDevice(stream_);
    std::cout << "[AudioCapture] Successfully started audio capture stream on device ID " << target_dev << std::endl;
    return true;
}

void AudioCapture::stop() {
    if (recording_.exchange(false)) {
        if (stream_) {
            SDL_PauseAudioStreamDevice(stream_);
            SDL_UnbindAudioStream(stream_);
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        pcm_buffer_.clear();
        std::cout << "[AudioCapture] Stopped audio capture" << std::endl;
    }
}

bool AudioCapture::is_recording() const {
    return recording_.load();
}

std::vector<uint8_t> AudioCapture::read_audio_data() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::vector<uint8_t> result = std::move(pcm_buffer_);
    pcm_buffer_.clear();
    return result;
}

float AudioCapture::get_current_peak() const {
    return current_peak_.load();
}

void SDLCALL AudioCapture::audio_stream_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int /*total_amount*/) {
    auto* self = static_cast<AudioCapture*>(userdata);
    if (!self || !self->recording_.load()) return;

    int bytes_to_read = (additional_amount > 0) ? additional_amount : SDL_GetAudioStreamAvailable(stream);
    if (bytes_to_read > 0) {
        std::vector<uint8_t> temp(static_cast<size_t>(bytes_to_read));
        int bytes_read = SDL_GetAudioStreamData(stream, temp.data(), bytes_to_read);
        if (bytes_read > 0) {
            temp.resize(static_cast<size_t>(bytes_read));
            
            // Calculate VU peak for visualization
            const int16_t* samples = reinterpret_cast<const int16_t*>(temp.data());
            size_t num_samples = static_cast<size_t>(bytes_read) / sizeof(int16_t);
            int16_t max_val = 0;
            for (size_t i = 0; i < num_samples; ++i) {
                int16_t val = static_cast<int16_t>(std::abs(static_cast<int>(samples[i])));
                if (val > max_val) max_val = val;
            }
            float peak = static_cast<float>(max_val) / 32767.0f;
            self->current_peak_.store(peak);

            std::lock_guard<std::mutex> lock(self->buffer_mutex_);
            self->pcm_buffer_.insert(self->pcm_buffer_.end(), temp.begin(), temp.end());
        }
    }
}

} // namespace rouen::helpers
