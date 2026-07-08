#pragma once

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <regex>

#include <chrono>

// Include our compatibility layer for C++20/23 features
#include "../helpers/compat/compat.hpp"
#include "../helpers/html_media_extractor.hpp"
#include "../helpers/platform_utils.hpp"
#include "../helpers/media_player.hpp"

#include "../registrar.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"
#include "../models/rss/feed.hpp"
#include "../models/rss/sqliterepo.hpp"

namespace rouen::hosts {

namespace {
    inline std::chrono::system_clock::time_point parse_db_date(const std::string& date_str) {
        std::tm tm = {};
        std::istringstream ss(date_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) {
            return std::chrono::system_clock::now();
        }
        
        using namespace std::chrono;
        auto date = year{tm.tm_year + 1900}/(tm.tm_mon + 1)/tm.tm_mday;
        auto time = hours{tm.tm_hour} + minutes{tm.tm_min} + seconds{tm.tm_sec};
        return sys_days{date} + time;
    }

    inline std::string extractYoutubeChannelId(const std::string& html) {
        std::smatch match;
        
        // 1. Try to find the RSS feed link directly
        std::regex r1(R"raw(youtube\.com/feeds/videos\.xml\?channel_id=(UC[A-Za-z0-9_-]{22}))raw");
        if (std::regex_search(html, match, r1) && match.size() > 1) {
            return match.str(1);
        }
        
        // 2. Try metadata channelId field in page JSON
        std::regex r2(R"raw("channelId"\s*:\s*"(UC[A-Za-z0-9_-]{22})")raw");
        if (std::regex_search(html, match, r2) && match.size() > 1) {
            return match.str(1);
        }
        
        // 3. Try browseId field in page JSON
        std::regex r3(R"raw("browseId"\s*:\s*"(UC[A-Za-z0-9_-]{22})")raw");
        if (std::regex_search(html, match, r3) && match.size() > 1) {
            return match.str(1);
        }
        
        // 4. Try itemprop="channelId"
        std::regex r4(R"raw(itemprop="channelId"\s+content="(UC[A-Za-z0-9_-]{22})")raw");
        if (std::regex_search(html, match, r4) && match.size() > 1) {
            return match.str(1);
        }
        
        return "";
    }

    inline std::string resolveYoutubeUrl(const std::string& input_url) {
        std::string url = input_url;
        // Trim whitespace
        url.erase(0, url.find_first_not_of(" \t\r\n"));
        url.erase(url.find_last_not_of(" \t\r\n") + 1);
        
        if (url.empty()) {
            return url;
        }
        
        // If it's already a youtube feed URL, return it as-is
        if (url.find("youtube.com/feeds/videos.xml") != std::string::npos) {
            return url;
        }
        
        // Check if it looks like a YouTube URL
        bool is_youtube = (url.find("youtube.com") != std::string::npos || 
                           url.find("youtu.be") != std::string::npos);
        if (!is_youtube) {
            return input_url;
        }
        
        // Pattern 1: URL contains channel/UC...
        std::regex channel_url_regex(R"raw(youtube\.com/channel/(UC[A-Za-z0-9_-]{22}))raw");
        std::smatch match;
        if (std::regex_search(url, match, channel_url_regex) && match.size() > 1) {
            return "https://www.youtube.com/feeds/videos.xml?channel_id=" + match.str(1);
        }
        
        // Pattern 2: Otherwise, it's handles/username (@Name, c/Name, user/Name, etc.)
        // We should fetch the page and find the channel ID.
        try {
            std::string fetch_url = url;
            if (fetch_url.find("http://") != 0 && fetch_url.find("https://") != 0) {
                fetch_url = "https://" + fetch_url;
            }
            
            http::fetch client{10};
            std::vector<std::string> headers = {
                "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            };
            
            std::string html = client(fetch_url, headers);
            std::string channel_id = extractYoutubeChannelId(html);
            if (!channel_id.empty()) {
                return "https://www.youtube.com/feeds/videos.xml?channel_id=" + channel_id;
            }
        } catch (const std::exception& e) {
            HTTP_WARN_FMT("Failed to resolve YouTube channel URL {}: {}", url, e.what());
        }
        
        return input_url;
    }
}

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
        std::string link;
        std::string enclosure;
        std::string image_url;
        std::chrono::system_clock::time_point publish_date;
        std::vector<media::html::extracted_media> extracted_media_urls; // Enhanced: extracted media from content
        std::optional<double> watermark; // playback watermark
        
        long long feed_id = -1;
        std::string feed_title;
        
