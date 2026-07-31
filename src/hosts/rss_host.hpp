#pragma once

#include <deque>
#include <chrono>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <set>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <functional>

#include "../helpers/html_media_extractor.hpp" // for media::html::extracted_media
#include "../models/rss/feed.hpp"
#include "../models/rss/sqliterepo.hpp"
#include "../models/rss/rss_url_resolver.hpp"

namespace rouen::hosts {

/**
 * RSS Host Controller
 * 
 * This class acts as a controller for RSS feeds, managing the communication
 * between the UI (cards) and the data model (feeds and SQLite repository).
 * It provides methods for adding, removing, and accessing RSS feeds.
 */
class RSSHost {
public:
    // Feed item representation that can be used by UI components
    struct FeedItem {
        std::string title;
        std::string description;
        std::string clean_description;
        std::string link;
        std::string enclosure;
        std::string image_url;
        std::chrono::system_clock::time_point publish_date;
        std::vector<media::html::extracted_media> extracted_media_urls; // Enhanced: extracted media from content
        std::optional<double> watermark; // playback watermark
        std::optional<double> media_duration_seconds; // media duration in seconds
        
        long long feed_id = -1;
        std::string feed_title;
        
        // Enhanced: Get the best available media URL for playback
        std::string get_best_media_url() const;
        
        // Enhanced: Check if this item has playable media
        bool has_media() const;
    };

    // Feed information structure
    struct FeedInfo {
        long long id;
        std::string title;
        std::string url;
        std::string image_url;
        std::string language;
    };

    /**
     * Constructor initializes the RSS host with a system runner and loads existing feeds
     */
    RSSHost();

    ~RSSHost();

    std::chrono::system_clock::time_point last_refresh_time() const;
    
    int refresh_interval_s() const;
    
    void trigger_manual_refresh();

    double requests_per_minute() const;
    double last_request_duration_ms() const;
    uint64_t total_requests() const;
    void record_request(double duration_ms);

    int get_timeout() const;
    void set_timeout(int t);

    bool is_auto_timeout_enabled() const;
    void set_auto_timeout_enabled(bool enabled);

    std::vector<FeedItem> search_items(const std::string& query);

    /**
     * Add new feeds from a list of URLs
     */
    void add_feeds(std::vector<std::string> urls, bool open_added_card = false);

    /**
     * Add a new feed by URL
     */
    bool add_feed(const std::string& url, bool open_added_card = true);

    /**
     * Get all feeds
     */
    std::vector<std::shared_ptr<media::rss::feed>> feeds() const;

    /**
     * Delete a feed by URL
     */
    void delete_feed(std::string_view url);

    void save_smart_list(const std::string& title, const media::rss::filter_group& filter);

    void delete_smart_list(const std::string& title);

    long long find_subscribed_youtube_feed_id(const std::string& channel_id, const std::string& feed_url, const std::string& channel_title) const;

    void rebuild_yt_index();

    struct SmartListInfo {
        std::string title;
        media::rss::filter_group filter;
    };

    std::vector<SmartListInfo> get_smart_lists();

    std::vector<FeedItem> get_filtered_items(const media::rss::filter_group& filter);

    std::set<std::string> get_feed_tags(long long feed_id);

    std::vector<std::string> get_available_tags();
    
    void add_feed_tag(long long feed_id, std::string_view tag);
    
    void remove_feed_tag(long long feed_id, std::string_view tag);

    int delete_unused_tags();

    std::string get_feed_language(long long feed_id);
    
    void set_feed_language(long long feed_id, std::string_view language);

    /**
     * Get feed information by ID
     */
    std::optional<FeedInfo> get_feed_info(long long feed_id);

    /**
     * Get items for a specific feed
     */
    std::vector<FeedItem> get_feed_items(long long feed_id, int limit = 100);

    bool add_feed_item(long long feed_id, std::string_view item_link, std::string_view item_title = "");
    
