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
        // Accent Colors
        colors[0] = ImVec4(0.25f, 0.65f, 0.95f, 1.0f); // Primary - Vibrant Cyan/Blue
        colors[1] = ImVec4(0.35f, 0.75f, 0.98f, 0.7f); // Secondary
        get_color(2, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));

        if (!locator.empty()) {
            std::string path_str = ::helpers::StringHelper::url_decode(locator);
            load_media(path_str);
        } else {
            name("Media Player");
        }
    }

    ~media_card() override {
        // Stop playback when card is closed
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr) {
                item_ptr->stopMedia();
            }
        }
    }

    std::string get_uri() const override {
        return std::format("media:{}", path_.string());
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
        path_ = std::filesystem::path(filepath);
        name(path_.filename().empty() ? "Media Player" : path_.filename().string());
        auto_started_ = false;
        active_item_id_ = 0;
    }

    bool render() override {
        return render_window([this]() {
            if (path_.empty() || !std::filesystem::exists(path_)) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No valid media file loaded.");
                ImGui::Spacing();
                
                static char path_buf[512] = "";
                ImGui::Text("Open Media File Path:");
                ImGui::InputText("##media_path", path_buf, sizeof(path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Play Media")) {
                    load_media(path_buf);
                }
                return;
            }

            std::string media_url = path_.string();

            // Compute exact ImGuiID matching media_player::player ID stack
            ImGui::PushID(media_url.data(), media_url.data() + media_url.size());
            ImGuiID player_item_id = ImGui::GetID("MediaPlayer");
            ImGui::PopID();

            active_item_id_ = player_item_id;

            // Auto-start playback on initial load
            if (!auto_started_) {
                auto_started_ = true;
                media_player::stopAll();
                auto item_ptr = media_player::get_item_ptr(player_item_id);
                if (item_ptr) {
                    item_ptr->url = media_url;
                    item_ptr->start_offset = 0.0;
                    item_ptr->playMedia();
                }
            }

            // Render expanded active media player UI
            media_player::player(media_url, colors[0], path_.filename().string(), -1, "", "", media_player::get_dummy_watermark(), true);
        });
    }

private:
    std::filesystem::path path_;
    bool auto_started_{false};
    ImGuiID active_item_id_{0};
};

} // namespace rouen::cards