        // Enhanced: Get the best available media URL for playback
        std::string get_best_media_url() const {
            // Priority order:
            // 1. Direct enclosure URL (highest priority)
            // 2. Video content from extracted media
            // 3. Audio content from extracted media
            // 4. Any other media content
            // 5. YouTube/Vimeo links as fallback
            
            if (!enclosure.empty()) {
                return enclosure;
            }
            
            // Look for video content first
            for (const auto& media : extracted_media_urls) {
                if (media.type == "video" && 
                    (media.format == "mp4" || media.format == "webm" || media.format == "youtube" || media.format == "vimeo")) {
                    return media.url;
                }
            }
            
            // Then audio content
            for (const auto& media : extracted_media_urls) {
                if (media.type == "audio" && 
                    (media.format == "mp3" || media.format == "wav" || media.format == "ogg" || media.format == "aac")) {
                    return media.url;
                }
            }
            
            // Any other media
            for (const auto& media : extracted_media_urls) {
                if (!media.url.empty()) {
                    return media.url;
                }
            }
            
            // If this is a YouTube link, return it directly
            if (link.find("youtube.com") != std::string::npos || link.find("youtu.be") != std::string::npos) {
                return link;
            }
            
            return "";
        }
        
        // Enhanced: Check if this item has playable media
        bool has_media() const {
            return !enclosure.empty() || 
                   !extracted_media_urls.empty() || 
                   link.find("youtube.com") != std::string::npos ||
                   link.find("youtu.be") != std::string::npos ||
                   link.find("vimeo.com") != std::string::npos;
        }
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
    RSSHost() 
        : repo_(rouen::platform::get_user_data_path("rss.db").string())
    {
        // Load settings from database
        try {
            std::string t_str = repo_.get_setting("timeout", "60");
            timeout_s_ = std::stoi(t_str);
        } catch (...) {
            timeout_s_ = 60;
        }
        try {
            std::string auto_str = repo_.get_setting("auto_timeout", "1");
            auto_timeout_enabled_ = (auto_str == "1");
        } catch (...) {
            auto_timeout_enabled_ = true;
        }
        RSS_INFO_FMT("RSSHost loaded settings: timeout={}s, auto_timeout={}", timeout_s_, auto_timeout_enabled_ ? "true" : "false");

        RSS_INFO("RSSHost constructor starting...");
        // Load existing feeds but defer loading items until they're needed
        std::vector<std::string> urls;
        
        try {
            RSS_INFO("RSSHost scanning feeds from repository...");
            repo_.scan_feeds([this, &urls](long long feed_id, const char* url, const char* title, const char* image_url, const char* language) {
                RSS_DEBUG_FMT("Found feed: ID={}, URL={}", feed_id, (url ? url : "null"));
                
                auto feed_ptr = std::make_shared<media::rss::feed>();
                feed_ptr->feed_title = title ? title : "";
                feed_ptr->source_link = url ? url : "";
                feed_ptr->feed_link = url ? url : "";
                feed_ptr->set_image(image_url ? image_url : "");
                feed_ptr->repo_id = feed_id;
                feed_ptr->language = language ? language : "";
                
                // Load existing items from database
                repo_.scan_items(feed_id, [feed_ptr](const char* item_link, const char* item_enclosure, const char* item_title, 
                                                 const char* item_desc, const char* item_pub_date, const char* item_img_url,
                                                 std::optional<double> watermark) {
                    auto publish_date = parse_db_date(item_pub_date ? item_pub_date : "");
                    
                    media::rss::feed_item item(
                        item_title ? item_title : "",
                        item_link ? item_link : "",
                        item_desc ? item_desc : "",
                        item_enclosure ? item_enclosure : "",
                        item_img_url ? item_img_url : "",
                        publish_date
                    );
                    item.watermark = watermark;
                    
                    // Populate extracted media URLs from description if present
                    if (!item.description.empty()) {
                        item.extracted_media_urls = media::html::extract_media_urls(item.description);
                    }
                    
                    feed_ptr->items.push_back(std::move(item));
                });
                
                // Sort items by date (newest first)
                std::sort(feed_ptr->items.begin(), feed_ptr->items.end(), [](const media::rss::feed_item& a, const media::rss::feed_item& b) {
                    return a.updated > b.updated;
                });
                
                // Load tags from database
                feed_ptr->tags = repo_.get_feed_tags(feed_id);
                
                // If feed has no tags, classify dynamically and save
                if (feed_ptr->tags.empty()) {
                    std::vector<media::rss::feed_item> items_copy;
                    for (const auto& item : feed_ptr->items) {
                        items_copy.push_back(item);
                    }
                    auto default_tags = classify_feed_dynamically(feed_ptr->source_link, items_copy);
                    for (const auto& tag : default_tags) {
                        repo_.add_feed_tag(feed_id, tag);
                    }
                    feed_ptr->tags = std::set<std::string>(default_tags.begin(), default_tags.end());
                }

                std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                feeds_.emplace_back(feed_ptr);
                urls.emplace_back(url ? url : "");
                RSS_DEBUG_FMT("Added feed ID={} to collection with {} cached items", feed_id, feed_ptr->items.size());
            });
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception during RSSHost feed scanning: {}", e.what());
        }
        
        // Load podcasts from podcasts.txt file if it exists
        // This must happen AFTER loading existing feeds so we can properly check for duplicates
        loadPodcastsFromFile();
        
        // Start the periodic background refresh loop
        startRefreshLoop();
        
        // Defer initial feed refresh to allow the startup phase to complete smoothly without thread/DB contention
        std::jthread([this, urls_to_refresh = std::move(urls)]() mutable {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if ("quitting"_fnb()) return;
            RSS_INFO("RSSHost starting initial feed refresh asynchronously...");
            refreshFeeds(std::move(urls_to_refresh));
        }).detach();
        RSS_INFO("RSSHost constructor completed (initial refresh deferred)");
        
        // Register the watermark callback so the player can update our database
        media_player_item::save_watermark_cb = [this](long long feed_id, const std::string& item_link, const std::string& item_title, double watermark) {
            this->updateWatermark(feed_id, item_link, item_title, watermark);
        };
    }

