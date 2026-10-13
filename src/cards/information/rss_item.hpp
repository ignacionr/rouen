#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../interface/card.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/ui_context.hpp"
#include "../../hosts/rss_host.hpp"

namespace helpers {
class ImageCache;
}

namespace rouen::cards {

// Card to display a single RSS item
class rss_item : public card {
public:
    explicit rss_item(const std::string& item_info);
    ~rss_item() override;

    void on_close() override;
    bool render() override;
    bool render(rouen::ui::ui_context& ui) override;
    [[nodiscard]] std::string get_uri() const override;

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_playing() const noexcept { return is_playing_; }
    [[nodiscard]] bool is_bookmarked() const noexcept { return is_bookmarked_; }
    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }

    void set_renderer(SDL_Renderer* r) noexcept;
    void clear_item_textures();

    static void calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1) noexcept;

private:
    void loadItem();
    void request_image_download(const std::string& url);
    [[nodiscard]] std::string get_playable_media_url() const;

    long long feed_id = -1;
    std::string item_link;
    std::string item_title;
    bool item_loaded = false;
    bool auto_play = false;

    bool is_playing_{false};
    bool is_bookmarked_{false};
    std::string last_action_{};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::shared_ptr<hosts::RSSHost> rss_host;
    hosts::RSSHost::FeedItem item;

    media_player::item media;

    SDL_Renderer* renderer = nullptr;
    std::shared_ptr<::helpers::ImageCache> image_cache;

    struct LoadedItemTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, LoadedItemTexture> item_textures;
};

} // namespace rouen::cards
