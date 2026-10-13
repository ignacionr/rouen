#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
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
                stream_source_ = "YouTube Live / Stream";
            } else {
                card_title_ = "Media Stream";
                stream_source_ = "HLS / AAC Audio (320 kbps)";
            }
        } else {
            card_title_ = path_.filename().empty() ? "Media Player" : path_.filename().string();
            stream_source_ = std::format("Local File ({})", path_.extension().string());
        }
        name(card_title_);
        auto_started_ = false;
        active_item_id_ = 0;
        current_position_ms_ = 0;

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
                is_playing_ = true;
                is_paused_ = false;
            }
        }
    }

    static std::string format_time_ms(long long ms) {
        if (ms < 0) ms = 0;
        double const seconds = static_cast<double>(ms) / 1000.0;
        auto const hours = static_cast<int>(seconds) / 3600;
        int const minutes = (static_cast<int>(seconds) % 3600) / 60;
        auto const secs = static_cast<int>(seconds) % 60;
        if (hours > 0) {
            return std::format("{:02d}:{:02d}:{:02d}", hours, minutes, secs);
        } else {
            return std::format("{:02d}:{:02d}", minutes, secs);
        }
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container
        glz::json_t header;
        header["type"] = "Container";
        header["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Column 1: Emoji icon
        glz::json_t icon_col;
        icon_col["type"] = "Column";
        icon_col["width"] = "auto";
        std::vector<glz::json_t> icon_items;
        glz::json_t icon_tb;
        icon_tb["type"] = "TextBlock";
        icon_tb["text"] = "\xF0\x9F\x8E\xB5"; // 🎵
        icon_tb["size"] = "Large";
        icon_items.push_back(std::move(icon_tb));
        icon_col["items"] = std::move(icon_items);
        columns.push_back(std::move(icon_col));

        // Column 2: Title & subtitle
        glz::json_t title_col;
        title_col["type"] = "Column";
        title_col["width"] = "stretch";
        std::vector<glz::json_t> title_items;
        glz::json_t title_tb;
        title_tb["type"] = "TextBlock";
        title_tb["text"] = (card_title_.empty() || card_title_ == "Media Player") ? "Media Streaming Player" : card_title_;
        title_tb["weight"] = "Bolder";
        title_tb["size"] = "Large";
        title_items.push_back(std::move(title_tb));

        glz::json_t subtitle_tb;
        subtitle_tb["type"] = "TextBlock";
        subtitle_tb["text"] = media_url_.empty() ? "https://stream.rouen.live/master.m3u8" : media_url_;
        subtitle_tb["isSubtle"] = true;
        subtitle_tb["size"] = "Small";
        title_items.push_back(std::move(subtitle_tb));
        title_col["items"] = std::move(title_items);
        columns.push_back(std::move(title_col));

        // Column 3: Status Badge
        glz::json_t status_col;
        status_col["type"] = "Column";
        status_col["width"] = "auto";
        std::vector<glz::json_t> status_items;
        glz::json_t status_tb;
        status_tb["type"] = "TextBlock";
        if (is_playing()) {
            status_tb["text"] = "PLAYING";
            status_tb["weight"] = "Bolder";
            status_tb["color"] = "Good";
            status_tb["size"] = "Small";
        } else if (is_paused()) {
            status_tb["text"] = "PAUSED";
            status_tb["weight"] = "Bolder";
            status_tb["color"] = "Warning";
            status_tb["size"] = "Small";
        } else {
            status_tb["text"] = "STOPPED";
            status_tb["weight"] = "Bolder";
            status_tb["color"] = "Attention";
            status_tb["size"] = "Small";
        }
        status_items.push_back(std::move(status_tb));
        status_col["items"] = std::move(status_items);
        columns.push_back(std::move(status_col));

        col_set["columns"] = std::move(columns);
        std::vector<glz::json_t> header_items;
        header_items.push_back(std::move(col_set));
        header["items"] = std::move(header_items);
        body.push_back(std::move(header));

        // 2. FactSet
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        std::string stream_src = stream_source_;
        if (stream_src.empty()) {
            stream_src = "HLS / AAC Audio (320 kbps)";
        }
        facts.push_back(glz::json_t::object_t{{"title", "Stream Source:"}, {"value", stream_src}});

        std::string playback_time;
        long long cur_ms = get_current_position_ms();
        long long dur_ms = get_duration_ms();
        if (cur_ms == 0 && dur_ms == 0 && !is_playing() && !is_paused()) {
            playback_time = "03:45 / 45:00";
        } else {
            playback_time = std::format("{} / {}", format_time_ms(cur_ms), format_time_ms(dur_ms));
        }
        facts.push_back(glz::json_t::object_t{{"title", "Playback Time:"}, {"value", playback_time}});

        int vol = get_volume_percent();
        facts.push_back(glz::json_t::object_t{{"title", "Volume Level:"}, {"value", std::format("{}%", vol)}});
        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));

        // 3. Volume Container
        glz::json_t vol_container;
        vol_container["type"] = "Container";
        vol_container["separator"] = true;
        std::vector<glz::json_t> vol_items;

        glz::json_t vol_slider;
        vol_slider["type"] = "Input.Number";
        vol_slider["id"] = "volume_slider";
        vol_slider["label"] = "Volume (0 - 100)";
        vol_slider["value"] = vol;
        vol_slider["min"] = 0.0;
        vol_slider["max"] = 100.0;
        vol_items.push_back(std::move(vol_slider));
        vol_container["items"] = std::move(vol_items);
        body.push_back(std::move(vol_container));

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        actions.push_back(glz::json_t::object_t{
            {"type", "Action.Execute"},
            {"title", "Play"},
            {"verb", "media_play"}
        });
        actions.push_back(glz::json_t::object_t{
            {"type", "Action.Execute"},
            {"title", "Pause"},
            {"verb", "media_pause"}
        });
        actions.push_back(glz::json_t::object_t{
            {"type", "Action.Execute"},
            {"title", "Stop"},
            {"verb", "media_stop"}
        });
        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;

        try {
            std::string verb;
            glz::json_t data{};
            glz::json_t payload{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }
                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            auto extract_number = [](const glz::json_t& obj, const std::string& key) -> std::optional<double> {
                if (!obj.is_object() || !obj.contains(key)) return std::nullopt;
                const auto& val = obj[key];
                if (val.holds<double>()) return val.get<double>();
                if (val.holds<std::string>()) {
                    try {
                        return std::stod(val.get<std::string>());
                    } catch (...) {}
                }
                return std::nullopt;
            };

            if (verb == "media_play") {
                last_action_ = "media_play";
                is_playing_ = true;
                is_paused_ = false;
                if (media_url_.empty()) {
                    media_url_ = "https://stream.rouen.live/master.m3u8";
                }
                if (active_item_id_ != 0) {
                    auto item_ptr = media_player::get_item_ptr(active_item_id_);
                    if (item_ptr) {
                        if (item_ptr->is_paused.load()) {
                            item_ptr->resumeMedia();
                        } else if (!item_ptr->is_playing) {
                            item_ptr->playMedia(this);
                        }
                    }
                }
            } else if (verb == "media_pause") {
                last_action_ = "media_pause";
                is_playing_ = false;
                is_paused_ = true;
                if (active_item_id_ != 0) {
                    auto item_ptr = media_player::get_item_ptr(active_item_id_);
                    if (item_ptr) {
                        item_ptr->pauseMedia();
                    }
                }
            } else if (verb == "media_stop") {
                last_action_ = "media_stop";
                is_playing_ = false;
                is_paused_ = false;
                current_position_ms_ = 0;
                if (active_item_id_ != 0) {
                    auto item_ptr = media_player::get_item_ptr(active_item_id_);
                    if (item_ptr) {
                        item_ptr->stopMedia();
                    }
                }
            } else if (verb == "media_set_volume") {
                last_action_ = "media_set_volume";
                std::optional<double> vol = extract_number(data, "volume");
                if (!vol) vol = extract_number(data, "volume_slider");
                if (!vol) vol = extract_number(payload, "volume");
                if (!vol) vol = extract_number(payload, "volume_slider");
                if (!vol) vol = extract_number(data, "vol");

                if (vol) {
                    int new_vol = std::clamp(static_cast<int>(*vol), 0, 100);
                    volume_percent_ = new_vol;
                    if (active_item_id_ != 0) {
                        auto item_ptr = media_player::get_item_ptr(active_item_id_);
                        if (item_ptr) {
                            item_ptr->setVolume(new_vol);
                        }
                    }
                }
            } else if (verb == "media_seek") {
                last_action_ = "media_seek";
                std::optional<double> seek = extract_number(data, "seek_seconds");
                if (!seek) seek = extract_number(data, "seconds");
                if (!seek) seek = extract_number(data, "position");
                if (!seek) seek = extract_number(data, "position_seconds");
                if (!seek) seek = extract_number(data, "seek");
                if (!seek) seek = extract_number(payload, "seek_seconds");
                if (!seek) seek = extract_number(payload, "seconds");

                if (seek) {
                    double target_sec = std::max(0.0, *seek);
                    current_position_ms_ = static_cast<long long>(target_sec * 1000.0);
                    if (active_item_id_ != 0) {
                        auto item_ptr = media_player::get_item_ptr(active_item_id_);
                        if (item_ptr) {
                            item_ptr->seekTo(target_sec);
                        }
                    }
                }
            } else if (verb == "toggle_adaptive_view") {
                last_action_ = "toggle_adaptive_view";
                adaptive_view_mode = !adaptive_view_mode;
            }

            last_adaptive_parse_time_ = {};
        } catch (const std::exception& e) {
            std::cerr << "[media] Action error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[media] Unknown action error" << std::endl;
        }
    }

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

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

    [[nodiscard]] bool is_playing() const noexcept {
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr && item_ptr->is_playing) {
                return !item_ptr->is_paused.load();
            }
        }
        return is_playing_ && !is_paused_;
    }

    [[nodiscard]] bool is_paused() const noexcept {
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr && item_ptr->is_playing) {
                return item_ptr->is_paused.load();
            }
        }
        return is_paused_;
    }

    [[nodiscard]] int get_volume() const noexcept {
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr) {
                return item_ptr->volume.load();
            }
        }
        return volume_percent_;
    }

    [[nodiscard]] int get_volume_percent() const noexcept {
        return get_volume();
    }

    [[nodiscard]] long long get_current_position_ms() const noexcept {
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr && item_ptr->is_playing) {
                return static_cast<long long>(item_ptr->position.load() * 1000.0);
            }
        }
        return current_position_ms_;
    }

    [[nodiscard]] double get_position_seconds() const noexcept {
        return static_cast<double>(get_current_position_ms()) / 1000.0;
    }

    [[nodiscard]] long long get_duration_ms() const noexcept {
        if (active_item_id_ != 0) {
            auto item_ptr = media_player::get_item_ptr(active_item_id_);
            if (item_ptr && item_ptr->is_playing && item_ptr->duration.load() > 0.0) {
                return static_cast<long long>(item_ptr->duration.load() * 1000.0);
            }
        }
        return duration_ms_;
    }

    [[nodiscard]] double get_duration_seconds() const noexcept {
        return static_cast<double>(get_duration_ms()) / 1000.0;
    }

    void set_duration_ms(long long ms) noexcept {
        duration_ms_ = ms;
    }

    [[nodiscard]] const std::string& get_media_url() const noexcept {
        return media_url_;
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept {
        return adaptive_view_mode;
    }

    void set_adaptive_view_mode(bool enabled) noexcept {
        adaptive_view_mode = enabled;
    }

    [[nodiscard]] const std::string& get_last_action() const noexcept {
        return last_action_;
    }

private:
    std::filesystem::path path_;
    std::string media_url_;
    std::string card_title_{"Media Player"};
    std::string stream_source_{"HLS / AAC Audio (320 kbps)"};
    bool auto_started_{false};
    ImGuiID active_item_id_{0};

    bool is_playing_{false};
    bool is_paused_{false};
    int volume_percent_{80};
    long long current_position_ms_{0};
    long long duration_ms_{0};
    std::string last_action_{};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
};

} // namespace rouen::cards