    ~RSSHost() {
        RSS_INFO("RSSHost destructor starting...");
        // 1. Stop all active media playback to save final watermarks gracefully while the host is still alive
        try {
            media_player::stopAll();
        } catch (...) {
            RSS_WARN("Failed to stop media player during RSSHost destructor");
        }
        
        // 2. Clear the callback so no late background notifications try to invoke it
        media_player_item::save_watermark_cb = nullptr;
        
        periodic_refresh_thread_.request_stop();
        fetch_thread_.request_stop();
        RSS_INFO("RSSHost destructor completed");
    }

    std::chrono::system_clock::time_point last_refresh_time() const {
        return last_refresh_time_;
    }
    
    int refresh_interval_s() const {
        return refresh_interval_s_;
    }
    
    void triggerManualRefresh() {
        should_force_refresh_.store(true);
    }

    int get_timeout() const { return timeout_s_; }
    void set_timeout(int t) {
        timeout_s_ = std::clamp(t, 5, 300);
        try {
            repo_.set_setting("timeout", std::to_string(timeout_s_));
        } catch (...) {}
    }

    bool is_auto_timeout_enabled() const { return auto_timeout_enabled_; }
    void set_auto_timeout_enabled(bool enabled) {
        auto_timeout_enabled_ = enabled;
        try {
            repo_.set_setting("auto_timeout", auto_timeout_enabled_ ? "1" : "0");
        } catch (...) {}
    }

    std::vector<FeedItem> searchItems(const std::string& query) {
        std::vector<FeedItem> results;
        repo_.search_items(query, [&results](long long feed_id, const char* feed_title, const char* link,
                                            const char* enclosure, const char* title, const char* description,
                                            const char* pub_date, const char* image_url, std::optional<double> watermark) {
            FeedItem item;
            item.feed_id = feed_id;
            item.feed_title = feed_title ? feed_title : "";
            item.link = link ? link : "";
            item.enclosure = enclosure ? enclosure : "";
            item.title = title ? title : "";
            item.description = description ? description : "";
            item.image_url = image_url ? image_url : "";
            item.watermark = watermark;
            
            item.publish_date = media::rss::parse_rss_date(pub_date);
            
            if (!item.description.empty()) {
                item.extracted_media_urls = media::html::extract_media_urls(item.description);
            }
            
            results.push_back(std::move(item));
        });
        return results;
    }

    /**
     * Add new feeds from a list of URLs
     */
    void addFeeds(std::vector<std::string> urls) {
        RSS_WARN_FMT("addFeeds called with {} URLs", urls.size());
        for (const auto& url : urls) {
            RSS_WARN_FMT("URL being added: {}", url);
        }
        refreshFeeds(std::move(urls));
    }

