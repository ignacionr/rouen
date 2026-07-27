#pragma once

#include <cctype>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

inline bool is_remote_url(std::string_view url) {
    return url.starts_with("http://") || url.starts_with("https://") ||
           url.starts_with("rtmp://") || url.starts_with("rtsp://") ||
           url.find("youtube.com") != std::string_view::npos ||
           url.find("youtu.be") != std::string_view::npos;
}

inline bool is_valid_media_source(const std::filesystem::path& p, std::string_view raw_input = "") {
    if (is_remote_url(raw_input)) return true;
    if (p.empty()) return false;
    std::string path_str = p.string();
    if (is_remote_url(path_str)) return true;
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

inline bool is_supported_media_extension(std::string_view ext) {
    std::string lower_ext;
    lower_ext.reserve(ext.size());
    for (char c : ext) lower_ext += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return lower_ext == ".mp4" || lower_ext == ".mkv"  || lower_ext == ".avi" ||
           lower_ext == ".mov" || lower_ext == ".webm" || lower_ext == ".mp3" ||
           lower_ext == ".wav" || lower_ext == ".aac"  || lower_ext == ".flac" ||
           lower_ext == ".ogg" || lower_ext == ".m4a"  || lower_ext == ".wma" ||
           lower_ext == ".m4v" || lower_ext == ".mpg"  || lower_ext == ".mpeg" ||
           lower_ext == ".3gp" || lower_ext == ".opus";
}

struct media_card : public card {
    media_card(std::string_view locator = "") {
        width = 600.0f;
        // Accent Colors
        colors[0] = ImVec4(0.25f, 0.65f, 0.95f, 1.0f); // Primary - Vibrant Cyan/Blue
        colors[1] = ImVec4(0.35f, 0.75f, 0.98f, 0.7f); // Secondary
        get_color(2, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));

        if (!locator.empty()) {
            std::string path_str = ::helpers::StringHelper::url_decode(locator);
            if (path_str.starts_with("media:")) {
                path_str = path_str.substr(6);
            }
            load_media(path_str);
        } else {
            name("Media Player");
        }
    }

    void on_close() override {
        media_player::stopForOwner(this);
    }

    ~media_card() override {
        on_close();
        // Stop playback when card is closed
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr) {
                item_ptr->stopMedia();
            }
        }
    }

    std::string get_uri() const override {
        return std::format("media:{}", media_url_.empty() ? path_.string() : media_url_);
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "media" || uri.starts_with("media:");
    }

    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("media:")) {
            load_media(::helpers::StringHelper::url_decode(uri.substr(6)));
        }
    }

    void load_media(const std::string& filepath) {
        media_url_ = filepath;
        path_ = std::filesystem::path(filepath);
        if (is_remote_url(media_url_)) {
            if (media_url_.find("youtube.com") != std::string::npos || media_url_.find("youtu.be") != std::string::npos) {
                card_title_ = "YouTube Video";
            } else {
                card_title_ = "Media Stream";
            }
        } else {
            card_title_ = path_.filename().empty() ? "Media Player" : path_.filename().string();
        }
        name(card_title_);
        auto_started_ = false;
        active_item_id_ = 0;

        if (!media_url_.empty() && is_valid_media_source(path_, media_url_)) {
            std::string media_url = media_url_;
            ImGuiID player_item_id = ImHashData(media_url.data(), media_url.size(), 0);

            active_item_id_ = player_item_id;

            auto item_ptr = media_player::get_item_ptr(player_item_id);
            if (item_ptr) {
                if (!item_ptr->is_playing || item_ptr->url != media_url) {
                    item_ptr->url = media_url;
                    item_ptr->start_offset = 0.0;
                    item_ptr->playMedia(this);
                }
                auto_started_ = true;
            }
        }
    }

    bool render() override {
        return render_window([this]() {
            if (media_url_.empty() || !is_valid_media_source(path_, media_url_)) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No valid media file or stream loaded.");
                ImGui::Spacing();
                
                static char path_buf[512] = "";
                ImGui::Text("Open Media File Path or URL:");
                ImGui::InputText("##media_path", path_buf, sizeof(path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Play Media")) {
                    load_media(path_buf);
                }
                return;
            }

            std::string media_url = media_url_;
            ImGuiID player_item_id = ImHashData(media_url.data(), media_url.size(), 0);
            active_item_id_ = player_item_id;

            // Auto-start playback on initial load
            if (!auto_started_) {
                auto_started_ = true;
                media_player::stopAll();
                auto item_ptr = media_player::get_item_ptr(player_item_id);
                if (item_ptr) {
                    item_ptr->url = media_url;
                    item_ptr->start_offset = 0.0;
                    item_ptr->playMedia(this);
                }
            }

            // Render expanded active media player UI
            std::string display_title = card_title_.empty() ? "Media Player" : card_title_;
            media_player::player(media_url, colors[0], display_title, -1, "", "", media_player::get_dummy_watermark(), true, 0.0f, this);
        });
    }

private:
    std::filesystem::path path_;
    std::string media_url_;
    std::string card_title_{"Media Player"};
    bool auto_started_{false};
    ImGuiID active_item_id_{0};
};

} // namespace rouen::cards
