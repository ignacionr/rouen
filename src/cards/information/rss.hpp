#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../interface/card.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../hosts/rss_host.hpp"
#include "../../models/rss/feed.hpp"

namespace rouen::cards {

// Main RSS card that displays all feeds
class rss : public card {
public:
    enum class TextureStatus {
        Pending,
        Loaded,
        Failed
    };

    struct LoadedFeedTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        TextureStatus status = TextureStatus::Pending;
    };

    struct AISearchResult {
        std::string title;
        std::string url;
        std::string description;
    };

    rss();
    ~rss() override;

    void on_close() override;
    bool supports_menu_decoration() const override { return false; }
    bool render() override;
    [[nodiscard]] std::string get_uri() const override;
    std::vector<card_performance_metric> get_performance_measurements() const override;

    void set_renderer(SDL_Renderer* r);
    void clear_feed_textures();

    void render_add_feed();
    void render_feed_list(const std::vector<std::shared_ptr<media::rss::feed>>& feeds, std::string& search_text, bool& has_matches);

    bool addFeed(const std::string& url);
    static std::shared_ptr<hosts::RSSHost> getHost();
    void invalidate_freshness_cache(std::string_view feed_url = "");

    void triggerAIFeedSearch(const std::string& topic);
    std::vector<AISearchResult> performAIFeedSearch(const std::string& topic);
    std::vector<AISearchResult> parseAIFeedResponse(const std::string& response);

    static std::string truncate_text(const std::string& text, float max_width);
    static void calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1);

private:
    static std::string feed_texture_cache_key(const std::string& url, ::helpers::ImageCache::Variant variant);
    SDL_Texture* get_feed_texture(const std::string& url, ::helpers::ImageCache::Variant variant, int& texture_width, int& texture_height);
    ImVec4 get_freshness_color(const std::shared_ptr<media::rss::feed>& feed, const std::chrono::system_clock::time_point& now) const;
    std::string get_freshness_text(const std::shared_ptr<media::rss::feed>& feed, const std::chrono::system_clock::time_point& now) const;
    void request_image_download(const std::string& url);

    // AI search state
    bool ai_search_in_progress_ = false;
    std::vector<AISearchResult> ai_search_results_;
    std::future<std::vector<AISearchResult>> ai_search_future_;
    
    std::string selected_tag_ = "All";
    
    // Cache for feed freshness colors to avoid recalculating every frame
    mutable std::unordered_map<std::string, std::pair<ImVec4, std::chrono::system_clock::time_point>> freshness_cache;
    static constexpr size_t MAX_CACHE_SIZE = 1000;
    
    std::shared_ptr<hosts::RSSHost> rss_host;

    // Cached feeds state to avoid filtering and sorting every frame
    struct FeedCacheState {
        size_t item_count = 0;
        std::chrono::system_clock::time_point latest_item_time;
        std::set<std::string> tags;
    };
    std::vector<std::shared_ptr<media::rss::feed>> cached_feeds_;
    std::vector<std::shared_ptr<media::rss::feed>> cached_all_feeds_;
    std::vector<std::string> cached_gallery_tags_;
    std::vector<rouen::hosts::RSSHost::SmartListInfo> cached_smart_lists_;
    std::chrono::steady_clock::time_point last_gallery_cache_update_time_{};
    std::chrono::steady_clock::time_point last_freshness_cache_invalidation_time_{};
    std::string last_selected_tag_ = "";
    size_t last_all_feeds_size_ = 0;
    std::unordered_map<std::string, FeedCacheState> feed_states_;
    std::set<std::string> top_fresh_tags_;
    bool show_all_tags_ = false;
    std::string status_message_ = "";
    std::chrono::steady_clock::time_point status_message_time_{};

    // Search debouncing and caching state
    char search_buffer_[256] = "";
    std::string debounced_search_query_ = "";
    std::chrono::system_clock::time_point last_search_type_time_;
    bool search_pending_ = false;

    std::string cached_search_query_ = "";
    std::string cached_search_tag_ = "";
    bool search_results_dirty_ = true;
    std::vector<std::shared_ptr<media::rss::feed>> cached_matching_feeds_;
    std::vector<hosts::RSSHost::FeedItem> cached_matching_items_;

    SDL_Renderer* renderer = nullptr;
    std::shared_ptr<::helpers::ImageCache> image_cache;

    std::unordered_map<std::string, LoadedFeedTexture> feed_textures;
    std::set<std::string> failed_downloads_;
    std::mutex downloading_mutex_;
    std::atomic<bool> image_downloaded_signal_{false};

    mutable std::string cached_rpm_str_;
    mutable std::string cached_latency_str_;
    mutable std::string cached_feed_cnt_str_;
};

} // namespace rouen::cards
