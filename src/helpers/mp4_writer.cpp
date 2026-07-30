#include "mp4_writer.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
}

#include <iostream>

namespace rouen::helpers {

NativeMP4Writer::NativeMP4Writer() = default;

NativeMP4Writer::~NativeMP4Writer() {
    close();
}

bool NativeMP4Writer::open(const std::string& output_filename, int width, int height, int fps, int sample_rate) {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    close();

    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;
    if (fps <= 0) fps = 30;

    width_ = width;
    height_ = height;
    fps_ = fps;
    sample_rate_ = sample_rate;
    output_filename_ = output_filename;

    std::cout << "[NativeMP4Writer] Initializing target: " << output_filename << " (" << width << "x" << height << " @ " << fps << " fps)" << std::endl;

    // 1. Allocate format context for MP4 output file
    if (avformat_alloc_output_context2(&fmt_ctx_, nullptr, "mp4", output_filename.c_str()) < 0) {
        std::cerr << "[NativeMP4Writer] Failed to allocate MP4 output format context for " << output_filename << std::endl;
        return false;
    }

    // 2. Setup Video Stream & Encoder
    const AVCodec* video_codec = avcodec_find_encoder_by_name("libx264");
    std::string fallback_name = "libx264";
    if (!video_codec) {
        video_codec = avcodec_find_encoder_by_name("h264_videotoolbox");
        fallback_name = "h264_videotoolbox";
    }
    if (!video_codec) {
        std::cerr << "[NativeMP4Writer] Video encoder not found" << std::endl;
        return false;
    }

    std::cout << "[NativeMP4Writer] Selected video codec: " << video_codec->name << std::endl;

    video_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!video_stream_) return false;
    video_stream_->id = 0;

    video_enc_ctx_ = avcodec_alloc_context3(video_codec);
    if (!video_enc_ctx_) return false;

    video_enc_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    video_enc_ctx_->codec_id = video_codec->id;
    video_enc_ctx_->width = width;
    video_enc_ctx_->height = height;
    video_enc_ctx_->time_base = AVRational{1, fps};
    video_enc_ctx_->framerate = AVRational{fps, 1};
    video_enc_ctx_->gop_size = 1;
    video_enc_ctx_->max_b_frames = 0;
    video_enc_ctx_->bit_rate = 4000000;
    video_enc_ctx_->thread_count = 1;
    video_enc_ctx_->thread_type = 0;