    /**
     * Add a new feed by URL
     */
    bool addFeed(const std::string& url) {
        try {
            std::vector<std::string> urls = {url};
            refreshFeeds(std::move(urls));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * Get all feeds
     */
    auto feeds() const {
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        return feeds_;
    }

    /**
     * Delete a feed by URL
     */
    void deleteFeed(std::string_view url) {
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        auto pos = std::find_if(feeds_.begin(), feeds_.end(),
                               [url](auto const& f) {
                                   return f->feed_link == url || f->source_link == url;
                               });
        
        if (pos != feeds_.end()) {
            feeds_.erase(pos);
            repo_.delete_feed(url);
        }
    }

    std::set<std::string> getFeedTags(long long feed_id) {
        return repo_.get_feed_tags(feed_id);
    }
    
    void addFeedTag(long long feed_id, std::string_view tag) {
        repo_.add_feed_tag(feed_id, tag);
        
        // Also update memory representation
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        for (auto& feed : feeds_) {
            if (feed->repo_id == feed_id) {
                feed->tags.insert(std::string(tag));
                break;
            }
        }
    }
    
    void removeFeedTag(long long feed_id, std::string_view tag) {
        repo_.remove_feed_tag(feed_id, tag);
        
        // Also update memory representation
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        for (auto& feed : feeds_) {
            if (feed->repo_id == feed_id) {
                feed->tags.erase(std::string(tag));
                break;
            }
        }
    }

    std::string getFeedLanguage(long long feed_id) {
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        for (const auto& feed : feeds_) {
            if (feed->repo_id == feed_id) {
                return feed->language;
            }
        }
        return "";
    }
    
    void setFeedLanguage(long long feed_id, std::string_view language) {
        repo_.update_feed_language(feed_id, language);
        
        // Also update memory representation
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        for (auto& feed : feeds_) {
            if (feed->repo_id == feed_id) {
                feed->language = std::string(language);
                break;
            }
        }
    }

    /**
     * Get feed information by ID
     */
    std::optional<FeedInfo> getFeedInfo(long long feed_id) {
        std::optional<FeedInfo> result;
        
        repo_.scan_feeds([&result, feed_id](long long id, const char* url, const char* title, const char* image_url, const char* language) {
            if (id == feed_id) {
                result = FeedInfo{
                    .id = id,
                    .title = title ? title : "",
                    .url = url ? url : "",
                    .image_url = image_url ? image_url : "",
                    .language = language ? language : ""
                };
            }
        });
        
        return result;
    }

    /**
     * Get items for a specific feed
     */
    std::vector<FeedItem> getFeedItems(long long feed_id) {
        std::vector<FeedItem> items;
        
        repo_.scan_items(feed_id, [&items, feed_id](const char* link, const char* enclosure, const char* title, 
                                         const char* description, const char* pub_date, const char* image_url,
                                         std::optional<double> watermark) {
            // Mark unused parameters to avoid warnings
            (void)link; (void)enclosure; (void)title;
            (void)description; (void)pub_date; (void)image_url;
            
            auto publish_date = parse_db_date(pub_date ? pub_date : "");
            
            // Create and store the item
            FeedItem item{
                .title = title ? title : "",
                .description = description ? description : "",
                .link = link ? link : "",
                .enclosure = enclosure ? enclosure : "",
                .image_url = image_url ? image_url : "",
                .publish_date = publish_date,
                .extracted_media_urls = {},
                .watermark = watermark,
                .feed_id = feed_id,
                .feed_title = ""
            };
            
            // Enhanced: Extract media URLs from description content
            if (!item.description.empty()) {
                item.extracted_media_urls = media::html::extract_media_urls(item.description);
            }
            
            items.push_back(std::move(item));
        });
        
        // Sort items by publish date (newest first)
        std::sort(items.begin(), items.end(), [](const FeedItem& a, const FeedItem& b) {
            return a.publish_date > b.publish_date;
        });
        
        return items;
    }
    
    /**
     * Get a specific item from a feed by its link
     */
    std::optional<FeedItem> getFeedItem(long long feed_id, const std::string& item_link, const std::string& item_title) {
        std::optional<FeedItem> result;
        
        repo_.scan_items(feed_id, [&result, &item_link, &item_title, feed_id](const char* link, const char* enclosure, const char* title, 
                                                     const char* description, const char* pub_date, const char* image_url,
                                                     std::optional<double> watermark) {
            // Mark unused parameters to avoid warnings
            (void)link; (void)enclosure; (void)title;
            (void)description; (void)pub_date; (void)image_url;
            
            if (link && item_link == link && (item_title.empty() || (title && item_title == title))) {
                auto publish_date = parse_db_date(pub_date ? pub_date : "");
                
                result = FeedItem{
                    .title = title ? title : "",
                    .description = description ? description : "",
                    .link = link,
                    .enclosure = enclosure ? enclosure : "",
                    .image_url = image_url ? image_url : "",
                    .publish_date = publish_date,
                    .extracted_media_urls = {},
                    .watermark = watermark,
                    .feed_id = feed_id,
                    .feed_title = ""
                };
                
                // Enhanced: Extract media URLs from description content
                if (!result->description.empty()) {
                    result->extracted_media_urls = media::html::extract_media_urls(result->description);
                }
            }
        });
        
        return result;
    }
    
    void updateWatermark(long long feed_id, const std::string& item_link, const std::string& item_title, std::optional<double> watermark) {
        // Update database
        repo_.update_watermark(feed_id, item_link, item_title, watermark);
        
        // Update in-memory cache
        std::lock_guard<std::mutex> lock(feeds_mutex_);
        for (auto& feed : feeds_) {
            if (feed->repo_id == feed_id) {
                for (auto& item : feed->items) {
                    if (item.link == item_link && item.title == item_title) {
                        item.watermark = watermark;
                        RSS_DEBUG_FMT("Updated in-memory watermark for feed_id={}, title='{}' to {}", feed_id, item_title, watermark ? *watermark : 0.0);
                        return;
                    }
                }
            }
        }
    }

    /**
     * Load podcasts from a file if it exists and directly add them to the database
     * This checks for a podcasts.txt file in the current directory
     * and adds any RSS feed URLs found in the file
     */
    void loadPodcastsFromFile() {
        RSS_WARN("Checking for podcasts.txt file...");
        
        // Get the path to podcasts.txt using the resource path utility
        auto podcasts_path = rouen::platform::get_resource_path("podcasts.txt");
        
        // Check if file exists before attempting to read
        if (!std::filesystem::exists(podcasts_path)) {
            RSS_WARN_FMT("podcasts.txt file not found at path: {}, skipping", podcasts_path.string());
            return;
        }
        
        RSS_WARN_FMT("Reading podcasts from file: {}", podcasts_path.string());
        
        std::ifstream file(podcasts_path);
        if (!file.is_open()) {
            RSS_ERROR_FMT("Failed to open {} file", podcasts_path.string());
            return;
        }
        
        // Read the file line by line
        std::string line;
        std::vector<std::string> podcast_urls;
        int line_count = 0;
        int added_count = 0;
        
        while (std::getline(file, line)) {
            line_count++;
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                RSS_DEBUG_FMT("Line {}: Skipping comment or empty line", line_count);
                continue;
            }
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            if (!line.empty() && line.find_last_not_of(" \t") != std::string::npos) {
                line.erase(line.find_last_not_of(" \t") + 1);
            }
            
            if (!line.empty()) {
                podcast_urls.push_back(line);
                RSS_WARN_FMT("Line {}: Found podcast URL: {}", line_count, line);
            }
        }
        
        RSS_WARN_FMT("Found {} URLs in podcasts.txt", podcast_urls.size());
        
        if (podcast_urls.empty()) {
            RSS_WARN_FMT("No valid podcast URLs found in {}", podcasts_path.string());
            return;
        }
        
        RSS_WARN_FMT("Processing {} podcasts from {}", podcast_urls.size(), podcasts_path.string());
        
        // Directly process each URL and add it to the database
        for (const auto& url : podcast_urls) {
            // First check if this URL already exists in the repository
            bool exists = false;
            {
                std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                for (const auto& feed : feeds_) {
                    if (feed->source_link == url || feed->feed_link == url) {
                        RSS_WARN_FMT("Podcast already exists: {}", url);
                        exists = true;
                        break;
                    }
                }
            }
            
            if (!exists) {
                RSS_WARN_FMT("Adding new podcast URL to database: {}", url);
                // Directly add to database with default values
                long long feed_id = repo_.upsert_feed(url, url, "");
                
                if (feed_id > 0) {
                    // Create minimal feed representation for the UI
                    auto feed_ptr = std::make_shared<media::rss::feed>();
                    feed_ptr->feed_title = url;  // Use URL as title initially
                    feed_ptr->source_link = url;
                    feed_ptr->feed_link = url;
                    feed_ptr->repo_id = feed_id;
                    
                    // Add to in-memory collection
                    std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                    feeds_.emplace_back(feed_ptr);
                    added_count++;
                    RSS_WARN_FMT("Added feed ID={} to database and memory", feed_id);
                } else {
                    RSS_ERROR_FMT("Failed to add podcast to database: {}", url);
                }
            }
        }
        
        RSS_WARN_FMT("Directly added {} new podcasts from {}", added_count, podcasts_path.string());
        
        if (added_count > 0) {
            "notify"_sfn(std::format("Added {} new podcasts from {}", added_count, podcasts_path.string()));
        }
    }

    /**
     * Refresh a specific feed by ID
     * Returns true if the refresh was successful
     */
    bool refreshFeed(long long feed_id) {
        try {
            RSS_INFO_FMT("Refreshing feed with ID: {}", feed_id);
            
            // Find the feed URL from the repository
            std::optional<std::string> feed_url;
            
            repo_.scan_feeds([&feed_url, feed_id](long long id, const char* url, const char* /*title*/, const char* /*image_url*/, const char* /*language*/) {
                if (id == feed_id && url) {
                    feed_url = url;
                }
            });
            
            if (!feed_url) {
                RSS_ERROR_FMT("Could not find URL for feed ID: {}", feed_id);
                return false;
            }
            
            // Use a lambda to bypass the quit check in sync feed refresh
            auto never_quit = []() { return false; };
            
            // Fetch the feed synchronously
            auto refreshed_feed = addFeedSync(*feed_url, never_quit);
            
            if (refreshed_feed) {
                RSS_INFO_FMT("Successfully refreshed feed ID: {}", feed_id);
                return true;
            } else {
                RSS_ERROR_FMT("Failed to refresh feed ID: {}", feed_id);
                return false;
            }
        } 
        catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception while refreshing feed ID {}: {}", feed_id, e.what());
            return false;
        }
    }

private:
    // Callback for the HTTP fetch operation
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto parser = static_cast<media::rss::feed*>(userp);
        (*parser)(std::string_view{static_cast<char*>(contents), size * nmemb});
        return size * nmemb;
    }

