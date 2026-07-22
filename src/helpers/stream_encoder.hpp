#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <iostream>

class NativeStreamEncoder {
public:
    NativeStreamEncoder() = default;
    ~NativeStreamEncoder() {
        close();
    }

    bool init(const std::string& url, int width, int height, int fps) {
        avformat_network_init();

        // 1. Allocate format context for MPEG-TS streaming
        if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "mpegts", url.c_str()) < 0) {
            std::cerr << "[NativeStreamer] Failed to allocate format context" << std::endl;
            return false;
        }

        // 2. Add H.264 Video Stream (h264_videotoolbox on Mac, fallback to libx264/h264)
        const AVCodec* video_codec = avcodec_find_encoder_by_name("h264_videotoolbox");
        if (!video_codec) {
            video_codec = avcodec_find_encoder_by_name("libx264");
        }
        if (!video_codec) {
            video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        if (!video_codec) {
            std::cerr << "[NativeStreamer] H.264 encoder not found" << std::endl;
            return false;
        }

        video_stream = avformat_new_stream(fmt_ctx, nullptr);
        if (!video_stream) return false;

        video_enc_ctx = avcodec_alloc_context3(video_codec);
        if (!video_enc_ctx) return false;

        video_enc_ctx->codec_id = AV_CODEC_ID_H264;
        video_enc_ctx->width = width;
        video_enc_ctx->height = height;
        // MPEG-TS/UDP uses a generic timestamp base
        video_enc_ctx->time_base = AVRational{1, fps};
        video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        video_enc_ctx->gop_size = 4;
        
        if (std::string(video_codec->name) == "h264_videotoolbox") {
            video_enc_ctx->bit_rate = 12000000; // 12 Mbps target bitrate for crystal-clear local stream
            av_opt_set(video_enc_ctx->priv_data, "realtime", "1", 0); // Zero-latency realtime profile
        } else {
            // libx264 software fallback settings: visually lossless CRF 18
            av_opt_set(video_enc_ctx->priv_data, "preset", "superfast", 0);
            av_opt_set(video_enc_ctx->priv_data, "tune", "zerolatency", 0);
            av_opt_set(video_enc_ctx->priv_data, "crf", "18", 0);
        }

        if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(video_enc_ctx, video_codec, nullptr) < 0) {
            std::cerr << "[NativeStreamer] Failed to open video codec" << std::endl;
            return false;
        }

        avcodec_parameters_from_context(video_stream->codecpar, video_enc_ctx);
        video_stream->time_base = video_enc_ctx->time_base;

        // 3. Add AAC Audio Stream
        const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audio_codec) {
            std::cerr << "[NativeStreamer] AAC encoder not found" << std::endl;
            return false;
        }

        audio_stream = avformat_new_stream(fmt_ctx, nullptr);
        if (!audio_stream) return false;

        audio_enc_ctx = avcodec_alloc_context3(audio_codec);
        if (!audio_enc_ctx) return false;

        audio_enc_ctx->codec_id = AV_CODEC_ID_AAC;
        audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; // Native AAC uses Float Planar
        audio_enc_ctx->sample_rate = 44100;
        
        AVChannelLayout stereo_layout;
        av_channel_layout_default(&stereo_layout, 2);
        av_channel_layout_copy(&audio_enc_ctx->ch_layout, &stereo_layout);
        av_channel_layout_uninit(&stereo_layout);
        
        audio_enc_ctx->bit_rate = 128000;
        audio_enc_ctx->time_base = AVRational{1, 44100};

        if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(audio_enc_ctx, audio_codec, nullptr) < 0) {
            std::cerr << "[NativeStreamer] Failed to open audio codec" << std::endl;
            return false;
        }

        avcodec_parameters_from_context(audio_stream->codecpar, audio_enc_ctx);
        audio_stream->time_base = audio_enc_ctx->time_base;

        // 4. Open UDP network output file
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt_ctx->pb, url.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "[NativeStreamer] Failed to open output URL: " << url << std::endl;
                return false;
            }
        }

        // 5. Write header
        if (avformat_write_header(fmt_ctx, nullptr) < 0) {
            std::cerr << "[NativeStreamer] Failed to write header" << std::endl;
            return false;
        }

        // 6. Set up converters
        sws_ctx = sws_getContext(
            width, height, AV_PIX_FMT_RGBA,
            width, height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        swr_ctx = swr_alloc();
        AVChannelLayout in_layout;
        av_channel_layout_default(&in_layout, 2);
        av_opt_set_chlayout(swr_ctx, "in_chlayout", &in_layout, 0);
        av_opt_set_int(swr_ctx, "in_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

        av_opt_set_chlayout(swr_ctx, "out_chlayout", &audio_enc_ctx->ch_layout, 0);
        av_opt_set_int(swr_ctx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
        
        swr_init(swr_ctx);
        av_channel_layout_uninit(&in_layout);

        // 7. Allocate video frame
        video_frame = av_frame_alloc();
        video_frame->format = AV_PIX_FMT_YUV420P;
        video_frame->width = width;
        video_frame->height = height;
        av_frame_get_buffer(video_frame, 0);

        // 8. Allocate audio frame
        audio_frame = av_frame_alloc();
        audio_frame->format = AV_SAMPLE_FMT_FLTP;
        audio_frame->nb_samples = audio_enc_ctx->frame_size;
        av_channel_layout_copy(&audio_frame->ch_layout, &audio_enc_ctx->ch_layout);
        av_frame_get_buffer(audio_frame, 0);

        video_pts = 0;
        audio_pts = 0;

        return true;
    }

    void encode_video(const uint8_t* rgba_pixels) {
        if (!video_enc_ctx || !sws_ctx || !video_frame) return;

        av_frame_make_writable(video_frame);

        const uint8_t* src_data[1] = { rgba_pixels };
        int src_linesize[1] = { video_enc_ctx->width * 4 };
        sws_scale(
            sws_ctx, src_data, src_linesize, 0, video_enc_ctx->height,
            video_frame->data, video_frame->linesize
        );

        video_frame->pts = video_pts++;

        avcodec_send_frame(video_enc_ctx, video_frame);

        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(video_enc_ctx, pkt) >= 0) {
            av_packet_rescale_ts(pkt, video_enc_ctx->time_base, video_stream->time_base);
            pkt->stream_index = video_stream->index;

            {
                std::lock_guard<std::mutex> lock(write_mutex);
                av_interleaved_write_frame(fmt_ctx, pkt);
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }

    void encode_audio(const uint8_t* pcm_data, size_t size) {
        if (!audio_enc_ctx || !swr_ctx || !audio_frame) return;

        {
            std::lock_guard<std::mutex> lock(audio_buf_mutex);
            audio_buf.insert(audio_buf.end(), pcm_data, pcm_data + size);
        }

        const size_t bytes_per_frame = static_cast<size_t>(audio_enc_ctx->frame_size) * 4; // 1024 samples * 2 channels * 2 bytes

        while (true) {
            std::vector<uint8_t> chunk;
            {
                std::lock_guard<std::mutex> lock(audio_buf_mutex);
                if (audio_buf.size() < bytes_per_frame) break;
                chunk.assign(audio_buf.begin(), audio_buf.begin() + bytes_per_frame);
                audio_buf.erase(audio_buf.begin(), audio_buf.begin() + bytes_per_frame);
            }

            av_frame_make_writable(audio_frame);

            const uint8_t* in_data[1] = { chunk.data() };
            swr_convert(
                swr_ctx, audio_frame->data, audio_frame->nb_samples,
                in_data, audio_enc_ctx->frame_size
            );

            audio_frame->pts = audio_pts;
            audio_pts += audio_enc_ctx->frame_size;

            avcodec_send_frame(audio_enc_ctx, audio_frame);

            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(audio_enc_ctx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, audio_enc_ctx->time_base, audio_stream->time_base);
                pkt->stream_index = audio_stream->index;

                {
                    std::lock_guard<std::mutex> lock(write_mutex);
                    av_interleaved_write_frame(fmt_ctx, pkt);
                }
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
    }

    void close() {
        if (video_enc_ctx && video_stream) {
            avcodec_send_frame(video_enc_ctx, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(video_enc_ctx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, video_enc_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                std::lock_guard<std::mutex> lock(write_mutex);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        if (audio_enc_ctx && audio_stream) {
            avcodec_send_frame(audio_enc_ctx, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(audio_enc_ctx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, audio_enc_ctx->time_base, audio_stream->time_base);
                pkt->stream_index = audio_stream->index;
                std::lock_guard<std::mutex> lock(write_mutex);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        if (fmt_ctx) {
            av_write_trailer(fmt_ctx);
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&fmt_ctx->pb);
            }
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }

        if (video_enc_ctx) {
            avcodec_free_context(&video_enc_ctx);
            video_enc_ctx = nullptr;
        }
        if (audio_enc_ctx) {
            avcodec_free_context(&audio_enc_ctx);
            audio_enc_ctx = nullptr;
        }
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        if (swr_ctx) {
            swr_free(&swr_ctx);
            swr_ctx = nullptr;
        }
        if (video_frame) {
            av_frame_free(&video_frame);
            video_frame = nullptr;
        }
        if (audio_frame) {
            av_frame_free(&audio_frame);
            audio_frame = nullptr;
        }

        std::lock_guard<std::mutex> lock(audio_buf_mutex);
        audio_buf.clear();
    }

private:
    AVFormatContext* fmt_ctx{nullptr};
    AVCodecContext* video_enc_ctx{nullptr};
    AVCodecContext* audio_enc_ctx{nullptr};
    AVStream* video_stream{nullptr};
    AVStream* audio_stream{nullptr};
    SwsContext* sws_ctx{nullptr};
    SwrContext* swr_ctx{nullptr};
    AVFrame* video_frame{nullptr};
    AVFrame* audio_frame{nullptr};

    int64_t video_pts{0};
    int64_t audio_pts{0};

    std::mutex write_mutex;
    std::mutex audio_buf_mutex;
    std::vector<uint8_t> audio_buf;
};