    std::string res_str = std::to_string(width) + "x" + std::to_string(height);
    av_opt_set(video_enc_ctx_, "video_size", res_str.c_str(), 0);

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "threads", "1", 0);
    std::string codec_name = video_codec->name ? video_codec->name : "";
    if (codec_name == "h264_videotoolbox") {
        video_pix_fmt_ = static_cast<int>(AV_PIX_FMT_NV12);
        video_enc_ctx_->pix_fmt = AV_PIX_FMT_NV12;
        av_opt_set_pixel_fmt(video_enc_ctx_, "pixel_format", AV_PIX_FMT_NV12, 0);
        av_dict_set(&opts, "realtime", "1", 0);
    } else {
        video_pix_fmt_ = static_cast<int>(AV_PIX_FMT_YUV420P);
        video_enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        av_opt_set_pixel_fmt(video_enc_ctx_, "pixel_format", AV_PIX_FMT_YUV420P, 0);
        av_dict_set(&opts, "preset", "ultrafast", 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
    }

    if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        video_enc_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int open_res = avcodec_open2(video_enc_ctx_, video_codec, &opts);
    av_dict_free(&opts);

    if (open_res < 0) {
        char errbuf[256];
        av_strerror(open_res, errbuf, sizeof(errbuf));
        std::cerr << "[NativeMP4Writer] Primary video codec " << codec_name << " failed (" << errbuf << "), trying fallback..." << std::endl;
        avcodec_free_context(&video_enc_ctx_);

        video_codec = (codec_name == "h264_videotoolbox") ? avcodec_find_encoder_by_name("libx264") : avcodec_find_encoder_by_name("h264_videotoolbox");
        if (!video_codec) video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!video_codec) return false;

        fallback_name = video_codec->name ? video_codec->name : "";
        std::cout << "[NativeMP4Writer] Trying fallback video codec: " << fallback_name << std::endl;

        video_enc_ctx_ = avcodec_alloc_context3(video_codec);
        if (!video_enc_ctx_) return false;

        video_enc_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
        video_enc_ctx_->codec_id = video_codec->id;
        video_enc_ctx_->width = width;
        video_enc_ctx_->height = height;
        video_enc_ctx_->time_base = AVRational{1, fps};
        video_enc_ctx_->framerate = AVRational{fps, 1};
        video_enc_ctx_->gop_size = fps;
        video_enc_ctx_->max_b_frames = 0;
        video_enc_ctx_->bit_rate = 4000000;

        av_opt_set(video_enc_ctx_, "video_size", res_str.c_str(), 0);

        AVDictionary* fallback_opts = nullptr;
        if (fallback_name == "h264_videotoolbox") {
            video_enc_ctx_->pix_fmt = AV_PIX_FMT_NV12;
            av_opt_set_pixel_fmt(video_enc_ctx_, "pixel_format", AV_PIX_FMT_NV12, 0);
            av_dict_set(&fallback_opts, "realtime", "1", 0);
        } else {
            video_enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
            av_opt_set_pixel_fmt(video_enc_ctx_, "pixel_format", AV_PIX_FMT_YUV420P, 0);
            av_dict_set(&fallback_opts, "preset", "ultrafast", 0);
            av_dict_set(&fallback_opts, "tune", "zerolatency", 0);
        }

        if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            video_enc_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        int fb_res = avcodec_open2(video_enc_ctx_, video_codec, &fallback_opts);
        av_dict_free(&fallback_opts);

        if (fb_res < 0) {
            char fbuf[256];
            av_strerror(fb_res, fbuf, sizeof(fbuf));
            std::cerr << "[NativeMP4Writer] Fallback video codec open failed: " << fbuf << std::endl;
            return false;
        }
    }

    avcodec_parameters_from_context(video_stream_->codecpar, video_enc_ctx_);
    video_stream_->time_base = video_enc_ctx_->time_base;

    // 3. Setup AAC Audio Stream (if requested)
    if (sample_rate > 0) {
        const AVCodec* audio_codec = avcodec_find_encoder_by_name("aac");
        if (!audio_codec) audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audio_codec) {
            std::cerr << "[NativeMP4Writer] AAC audio encoder not found" << std::endl;
            return false;
        }

        audio_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
        if (!audio_stream_) return false;
        audio_stream_->id = 1;

        audio_enc_ctx_ = avcodec_alloc_context3(audio_codec);
        if (!audio_enc_ctx_) return false;

        audio_enc_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_enc_ctx_->codec_id = AV_CODEC_ID_AAC;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        if (audio_codec->sample_fmts) {
            audio_enc_ctx_->sample_fmt = audio_codec->sample_fmts[0];
        } else {
            audio_enc_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        std::cout << "[NativeMP4Writer] Selected audio sample_fmt: " << av_get_sample_fmt_name(audio_enc_ctx_->sample_fmt) << std::endl;
        audio_enc_ctx_->sample_rate = sample_rate;
        
        std::memset(&audio_enc_ctx_->ch_layout, 0, sizeof(audio_enc_ctx_->ch_layout));
        audio_enc_ctx_->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
        audio_enc_ctx_->ch_layout.nb_channels = 2;
        audio_enc_ctx_->ch_layout.u.mask = AV_CH_LAYOUT_STEREO;

        audio_enc_ctx_->bit_rate = 192000;
        audio_enc_ctx_->profile = AV_PROFILE_AAC_LOW;
        audio_enc_ctx_->thread_count = 1;
        audio_enc_ctx_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        audio_enc_ctx_->time_base = AVRational{1, sample_rate};

        if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_enc_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(audio_enc_ctx_, audio_codec, nullptr) < 0) {
            std::cerr << "[NativeMP4Writer] Failed to open AAC audio codec" << std::endl;
            return false;
        }
        std::cout << "[NativeMP4Writer] audio codec name: " << audio_codec->name << " ctx_codec_name: " << (audio_enc_ctx_->codec ? audio_enc_ctx_->codec->name : "null") << " frame_size=" << audio_enc_ctx_->frame_size << " caps=" << audio_codec->capabilities << std::endl;
        avcodec_parameters_from_context(audio_stream_->codecpar, audio_enc_ctx_);
        audio_stream_->codecpar->codec_tag = 0;
        audio_stream_->time_base = audio_enc_ctx_->time_base;
    }

    // 4. Open destination file
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        if (std::filesystem::exists(output_filename)) {
            std::error_code ec;
            std::filesystem::remove(output_filename, ec);
        }
        if (avio_open(&fmt_ctx_->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "[NativeMP4Writer] Failed to open output MP4 file: " << output_filename << std::endl;
            return false;
        }
    }

    // 5. Write file header
    std::cout << "[NativeMP4Writer] nb_streams before header: " << fmt_ctx_->nb_streams << std::endl;
    if (avformat_write_header(fmt_ctx_, nullptr) < 0) {
        std::cerr << "[NativeMP4Writer] Failed to write MP4 header" << std::endl;
        return false;
    }

    std::cout << "[NativeMP4Writer] Created MP4 target with " << fmt_ctx_->nb_streams << " streams (Video stream index=" << video_stream_->index << ", Audio stream index=" << (audio_stream_ ? std::to_string(audio_stream_->index) : "none") << ")" << std::endl;

    // 6. Setup SWS and SWR converters
    sws_ctx_ = sws_getContext(
        width, height, AV_PIX_FMT_RGBA,
        width, height, video_enc_ctx_->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (audio_enc_ctx_) {
        AVChannelLayout in_layout{};
        AVChannelLayout out_layout{};
        av_channel_layout_default(&in_layout, 2);
        av_channel_layout_default(&out_layout, 2);
        int swr_res = swr_alloc_set_opts2(
            &swr_ctx_,
            &out_layout,
            audio_enc_ctx_->sample_fmt,
            sample_rate,
            &in_layout,
            AV_SAMPLE_FMT_S16,
            sample_rate,
            0, nullptr
        );
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);
        if (swr_res < 0 || !swr_ctx_ || swr_init(swr_ctx_) < 0) {
            std::cerr << "[NativeMP4Writer] Failed to initialize SWR context" << std::endl;
            return false;
        }
    }

    video_pts_ = 0;
    audio_pts_ = 0;
    audio_pkts_written_ = 0;
    is_open_.store(true);

    std::cout << "[NativeMP4Writer] Successfully opened and created target MP4 file: " << output_filename << std::endl;
    return true;
}