    // Refresh feeds in a background thread with performance improvements
    void refreshFeeds(std::vector<std::string> urls) {
        // Define how many feeds to process in parallel
        const int BATCH_SIZE = 15;
        
        fetch_thread_ = std::jthread([this, url_list = std::move(urls)] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            
            // Track successful feeds for notification purposes
            int success_count = 0;
            int error_count = 0;
            
            // Process feeds in batches to balance performance
            for (size_t i = 0; i < url_list.size(); i += BATCH_SIZE) {
                // Create a batch of worker threads
                std::vector<std::jthread> workers;
                std::mutex results_mutex;
                std::vector<std::shared_ptr<media::rss::feed>> batch_results;
                
                // Process up to BATCH_SIZE feeds in parallel
                size_t end = std::min(i + BATCH_SIZE, url_list.size());
                
                for (size_t j = i; j < end; ++j) {
                    if (quit_job()) break;
                    
                    workers.emplace_back([this, &url_list, j, &results_mutex, &batch_results, &success_count, 
                                         &error_count, &quit_job](std::stop_token worker_stoken) {
                        // Create a composite quit check that includes the worker thread's stop token
                        auto worker_quit = [worker_stoken, &quit_job]() -> bool {
                            return quit_job() || worker_stoken.stop_requested();
                        };
                        
                        try {
                            // Only attempt to add the feed if we're not quitting
                            if (!worker_quit()) {
                                RSS_INFO_FMT("Starting to process feed: {}", url_list[j]);
                                auto feed_ptr = addFeedSync(url_list[j], worker_quit);
                                
                                if (feed_ptr) {
                                    RSS_INFO_FMT("Successfully fetched and processed feed: {}", url_list[j]);
                                    std::lock_guard<std::mutex> lock(results_mutex);
                                    batch_results.push_back(feed_ptr);
                                    ++success_count;
                                    
                                    // Update the UI periodically to show progress
                                    if (success_count % 25 == 0) {
                                        "notify"_sfn(std::format("Progress: {} RSS feeds loaded so far...", success_count));
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Failed to add feed {}: {}", url_list[j], e.what());
                            "notify"_sfn(std::format("Failed to add feed {}", url_list[j]));
                            
                            std::lock_guard<std::mutex> lock(results_mutex);
                            ++error_count;
                        } catch (...) {
                            RSS_ERROR_FMT("Failed to add feed {} with unknown error", url_list[j]);
                            
                            std::lock_guard<std::mutex> lock(results_mutex);
                            ++error_count;
                        }
                    });
                }
                
                // Wait for all workers in this batch to complete
                for (auto& worker : workers) {
                    worker.join();
                }
                
                if (quit_job()) break;
            }
            
            // Final notification
            if (success_count > 0) {
                "notify"_sfn(std::format("Successfully loaded {} RSS feeds.", success_count));
            }
            
            if (error_count > 0) {
                "notify"_sfn(std::format("{} feeds failed to load. Check the logs for details.", error_count));
            }
        });
    }

    // Synchronously add a feed
    std::shared_ptr<media::rss::feed> addFeedSync(std::string_view url, auto quitting) {
        try {
            std::string resolved_url = resolveYoutubeUrl(std::string(url));
            
            // Download and parse the feed
            auto feed_ptr = std::make_shared<media::rss::feed>(getFeed(resolved_url));
            
            if (quitting()) return nullptr;

            if (feed_ptr->is_permanently_redirected) {
                RSS_INFO_FMT("Permanent 301/308 redirect detected: {} -> {}. Updating database.", resolved_url, feed_ptr->source_link);
                repo_.update_feed_url(resolved_url, feed_ptr->source_link);
            }

            std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
            auto& feeds = feeds_;
            
            // Check if the feed already exists
            auto pos = std::find_if(feeds.begin(), feeds.end(),
                                  [oursrc = feed_ptr->source_link, original_url = resolved_url](auto const& f) {
                                      return f->source_link == oursrc || f->source_link == original_url;
                                  });
                                  
            // Add or merge with existing feed
            if (pos != feeds.end()) {
                // Update the existing feed
                feed_ptr->repo_id = (*pos)->repo_id;
                (*pos)->feed_title = feed_ptr->feed_title;
                (*pos)->feed_description = feed_ptr->feed_description;
                (*pos)->feed_link = feed_ptr->feed_link;
                (*pos)->source_link = feed_ptr->source_link; // Keep updated to final URL
                (*pos)->set_image(feed_ptr->image_url());
                
                // Merge new items, avoiding duplicates (matching by both link and title to support podcasts/Megaphone)
                for (auto const& item : feed_ptr->items) {
                    auto item_pos = std::find_if((*pos)->items.begin(), (*pos)->items.end(),
                                              [ourlink = item.link, ourtitle = item.title](auto const& i) {
                                                  return i.link == ourlink && i.title == ourtitle;
                                               });
                    if (item_pos == (*pos)->items.end()) {
                        (*pos)->items.emplace_back(item);
                    }
                }
                feed_ptr = *pos;
            } else {
                feeds.emplace_back(feed_ptr);
            }
            
            // Update the repository with feed info (using the redirected/final source link)
            feed_ptr->repo_id = repo_.upsert_feed(feed_ptr->source_link, feed_ptr->feed_title, feed_ptr->image_url());
            
            // Prepare items for batch insert
            std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items_batch;
            items_batch.reserve(feed_ptr->items.size());
            
            for (auto const& item : feed_ptr->items) {
                // Format the date time
                auto const pub_date = std::format("{:%F %T}", item.updated, item.updated);
                
                // Add to batch collection
                items_batch.emplace_back(
                    item.title,
                    item.enclosure,
                    item.link,
                    item.description,
                    pub_date,
                    item.image_url
                );
            }
            
            // Perform batch insert for better performance
            if (!items_batch.empty()) {
                repo_.batch_upsert_items(feed_ptr->repo_id, items_batch);
            }

            // Load tags for feed
            feed_ptr->tags = repo_.get_feed_tags(feed_ptr->repo_id);
            if (feed_ptr->tags.empty()) {
                std::vector<media::rss::feed_item> items_copy;
                for (const auto& item : feed_ptr->items) {
                    items_copy.push_back(item);
                }
                auto default_tags = classify_feed_dynamically(feed_ptr->source_link, items_copy);
                for (const auto& tag : default_tags) {
                    repo_.add_feed_tag(feed_ptr->repo_id, tag);
                }
                feed_ptr->tags = std::set<std::string>(default_tags.begin(), default_tags.end());
            }
            
            // Sort the feeds from the latest updated to the oldest
            std::sort(feeds.begin(), feeds.end(),
                    [](auto const& lhs, auto const& rhs) {
                        return lhs->items.empty() ? false : 
                              (rhs->items.empty() ? true : 
                                lhs->items.front().updated > rhs->items.front().updated);
                    });
                    
            return feed_ptr;
        } catch (const std::exception& e) {
            // Log the error and rethrow for handling in the caller
            "notify"_sfn(std::format("Error processing feed {}: {}", url, e.what()));
            throw;
        }
    }

    // Fetch and parse a feed from a URL with improved error handling
    media::rss::feed getFeed(std::string_view url) {
        std::string url_str{url};
        int failure_count = 0;
        {
            std::lock_guard<std::mutex> lock(failure_counts_mutex_);
            if (auto it = feed_failure_counts_.find(url_str); it != feed_failure_counts_.end()) {
                failure_count = it->second;
            }
        }

        int timeout = timeout_s_;
        if (auto_timeout_enabled_) {
            // Automatically increase timeout on failures, up to 180 seconds
            timeout = std::min(timeout_s_ + failure_count * 20, 180);
            if (failure_count > 0) {
                RSS_WARN_FMT("Feed {} has failed {} times. Adjusting timeout to {}s (base: {}s)", url, failure_count, timeout, timeout_s_);
            }
        }

        try {
            RSS_INFO_FMT("Starting feed fetch for URL: {} (timeout: {}s)", url, timeout);
            http::fetch fetch{static_cast<long>(timeout)}; // Dynamic timeout
            media::rss::feed parser;
            parser.source_link = url;
            
            // Set up proper headers for better compatibility
            auto header_client = [](auto h) {
                h("User-Agent: Rouen RSS Reader/1.0");
                h("Accept: application/rss+xml, application/xml, text/xml, */*");
            };
            
            fetch(std::string{url}, header_client, writeCallback, &parser);
            
            if (fetch.last_redirect_was_permanent()) {
                std::string final_url = fetch.last_effective_url();
                if (!final_url.empty() && final_url != url) {
                    parser.is_permanently_redirected = true;
                    parser.source_link = final_url;
                    RSS_INFO_FMT("Feed URL permanently redirected: {} -> {}", url, final_url);
                }
            }
            
            // Reset failure count on success
            {
                std::lock_guard<std::mutex> lock(failure_counts_mutex_);
                feed_failure_counts_[url_str] = 0;
            }

            RSS_INFO_FMT("Successfully fetched feed: {} - Title: {}", url, parser.feed_title);
            return parser;
        } catch (const std::exception& e) {
            // Increment failure count on failure
            {
                std::lock_guard<std::mutex> lock(failure_counts_mutex_);
                feed_failure_counts_[url_str] = failure_count + 1;
            }
            RSS_ERROR_FMT("Failed to fetch feed {}: {}", url, e.what());
            throw std::runtime_error(std::string("Failed to fetch feed: ") + e.what());
        }
    }

private:
    std::vector<std::string> classify_feed_dynamically(const std::string& url, const std::vector<media::rss::feed_item>& items) {
        std::vector<std::string> tags;
        std::string lower_url = url;
        std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
        
        // Podcasts
        bool is_podcast = (lower_url.find("megaphone.fm") != std::string::npos ||
                           lower_url.find("spreaker.com") != std::string::npos ||
                           lower_url.find("libsyn.com") != std::string::npos ||
                           lower_url.find("simplecast.com") != std::string::npos ||
                           lower_url.find("transistor.fm") != std::string::npos ||
                           lower_url.find("anchor.fm") != std::string::npos ||
                           lower_url.find("audioboom.com") != std::string::npos ||
                           lower_url.find("podbean.com") != std::string::npos ||
                           lower_url.find("acast.com") != std::string::npos ||
                           lower_url.find("podplaystudio.com") != std::string::npos);
        if (!is_podcast) {
            for (const auto& item : items) {
                if (!item.enclosure.empty() && 
                    (item.enclosure.find(".mp3") != std::string::npos || 
                     item.enclosure.find(".wav") != std::string::npos ||
                     item.enclosure.find("audio") != std::string::npos)) {
                    is_podcast = true;
                    break;
                }
            }
        }
        if (is_podcast) tags.push_back("Podcasts");
        
        // YouTube
        if (lower_url.find("youtube.com") != std::string::npos ||
            lower_url.find("youtu.be") != std::string::npos) {
            tags.push_back("YouTube");
        }
        
        // Tech / Dev
        if (lower_url.find("cpp") != std::string::npos ||
            lower_url.find("changelog") != std::string::npos ||
            lower_url.find("softwareengineering") != std::string::npos ||
            lower_url.find("devsarg") != std::string::npos ||
            lower_url.find("osnews") != std::string::npos ||
            lower_url.find("computerhistory") != std::string::npos ||
            lower_url.find("pythontest") != std::string::npos) {
            tags.push_back("Tech / Dev");
        }
        
        // News
        if (lower_url.find("wsj") != std::string::npos ||
            lower_url.find("npr") != std::string::npos ||
            lower_url.find("elpais") != std::string::npos ||
            lower_url.find("rt.com") != std::string::npos ||
            lower_url.find("repubblica") != std::string::npos ||
            lower_url.find("leftcom") != std::string::npos ||
            lower_url.find("themoscowtimes") != std::string::npos ||
            lower_url.find("aljazeera") != std::string::npos ||
            lower_url.find("channel4") != std::string::npos ||
            lower_url.find("c5n") != std::string::npos ||
            lower_url.find("clarin") != std::string::npos ||
            lower_url.find("dw.com") != std::string::npos ||
            lower_url.find("bbci.co.uk") != std::string::npos) {
            tags.push_back("News");
        }
        
        if (tags.empty()) {
            tags.push_back("Other");
        }
        
        return tags;
    }

private:
    // Thread-safe collection of feeds using mutex instead of atomic
    std::vector<std::shared_ptr<media::rss::feed>> feeds_;
    mutable std::mutex feeds_mutex_;
    
    media::rss::sqliterepo repo_;

    int timeout_s_ = 60;
    bool auto_timeout_enabled_ = true;
    std::unordered_map<std::string, int> feed_failure_counts_;
    std::mutex failure_counts_mutex_;
    
    // Declared last so it is stopped/joined first during destruction
    std::jthread fetch_thread_;

    std::chrono::system_clock::time_point last_refresh_time_ = std::chrono::system_clock::now();
    int refresh_interval_s_ = 900; // 15 minutes (900 seconds)
    std::atomic<bool> should_force_refresh_{false};
    std::jthread periodic_refresh_thread_;

    void startRefreshLoop() {
        periodic_refresh_thread_ = std::jthread([this] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            
            last_refresh_time_ = std::chrono::system_clock::now();
            
            while (!quit_job()) {
                // Sleep until next refresh interval, waking up periodically to check stop token/force refresh
                auto next_refresh = last_refresh_time_ + std::chrono::seconds(refresh_interval_s_);
                while (std::chrono::system_clock::now() < next_refresh && !quit_job()) {
                    if (should_force_refresh_.load()) {
                        break; // Break the sleep loop to refresh immediately
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                
                if (quit_job()) break;
                
                // Reset force refresh flag
                should_force_refresh_ = false;
                
                // Collect URLs of all currently loaded feeds
                std::vector<std::string> urls;
                {
                    std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                    for (const auto& feed : feeds_) {
                        urls.push_back(feed->source_link);
                    }
                }
                
                if (!urls.empty()) {
                    RSS_INFO_FMT("Starting periodic refresh of {} feeds...", urls.size());
                    
                    int success_count = 0;
                    int error_count = 0;
                    const int BATCH_SIZE = 15;
                    
                    for (size_t i = 0; i < urls.size(); i += BATCH_SIZE) {
                        std::vector<std::jthread> workers;
                        std::mutex results_mutex;
                        size_t end = std::min(i + BATCH_SIZE, urls.size());
                        
                        for (size_t j = i; j < end; ++j) {
                            if (quit_job()) break;
                            
                            workers.emplace_back([this, &urls, j, &results_mutex, &success_count, 
                                                 &error_count, &quit_job](std::stop_token worker_stoken) {
                                auto worker_quit = [worker_stoken, &quit_job]() -> bool {
                                    return quit_job() || worker_stoken.stop_requested();
                                };
                                
                                try {
                                    if (!worker_quit()) {
                                        auto feed_ptr = addFeedSync(urls[j], worker_quit);
                                        if (feed_ptr) {
                                            std::lock_guard<std::mutex> lock(results_mutex);
                                            ++success_count;
                                        }
                                    }
                                } catch (...) {
                                    std::lock_guard<std::mutex> lock(results_mutex);
                                    ++error_count;
                                }
                            });
                        }
                        
                        for (auto& worker : workers) {
                            worker.join();
                        }
                        
                        if (quit_job()) break;
                    }
                    
                    RSS_INFO_FMT("Periodic refresh done. Success: {}, Error: {}", success_count, error_count);
                }
                
                // Update last refresh time after completing the refresh
                last_refresh_time_ = std::chrono::system_clock::now();
            }
        });
    }
};

} // namespace rouen::hosts