    /**
     * Get a specific item from a feed by its link
     */
    std::optional<FeedItem> get_feed_item(long long feed_id, const std::string& item_link, const std::string& item_title);
    
    void update_watermark(long long feed_id, const std::string& item_link, const std::string& item_title, std::optional<double> watermark);

    /**
     * Load podcasts from a file if it exists and directly add them to the database
     * This checks for a podcasts.txt file in the current directory
     * and adds any RSS feed URLs found in the file
     */
    void load_podcasts_from_file();

    /**
     * Refresh a specific feed by ID
     * Returns true if the refresh was successful
     */
    bool refresh_feed(long long feed_id);

    struct FeedDiagnosticInfo {
        long long id = -1;
        std::string title;
        std::string url;
        std::string language;
        size_t item_count = 0;
        size_t tag_count = 0;
        double last_render_ms = 0.0;
        double avg_render_ms = 0.0;
        double max_render_ms = 0.0;
        double min_render_ms = 0.0;
        uint64_t render_count = 0;
        uint64_t slow_render_count = 0;
        bool is_slow = false;
    };

    struct RSSDiagnostics {
        size_t total_feeds = 0;
        size_t total_items = 0;
        std::string slowest_feed_title;
        std::string slowest_feed_uri;
        double slowest_feed_render_ms = 0.0;
        std::vector<FeedDiagnosticInfo> feeds;
    };

    /**
     * Diagnostic report on RSS feeds and card render times
     */
    RSSDiagnostics get_rss_diagnostics();

private:
    // Callback for the HTTP fetch operation
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);

    void refresh_feeds(std::vector<std::string> urls, bool open_added_card = false);

    // Synchronously add a feed
    std::shared_ptr<media::rss::feed> add_feed_sync(std::string_view url, const std::function<bool()>& quitting);

    // Fetch and parse a feed from a URL with improved error handling
    media::rss::feed get_feed(std::string_view url);

    void initialize_async(std::stop_token stoken, std::vector<std::string> urls);

private:
    static std::vector<std::string> classify_feed_dynamically(const std::string& url, const std::vector<media::rss::feed_item>& items);

private:
    // Thread-safe collection of feeds using mutex instead of atomic
    std::vector<std::shared_ptr<media::rss::feed>> feeds_;
    mutable std::mutex feeds_mutex_;
    
    mutable std::mutex yt_sub_mutex_;
    std::unordered_map<std::string, long long> yt_channel_id_map_;
    std::unordered_map<std::string, long long> yt_url_map_;
    std::unordered_map<std::string, long long> yt_title_map_;

    media::rss::sqliterepo repo_;

    int timeout_s_ = 60;
    bool auto_timeout_enabled_ = true;
    std::unordered_map<std::string, int> feed_failure_counts_;
    std::mutex failure_counts_mutex_;

    // Per-feed backoff state (e.g., after HTTP 429 responses)
    std::unordered_map<std::string, std::chrono::system_clock::time_point> feed_backoff_until_;
    std::mutex backoff_mutex_;
    
    // Per-feed last refresh times to allow staggered scheduling
    std::unordered_map<std::string, std::chrono::system_clock::time_point> feed_last_refresh_times_;
    std::mutex last_refresh_mutex_;
    
    // Declared last so it is stopped/joined first during destruction
    std::vector<std::jthread> active_fetch_threads_;
    std::mutex fetch_threads_mutex_;
    std::jthread periodic_refresh_thread_;
    std::jthread duration_backfill_thread_;
    std::jthread init_thread_;

    std::chrono::system_clock::time_point last_refresh_time_ = std::chrono::system_clock::now();
    int refresh_interval_s_ = 900; // 15 minutes (900 seconds)
    std::atomic<bool> should_force_refresh_{false};

    mutable std::mutex metrics_mutex_;
    mutable std::deque<std::chrono::steady_clock::time_point> request_timestamps_;
    double last_request_duration_ms_{0.0};
    uint64_t total_requests_count_{0};

    void start_refresh_loop();

    void start_duration_backfill();
};

} // namespace rouen::hosts