bool NativeMP4Writer::is_open() const {
    return is_open_.load();
}

void NativeMP4Writer::write_video_frame(const uint8_t* rgba_pixels) {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    if (!is_open_.load() || !fmt_ctx_ || !video_enc_ctx_ || !video_stream_ || !sws_ctx_ || !rgba_pixels) return;

    AVFrame* frame = av_frame_alloc();
    if (!frame) return;

    frame->format = static_cast<AVPixelFormat>(video_pix_fmt_);
    frame->width = width_;
    frame->height = height_;
    frame->pts = video_pts_++;
    frame->time_base = video_enc_ctx_->time_base;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return;
    }

    const uint8_t* src_data[4] = { rgba_pixels, nullptr, nullptr, nullptr };
    int src_linesize[4] = { width_ * 4, 0, 0, 0 };
    sws_scale(
        sws_ctx_, src_data, src_linesize, 0, height_,
        frame->data, frame->linesize
    );

    int send_res = avcodec_send_frame(video_enc_ctx_, frame);
    if (send_res == AVERROR(EAGAIN)) {
        AVPacket* pkt = av_packet_alloc();
        if (pkt) {
            while (avcodec_receive_packet(video_enc_ctx_, pkt) == 0) {
                if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = pkt->pts;
                av_packet_rescale_ts(pkt, video_enc_ctx_->time_base, video_stream_->time_base);
                pkt->stream_index = video_stream_->index;
                av_interleaved_write_frame(fmt_ctx_, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
        send_res = avcodec_send_frame(video_enc_ctx_, frame);
    }
    av_frame_free(&frame);

    if (send_res < 0) {
        char errbuf[256];
        av_strerror(send_res, errbuf, sizeof(errbuf));
        std::cerr << "[NativeMP4Writer] Video send frame error: " << errbuf << std::endl;
    }

    AVPacket* pkt = av_packet_alloc();
    if (pkt) {
        while (avcodec_receive_packet(video_enc_ctx_, pkt) == 0) {
            if (pkt->dts == AV_NOPTS_VALUE) {
                pkt->dts = pkt->pts;
            }
            av_packet_rescale_ts(pkt, video_enc_ctx_->time_base, video_stream_->time_base);
            pkt->stream_index = video_stream_->index;

            int ret = av_interleaved_write_frame(fmt_ctx_, pkt);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::cerr << "[NativeMP4Writer] Video write frame error: " << errbuf << std::endl;
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
}

void NativeMP4Writer::write_audio_samples(const uint8_t* pcm_s16_data, size_t size_in_bytes) {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    static int dbg_call_count = 0;
    if (++dbg_call_count <= 3) {
        std::cout << "[NativeMP4Writer] write_audio_samples called: is_open=" << is_open_.load()
                  << " audio_enc_ctx_=" << static_cast<void*>(audio_enc_ctx_)
                  << " swr_ctx_=" << static_cast<void*>(swr_ctx_)
                  << " size=" << size_in_bytes << std::endl;
    }
    if (!is_open_.load() || !fmt_ctx_ || !audio_enc_ctx_ || !audio_stream_ || !swr_ctx_) {
        return;
    }

    if (pcm_s16_data && size_in_bytes > 0) {
        std::lock_guard<std::recursive_mutex> ab_lock(audio_buf_mutex_);
        audio_buf_.insert(audio_buf_.end(), pcm_s16_data, pcm_s16_data + size_in_bytes);
    }

    int frame_sz = (audio_enc_ctx_ && audio_enc_ctx_->frame_size > 0) ? audio_enc_ctx_->frame_size : 1024;
    const size_t bytes_per_frame = static_cast<size_t>(frame_sz) * 4;
    if (bytes_per_frame == 0) return;

    while (true) {
        if (video_pts_ > 0) {
            double current_audio_sec = static_cast<double>(audio_pts_) / 44100.0;
            double current_video_sec = static_cast<double>(video_pts_) / 30.0;
            if (current_audio_sec >= current_video_sec + 0.5) {
                break;
            }
        }
        std::vector<uint8_t> chunk;
        {
            std::lock_guard<std::recursive_mutex> ab_lock(audio_buf_mutex_);
            if (audio_buf_.size() < bytes_per_frame) break;
            auto offset = static_cast<std::ptrdiff_t>(bytes_per_frame);
            chunk.assign(audio_buf_.begin(), audio_buf_.begin() + offset);
            audio_buf_.erase(audio_buf_.begin(), audio_buf_.begin() + offset);
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) break;

        frame->format = audio_enc_ctx_->sample_fmt;
        frame->sample_rate = audio_enc_ctx_->sample_rate;
        frame->time_base = audio_enc_ctx_->time_base;
        frame->nb_samples = audio_enc_ctx_->frame_size;
        frame->ch_layout = audio_enc_ctx_->ch_layout;

        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            break;
        }

        const uint8_t* in_data[8] = { chunk.data(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        int swr_res = swr_convert(
            swr_ctx_, frame->data, frame->nb_samples,
            in_data, frame->nb_samples
        );
        if (swr_res < 0) {
            char errbuf[256];
            av_strerror(swr_res, errbuf, sizeof(errbuf));
            std::cout << "[NativeMP4Writer] swr_convert error: " << errbuf << std::endl;
            av_frame_free(&frame);
            break;
        }

        frame->pts = audio_pts_;
        audio_pts_ += audio_enc_ctx_->frame_size;

        int send_res = avcodec_send_frame(audio_enc_ctx_, frame);
        av_frame_free(&frame);

        if (send_res < 0) {
            char errbuf[256];
            av_strerror(send_res, errbuf, sizeof(errbuf));
            std::cerr << "[NativeMP4Writer] Audio send frame error: " << errbuf << std::endl;
        }

        AVPacket* pkt = av_packet_alloc();
        int pkt_count = 0;
        if (pkt) {
            while (avcodec_receive_packet(audio_enc_ctx_, pkt) == 0) {
                ++pkt_count;
                if (pkt->duration == 0) {
                    pkt->duration = audio_enc_ctx_->frame_size;
                }
                if (pkt->dts == AV_NOPTS_VALUE) {
                    pkt->dts = pkt->pts;
                }
                av_packet_rescale_ts(pkt, audio_enc_ctx_->time_base, audio_stream_->time_base);
                pkt->stream_index = audio_stream_->index;

                int ret = av_interleaved_write_frame(fmt_ctx_, pkt);
                if (ret < 0) {
                    char errbuf[256];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    std::cerr << "[NativeMP4Writer] Audio write frame error (" << ret << "): " << errbuf << std::endl;
                }
                av_packet_unref(pkt);
            }
            if (pkt_count == 0) {
                static int dbg_no_pkt = 0;
                if (++dbg_no_pkt <= 3)
                    std::cout << "[NativeMP4Writer] Audio: send_res=" << send_res << " no packets received (AAC may be buffering)" << std::endl;
            } else {
                audio_pkts_written_ += pkt_count;
                if (audio_pkts_written_ <= 10 || audio_pkts_written_ % 20 == 0)
                    std::cout << "[NativeMP4Writer] audio_pkts_written_=" << audio_pkts_written_ << std::endl;
            }
            av_packet_free(&pkt);
        }
    }
}

void NativeMP4Writer::flush_audio() {
    write_audio_samples(nullptr, 0);
}

void NativeMP4Writer::close() {
    std::lock_guard<std::recursive_mutex> close_lock(write_mutex_);
    if (!fmt_ctx_) {
        is_open_.store(false);
        return;
    }

    if (audio_enc_ctx_ && audio_stream_ && swr_ctx_) {
        // Trim leftover unencoded audio buffer to match video end PTS cleanly
        if (fps_ > 0 && sample_rate_ > 0 && video_pts_ > 0) {
            double video_dur_sec = static_cast<double>(video_pts_) / fps_;
            double audio_dur_sec = static_cast<double>(audio_pts_) / sample_rate_;
            if (audio_dur_sec >= video_dur_sec) {
                std::lock_guard<std::recursive_mutex> ab_lock(audio_buf_mutex_);
                audio_buf_.clear();
            }
        }

        const size_t bytes_per_frame = static_cast<size_t>(audio_enc_ctx_->frame_size) * 4;
        {
            std::lock_guard<std::recursive_mutex> ab_lock(audio_buf_mutex_);
            if (audio_buf_.empty() && audio_pts_ == 0) {
                audio_buf_.resize(bytes_per_frame, 0);
            }
            size_t rem = audio_buf_.size() % bytes_per_frame;
            if (rem > 0) {
                audio_buf_.resize(audio_buf_.size() + (bytes_per_frame - rem), 0);
            }
        }
        int saved_fps = fps_;
        fps_ = 0;
        write_audio_samples(nullptr, 0);
        fps_ = saved_fps;

        std::lock_guard<std::recursive_mutex> ab_lock(audio_buf_mutex_);
        audio_buf_.clear();
    }
    is_open_.store(false);

    if (video_enc_ctx_) avcodec_send_frame(video_enc_ctx_, nullptr);
    if (audio_enc_ctx_) avcodec_send_frame(audio_enc_ctx_, nullptr);

    AVPacket* flush_pkt = av_packet_alloc();
    if (flush_pkt) {
        bool video_done = !video_enc_ctx_;
        bool audio_done = !audio_enc_ctx_;

        while (!video_done || !audio_done) {
            if (!video_done) {
                int res = avcodec_receive_packet(video_enc_ctx_, flush_pkt);
                if (res == 0) {
                    av_packet_rescale_ts(flush_pkt, video_enc_ctx_->time_base, video_stream_->time_base);
                    flush_pkt->stream_index = video_stream_->index;
                    av_interleaved_write_frame(fmt_ctx_, flush_pkt);
                    av_packet_unref(flush_pkt);
                } else {
                    video_done = true;
                }
            }
            if (!audio_done) {
                int res = avcodec_receive_packet(audio_enc_ctx_, flush_pkt);
                if (res == 0) {
                    if (flush_pkt->duration == 0) flush_pkt->duration = audio_enc_ctx_->frame_size;
                    if (flush_pkt->dts == AV_NOPTS_VALUE) flush_pkt->dts = flush_pkt->pts;
                    if (audio_stream_->time_base.den > 0 && audio_enc_ctx_->time_base.den > 0) {
                        av_packet_rescale_ts(flush_pkt, audio_enc_ctx_->time_base, audio_stream_->time_base);
                    }
                    flush_pkt->stream_index = audio_stream_->index;
                    int ret = av_interleaved_write_frame(fmt_ctx_, flush_pkt);
                    if (ret < 0) {
                        char errbuf[256];
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        std::cerr << "[NativeMP4Writer] Flush audio write error: " << errbuf << std::endl;
                    }
                    av_packet_unref(flush_pkt);
                } else {
                    audio_done = true;
                }
            }
        }
        av_packet_free(&flush_pkt);
    }

    if (fmt_ctx_) {
        if (fmt_ctx_->pb) avio_flush(fmt_ctx_->pb);
        int ret = av_write_trailer(fmt_ctx_);
        std::cout << "[NativeMP4Writer] av_write_trailer result: " << ret << std::endl;
        if (fmt_ctx_->pb && !(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_flush(fmt_ctx_->pb);
            avio_closep(&fmt_ctx_->pb);
        }
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }

    if (video_enc_ctx_) {
        avcodec_free_context(&video_enc_ctx_);
        video_enc_ctx_ = nullptr;
    }
    if (audio_enc_ctx_) {
        avcodec_free_context(&audio_enc_ctx_);
        audio_enc_ctx_ = nullptr;
    }
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    video_stream_ = nullptr;
    audio_stream_ = nullptr;
    is_open_.store(false);

    std::cout << "[NativeMP4Writer] Closed and flushed MP4 file to disk: " << output_filename_ << std::endl;
}

} // namespace rouen::helpers
