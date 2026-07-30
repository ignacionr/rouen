#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstddef>

// Forward declarations of FFmpeg C structs to avoid heavy FFmpeg header includes in mp4_writer.hpp
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct SwrContext;

namespace rouen::helpers {

class NativeMP4Writer {
public:
    NativeMP4Writer();
    ~NativeMP4Writer();

    NativeMP4Writer(const NativeMP4Writer&) = delete;
    NativeMP4Writer& operator=(const NativeMP4Writer&) = delete;

    bool open(const std::string& output_filename, int width = 1280, int height = 720, int fps = 30, int sample_rate = 44100);
    bool is_open() const;
    void write_video_frame(const uint8_t* rgba_pixels);
    void write_audio_samples(const uint8_t* pcm_s16_data, size_t size_in_bytes);
    void flush_audio();
    void close();

    int64_t get_video_pts() const { return video_pts_; }
    int64_t get_audio_pts() const { return audio_pts_; }

private:
    mutable std::recursive_mutex write_mutex_;
    mutable std::recursive_mutex audio_buf_mutex_;
    std::atomic<bool> is_open_{false};

    std::string output_filename_;
    int width_{1280};
    int height_{720};
    int fps_{30};
    int sample_rate_{44100};
    int video_pix_fmt_{0};

    AVFormatContext* fmt_ctx_{nullptr};
    AVCodecContext* video_enc_ctx_{nullptr};
    AVCodecContext* audio_enc_ctx_{nullptr};
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
    SwsContext* sws_ctx_{nullptr};
    SwrContext* swr_ctx_{nullptr};

    int64_t video_pts_{0};
    int64_t audio_pts_{0};
    int64_t audio_pkts_written_{0};

    std::vector<uint8_t> audio_buf_;
};

} // namespace rouen::helpers
