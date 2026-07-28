#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../interface/card.hpp"
#include "../../helpers/media_player.hpp"
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
    [[nodiscard]] std::string get_uri() const override;

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
