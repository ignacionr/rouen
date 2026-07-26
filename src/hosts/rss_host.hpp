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
#include <array>
#include <cstdio>
#include <cmath>

#include <chrono>

// Include our compatibility layer for C++20/23 features
#include "../helpers/compat/compat.hpp"
#include "../helpers/html_media_extractor.hpp"
#include "../helpers/platform_utils.hpp"
#include "../helpers/media_player.hpp"
#include "../helpers/string_helper.hpp"

#include "../registrar.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"
#include "../models/rss/feed.hpp"
#include "../models/rss/sqliterepo.hpp"

namespace rouen::hosts {

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

    inline std::string trim_copy(std::string value) {
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        if (!value.empty()) {
            auto end_pos = value.find_last_not_of(" \t\r\n");
            if (end_pos != std::string::npos) {
                value.erase(end_pos + 1);
            }
        }
        return value;
    }

    inline std::string decode_json_string(std::string value) {
        std::string out;
        out.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                ++i;
                switch (value[i]) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(value[i]); break;
                }
            } else {
                out.push_back(value[i]);
            }
        }
        return out;
    }

    inline bool is_youtube_media_url(const std::string& url) {
        std::string lower = ::helpers::StringHelper::to_lower(url);
        return lower.find("youtube.com") != std::string::npos || lower.find("youtu.be") != std::string::npos;
    }

    inline std::optional<std::string> extract_youtube_video_id(const std::string& url) {
        std::smatch match;
        static const std::regex watch_rx(R"raw([?&]v=([A-Za-z0-9_-]{11}))raw");
        static const std::regex short_rx(R"raw(youtu\.be/([A-Za-z0-9_-]{11}))raw");
        static const std::regex shorts_rx(R"raw(/shorts/([A-Za-z0-9_-]{11}))raw");
        static const std::regex embed_rx(R"raw(/embed/([A-Za-z0-9_-]{11}))raw");
        static const std::regex v_path_rx(R"raw(/v/([A-Za-z0-9_-]{11}))raw");

        if (std::regex_search(url, match, watch_rx) && match.size() > 1) {
            return match.str(1);
        }
        if (std::regex_search(url, match, short_rx) && match.size() > 1) {
            return match.str(1);
        }
        if (std::regex_search(url, match, shorts_rx) && match.size() > 1) {
            return match.str(1);
        }
        if (std::regex_search(url, match, embed_rx) && match.size() > 1) {
            return match.str(1);
        }
        if (std::regex_search(url, match, v_path_rx) && match.size() > 1) {
            return match.str(1);
        }
        return std::nullopt;
    }

    struct link_metadata {
        std::string title;
        std::string description;
        std::string image_url;
    };

    inline std::optional<std::string> extract_meta_content(std::string_view html, std::string_view key, std::string_view attr) {
        const std::regex rx(
            std::format(R"raw(<meta[^>]*{}\s*=\s*["']{}["'][^>]*content\s*=\s*["']([^"']+)["'][^>]*>)raw", attr, key),
            std::regex::icase
        );
        std::smatch match;
        std::string html_copy(html);
        if (std::regex_search(html_copy, match, rx) && match.size() > 1) {
            return ::helpers::StringHelper::strip_html_tags(match[1].str());
        }
        return std::nullopt;
    }

    inline link_metadata fetch_link_metadata(const std::string& link) {
        link_metadata md{};
        http::fetch client{15};
        const std::vector<std::string> headers = {
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
        };

        const std::string lower_link = ::helpers::StringHelper::to_lower(link);
        const bool is_youtube = (lower_link.find("youtube.com") != std::string::npos || lower_link.find("youtu.be") != std::string::npos);
        if (is_youtube) {
            try {
                const std::string oembed_url = "https://www.youtube.com/oembed?url=" + ::helpers::StringHelper::url_encode(link) + "&format=json";
                const std::string body = client(oembed_url, headers);
                std::smatch match;
                const std::regex title_rx(R"raw("title"\s*:\s*"((?:\\.|[^"\\])*)")raw");
                if (std::regex_search(body, match, title_rx) && match.size() > 1) {
                    md.title = decode_json_string(match[1].str());
                }
                const std::regex thumb_rx(R"raw("thumbnail_url"\s*:\s*"((?:\\.|[^"\\])*)")raw");
                if (std::regex_search(body, match, thumb_rx) && match.size() > 1) {
                    md.image_url = decode_json_string(match[1].str());
                }
            } catch (const std::exception& e) {
                HTTP_WARN_FMT("Failed to fetch YouTube oEmbed metadata for {}: {}", link, e.what());
            }
        }

        try {
            const std::string html = client(link, headers);

            if (md.title.empty()) {
                if (auto og_title = extract_meta_content(html, "og:title", "property")) {
                    md.title = *og_title;
                } else {
                    std::smatch match;
                    const std::regex title_rx(R"raw(<title[^>]*>(.*?)</title>)raw", std::regex::icase);
                    if (std::regex_search(html, match, title_rx) && match.size() > 1) {
                        md.title = ::helpers::StringHelper::strip_html_tags(match[1].str());
                    }
                }
            }

            if (md.description.empty()) {
                if (auto og_desc = extract_meta_content(html, "og:description", "property")) {
                    md.description = *og_desc;
                } else if (auto desc = extract_meta_content(html, "description", "name")) {
                    md.description = *desc;
                }
            }

            if (md.image_url.empty()) {
                if (auto og_image = extract_meta_content(html, "og:image", "property")) {
                    md.image_url = *og_image;
                }
            }
        } catch (const std::exception& e) {
            HTTP_WARN_FMT("Failed to fetch HTML metadata for {}: {}", link, e.what());
        }

        md.title = trim_copy(md.title);
        md.description = trim_copy(md.description);
        md.image_url = trim_copy(md.image_url);
        return md;
    }

    inline uint32_t read_u32_be(const unsigned char* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |
               static_cast<uint32_t>(p[3]);
    }

    inline uint64_t read_u64_be(const unsigned char* p) {
        return (static_cast<uint64_t>(read_u32_be(p)) << 32) | read_u32_be(p + 4);
    }

    inline std::optional<double> parse_mp4_duration_bytes(const unsigned char* data, size_t size) {
        size_t offset = 0;
        while (offset + 8 <= size) {
            uint64_t box_size = read_u32_be(data + offset);
            std::string_view box_type(reinterpret_cast<const char*>(data + offset + 4), 4);
            
            size_t header_size = 8;
            if (box_size == 1) {
                if (offset + 16 > size) break;
                box_size = read_u64_be(data + offset + 8);
                header_size = 16;
            } else if (box_size == 0) {
                box_size = size - offset;
            }
            
            if (box_size < header_size || offset + box_size > size) {
                break;
            }
            
            if (box_type == "moov") {
                size_t sub_offset = offset + header_size;
                size_t sub_limit = offset + box_size;
                while (sub_offset + 8 <= sub_limit) {
                    uint64_t sub_size = read_u32_be(data + sub_offset);
                    std::string_view sub_type(reinterpret_cast<const char*>(data + sub_offset + 4), 4);
                    
                    size_t sub_header = 8;
                    if (sub_size == 1) {
                        if (sub_offset + 16 > sub_limit) break;
                        sub_size = read_u64_be(data + sub_offset + 8);
                        sub_header = 16;
                    } else if (sub_size == 0) {
                        sub_size = sub_limit - sub_offset;
                    }
                    
                    if (sub_size < sub_header || sub_offset + sub_size > sub_limit) {
                        break;
                    }
                    
                    if (sub_type == "mvhd") {
                        size_t mvhd_data_offset = sub_offset + sub_header;
                        if (mvhd_data_offset + 4 > sub_limit) break;
                        
                        uint8_t version = data[mvhd_data_offset];
                        if (version == 0) {
                            if (mvhd_data_offset + 12 + 8 > sub_limit) break;
                            uint32_t timescale = read_u32_be(data + mvhd_data_offset + 12);
                            uint32_t duration = read_u32_be(data + mvhd_data_offset + 16);
                            if (timescale > 0 && duration > 0 && duration != 0xFFFFFFFF) {
                                return static_cast<double>(duration) / timescale;
                            }
                        } else if (version == 1) {
                            if (mvhd_data_offset + 20 + 12 > sub_limit) break;
                            uint32_t timescale = read_u32_be(data + mvhd_data_offset + 20);
                            uint64_t duration = read_u64_be(data + mvhd_data_offset + 24);
                            if (timescale > 0 && duration > 0 && duration != 0xFFFFFFFFFFFFFFFF) {
                                return static_cast<double>(duration) / timescale;
                            }
                        }
                    }
                    sub_offset += sub_size;
                }
            }
            offset += box_size;
        }
        return std::nullopt;
    }

    inline std::optional<double> probe_mp4_duration(const std::string& url) {
        try {
            http::fetch client{5}; // 5-second timeout for header probe
            // Request the first 256KB
            std::vector<std::string> headers = {
                "Range: bytes=0-262144",
                "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            };
            std::string buffer = client(url, headers);
            if (!buffer.empty()) {
                return parse_mp4_duration_bytes(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size());
            }
        } catch (const std::exception& e) {
            HTTP_DEBUG_FMT("Failed to probe MP4 duration for {}: {}", url, e.what());
        }
        return std::nullopt;
    }

    inline std::string shell_quote(std::string_view value) {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('\'');
        for (char c : value) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out.push_back(c);
            }
        }
        out.push_back('\'');
        return out;
    }

    inline std::optional<double> parse_duration_seconds(std::string value) {
        value = trim_copy(std::move(value));
        if (value.empty()) {
            return std::nullopt;
        }
        try {
            double parsed = std::stod(value);
            if (parsed > 0.0 && std::isfinite(parsed)) {
                return parsed;
            }
        } catch (...) {
        }
        return std::nullopt;
    }

    inline std::optional<double> probe_youtube_duration(const std::string& url) {
        auto video_id = extract_youtube_video_id(url);
        if (!video_id.has_value()) {
            return std::nullopt;
        }

        const std::string watch_url = "https://www.youtube.com/watch?v=" + *video_id;

        // Fast Primary Path: Direct HTTP fetch & regex scrape lengthSeconds / approxDurationMs (~20ms)
        try {
            http::fetch client{4};
            std::vector<std::string> headers = {
                "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            };
            std::string html = client(watch_url, headers);
            if (!html.empty()) {
                std::smatch match;
                static const std::regex length_seconds_rx(R"raw("lengthSeconds"\s*:\s*"(\d+)")raw");
                static const std::regex approx_duration_ms_rx(R"raw("approxDurationMs"\s*:\s*"(\d+)")raw");

                if (std::regex_search(html, match, length_seconds_rx) && match.size() > 1) {
                    try {
                        auto secs = std::stod(match.str(1));
                        if (secs > 0.0 && std::isfinite(secs)) {
                            return secs;
                        }
                    } catch (...) {}
                }
                if (std::regex_search(html, match, approx_duration_ms_rx) && match.size() > 1) {
                    try {
                        auto ms = std::stod(match.str(1));
                        if (ms > 0.0 && std::isfinite(ms)) {
                            return ms / 1000.0;
                        }
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // Fallback Path: Call yt-dlp with hard 3s timeout only if HTTP scrape fails
        static const std::string ytdlp_path = rouen::platform::find_executable("yt-dlp");
        if (!ytdlp_path.empty()) {
            const std::string command = std::format(
                "{} --no-warnings --socket-timeout 3 --quiet --print duration {} 2>/dev/null",
                shell_quote(ytdlp_path),
                shell_quote(watch_url)
            );

            FILE* pipe = popen(command.c_str(), "r");
            if (pipe) {
                std::array<char, 64> buffer{};
                std::string output;
                while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                    output += buffer.data();
                }
                [[maybe_unused]] int close_rc = pclose(pipe);
                auto dur = parse_duration_seconds(output);
                if (dur.has_value()) {
                    return dur;
                }
            }
        }

        return std::nullopt;
    }

    inline std::optional<double> probe_media_duration_ffprobe(const std::string& url) {
        static const std::string ffprobe_path = rouen::platform::find_executable("ffprobe");
        if (ffprobe_path.empty() || url.empty()) {
            return std::nullopt;
        }

        const std::string command = std::format(
            "{} -v error -show_entries format=duration "
            "-of default=noprint_wrappers=1:nokey=1 "
            "-rw_timeout 2000000 -i {} 2>/dev/null",
            shell_quote(ffprobe_path),
            shell_quote(url)
        );

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return std::nullopt;
        }

        std::array<char, 256> buffer{};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }
        [[maybe_unused]] int close_rc = pclose(pipe);

        return parse_duration_seconds(output);
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
        try {
            std::string interval_str = repo_.get_setting("refresh_interval", "3600");
            refresh_interval_s_ = std::stoi(interval_str);
        } catch (...) {
            refresh_interval_s_ = 3600;
        }
        RSS_INFO_FMT("RSSHost loaded settings: timeout={}s, auto_timeout={}, refresh_interval={}s", 
                     timeout_s_, auto_timeout_enabled_ ? "true" : "false", refresh_interval_s_);

        // Load feeds from database synchronously so they are available immediately
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
                
                // Load existing items from database (only load latest 5 items to keep startup instant)
                repo_.scan_items_limit(feed_id, 5, [feed_ptr](const char* item_link, const char* item_enclosure, const char* item_title, 
                                                 const char* item_desc, const char* item_pub_date, const char* item_img_url,
                                                 std::optional<double> watermark, std::optional<double> media_duration_seconds) {
                    auto publish_date = parse_db_date(item_pub_date ? item_pub_date : "");
                    
                    media::rss::feed_item item(
                        item_title ? item_title : "",
                        item_link ? item_link : "",
                        item_desc ? item_desc : "",
                        item_enclosure ? item_enclosure : "",
                        item_img_url ? item_img_url : "",
                        publish_date,
                        media_duration_seconds
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

        // Initialize staggered last refresh times to spread background updates evenly
        {
            std::lock_guard<std::mutex> lock(last_refresh_mutex_);
            auto now = std::chrono::system_clock::now();
            for (const auto& url : urls) {
                if (!url.empty()) {
                    // Stagger between now and refresh_interval_s_ seconds in the past
                    int offset_s = std::rand() % refresh_interval_s_;
                    feed_last_refresh_times_[url] = now - std::chrono::seconds(offset_s);
                }
            }
        }

        // Load feeds and start background tasks in a background thread to prevent UI thread freezes
        init_thread_ = std::jthread([this, urls_to_refresh = std::move(urls)](std::stop_token stoken) mutable {
            initializeAsync(stoken, std::move(urls_to_refresh));
        });
        
        RSS_INFO("RSSHost constructor completed (initialization deferred to background)");
        
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
        
        init_thread_.request_stop();
        if (init_thread_.joinable()) {
            init_thread_.join();
        }
        duration_backfill_thread_.request_stop();
        if (duration_backfill_thread_.joinable()) {
            duration_backfill_thread_.join();
        }
        periodic_refresh_thread_.request_stop();
        {
            std::lock_guard<std::mutex> lock(fetch_threads_mutex_);
            for (auto& t : active_fetch_threads_) {
                t.request_stop();
            }
            active_fetch_threads_.clear(); // std::jthread destructor will join them
        }
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
                                            const char* pub_date, const char* image_url, std::optional<double> watermark,
                                            std::optional<double> media_duration_seconds) {
            FeedItem item;
            item.feed_id = feed_id;
            item.feed_title = feed_title ? feed_title : "";
            item.link = link ? link : "";
            item.enclosure = enclosure ? enclosure : "";
            item.title = title ? title : "";
            item.description = description ? description : "";
            item.image_url = image_url ? image_url : "";
            item.watermark = watermark;
            item.media_duration_seconds = media_duration_seconds;
            
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
    void addFeeds(std::vector<std::string> urls, bool open_added_card = false) {
        RSS_WARN_FMT("addFeeds called with {} URLs", urls.size());
        for (const auto& url : urls) {
            RSS_WARN_FMT("URL being added: {}", url);
        }
        refreshFeeds(std::move(urls), open_added_card);
    }

    /**
     * Add a new feed by URL
     */
    bool addFeed(const std::string& url, bool open_added_card = true) {
        try {
            std::vector<std::string> urls = {url};
            refreshFeeds(std::move(urls), open_added_card);
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

    void saveSmartList(const std::string& title, const media::rss::filter_group& filter) {
        std::string filter_json = glz::write_json(filter).value_or("");
        if (!filter_json.empty()) {
            repo_.save_smart_list(title, filter_json);
        }
    }

    void deleteSmartList(const std::string& title) {
        repo_.delete_smart_list(title);
    }

    long long find_subscribed_youtube_feed_id(const std::string& channel_id, const std::string& feed_url, const std::string& channel_title) const {
        std::lock_guard<std::mutex> lock(yt_sub_mutex_);
        if (!channel_id.empty()) {
            auto it = yt_channel_id_map_.find(channel_id);
            if (it != yt_channel_id_map_.end()) return it->second;
        }
        if (!feed_url.empty()) {
            auto it = yt_url_map_.find(feed_url);
            if (it != yt_url_map_.end()) return it->second;
        }
        if (!channel_title.empty()) {
            auto it = yt_title_map_.find(channel_title);
            if (it != yt_title_map_.end()) return it->second;
            it = yt_title_map_.find("YouTube Channel: " + channel_title);
            if (it != yt_title_map_.end()) return it->second;
        }
        return -1;
    }

    void rebuild_yt_index() {
        std::unordered_map<std::string, long long> cid_map;
        std::unordered_map<std::string, long long> url_map;
        std::unordered_map<std::string, long long> title_map;

        {
            std::lock_guard<std::mutex> lock(feeds_mutex_);
            for (const auto& f : feeds_) {
                if (!f) continue;
                bool is_yt = (f->feed_link.find("youtube.com") != std::string::npos || 
                              f->source_link.find("youtube.com") != std::string::npos ||
                              f->feed_title.find("YouTube") != std::string::npos);
                if (!is_yt) continue;

                if (!f->feed_link.empty()) {
                    url_map[f->feed_link] = f->repo_id;
                    size_t cid_pos = f->feed_link.find("channel_id=");
                    if (cid_pos != std::string::npos) {
                        std::string cid = f->feed_link.substr(cid_pos + 11);
                        size_t amp = cid.find('&');
                        if (amp != std::string::npos) cid = cid.substr(0, amp);
                        if (!cid.empty()) cid_map[cid] = f->repo_id;
                    }
                }
                if (!f->source_link.empty()) {
                    url_map[f->source_link] = f->repo_id;
                    size_t cid_pos = f->source_link.find("channel_id=");
                    if (cid_pos != std::string::npos) {
                        std::string cid = f->source_link.substr(cid_pos + 11);
                        size_t amp = cid.find('&');
                        if (amp != std::string::npos) cid = cid.substr(0, amp);
                        if (!cid.empty()) cid_map[cid] = f->repo_id;
                    }
                }
                if (!f->feed_title.empty()) {
                    title_map[f->feed_title] = f->repo_id;
                }
            }
        }

        std::lock_guard<std::mutex> lock(yt_sub_mutex_);
        yt_channel_id_map_ = std::move(cid_map);
        yt_url_map_ = std::move(url_map);
        yt_title_map_ = std::move(title_map);
    }

    struct SmartListInfo {
        std::string title;
        media::rss::filter_group filter;
    };

    std::vector<SmartListInfo> getSmartLists() {
        std::vector<SmartListInfo> result;
        repo_.scan_smart_lists([&result](const std::string& title, const std::string& filter_json) {
            SmartListInfo info;
            info.title = title;
            auto err = glz::read_json(info.filter, filter_json);
            if (!err) {
                result.push_back(std::move(info));
            }
        });
        return result;
    }

    std::vector<FeedItem> getFilteredItems(const media::rss::filter_group& filter) {
        std::vector<FeedItem> items;
        repo_.scan_filtered_items(filter, [&items](long long feed_id, const std::string& feed_title,
                                                  const std::string& link, const std::string& enclosure,
                                                  const std::string& title, const std::string& description,
                                                  const std::string& pub_date, const std::string& image_url,
                                                  std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            auto publish_date = parse_db_date(pub_date);
            FeedItem item{
                .title = title,
                .description = description,
                .clean_description = "",
                .link = link,
                .enclosure = enclosure,
                .image_url = image_url,
                .publish_date = publish_date,
                .extracted_media_urls = {},
                .watermark = watermark,
                .media_duration_seconds = media_duration_seconds,
                .feed_id = feed_id,
                .feed_title = feed_title
            };
            if (!item.description.empty()) {
                item.extracted_media_urls = media::html::extract_media_urls(item.description);
                item.clean_description = ::helpers::StringHelper::strip_html_tags(item.description);
                if (item.clean_description.length() > 100) {
                    item.clean_description = item.clean_description.substr(0, 97) + "...";
                }
            }
            items.push_back(std::move(item));
        });
        return items;
    }

    std::set<std::string> getFeedTags(long long feed_id) {
        return repo_.get_feed_tags(feed_id);
    }

    std::vector<std::string> getAvailableTags() {
        return repo_.get_available_tags();
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
    std::vector<FeedItem> getFeedItems(long long feed_id, int limit = 100) {
        std::vector<FeedItem> items;
        
        repo_.scan_items_limit(feed_id, limit, [&items, feed_id](const char* link, const char* enclosure, const char* title, 
                                         const char* description, const char* pub_date, const char* image_url,
                                         std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            // Mark unused parameters to avoid warnings
            (void)link; (void)enclosure; (void)title;
            (void)description; (void)pub_date; (void)image_url;
            
            auto publish_date = parse_db_date(pub_date ? pub_date : "");
            
            // Create and store the item
            FeedItem item{
                .title = title ? title : "",
                .description = description ? description : "",
                .clean_description = "",
                .link = link ? link : "",
                .enclosure = enclosure ? enclosure : "",
                .image_url = image_url ? image_url : "",
                .publish_date = publish_date,
                .extracted_media_urls = {},
                .watermark = watermark,
                .media_duration_seconds = media_duration_seconds,
                .feed_id = feed_id,
                .feed_title = ""
            };
            
            // Enhanced: Extract media URLs from description content
            if (!item.description.empty()) {
                item.extracted_media_urls = media::html::extract_media_urls(item.description);
                item.clean_description = ::helpers::StringHelper::strip_html_tags(item.description);
                if (item.clean_description.length() > 100) {
                    item.clean_description = item.clean_description.substr(0, 97) + "...";
                }
            }
            
            items.push_back(std::move(item));
        });
        
        // Sort items by publish date (newest first)
        std::sort(items.begin(), items.end(), [](const FeedItem& a, const FeedItem& b) {
            return a.publish_date > b.publish_date;
        });
        
        return items;
    }

    bool addFeedItem(long long feed_id, std::string_view item_link, std::string_view item_title = "") {
        if (feed_id < 0) {
            RSS_ERROR_FMT("Invalid feed_id when adding feed item: {}", feed_id);
            return false;
        }

        std::string link = trim_copy(std::string(item_link));

        if (link.empty()) {
            RSS_WARN("Cannot add feed item with empty URL");
            return false;
        }

        std::string title = trim_copy(std::string(item_title));
        auto metadata = fetch_link_metadata(link);
        if (title.empty()) {
            title = metadata.title.empty() ? link : metadata.title;
        }

        auto now = std::chrono::system_clock::now();
        std::string pub_date = std::format("{:%F %T}", now, now);
        repo_.upsert_item_by_link(feed_id, link, title, "", metadata.description, pub_date, metadata.image_url);

        std::lock_guard<std::mutex> lock(feeds_mutex_);
        for (auto& feed : feeds_) {
            if (feed->repo_id != feed_id) {
                continue;
            }

            auto item_pos = std::find_if(feed->items.begin(), feed->items.end(), [&](const auto& item) {
                return item.link == link;
            });

            if (item_pos == feed->items.end()) {
                media::rss::feed_item new_item(title, link, metadata.description, "", metadata.image_url, now);
                feed->items.push_back(std::move(new_item));
            } else {
                item_pos->title = title;
                item_pos->updated = now;
                item_pos->description = metadata.description;
                item_pos->enclosure = "";
                item_pos->image_url = metadata.image_url;
            }

            std::sort(feed->items.begin(), feed->items.end(), [](const media::rss::feed_item& a, const media::rss::feed_item& b) {
                return a.updated > b.updated;
            });
            return true;
        }

        RSS_WARN_FMT("Added item to DB, but feed {} was not present in memory cache", feed_id);
        return true;
    }
    
    /**
     * Get a specific item from a feed by its link
     */
    std::optional<FeedItem> getFeedItem(long long feed_id, const std::string& item_link, const std::string& item_title) {
        std::optional<FeedItem> result;
        
        repo_.scan_items(feed_id, [&result, &item_link, &item_title, feed_id](const char* link, const char* enclosure, const char* title, 
                                                     const char* description, const char* pub_date, const char* image_url,
                                                     std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            // Mark unused parameters to avoid warnings
            (void)link; (void)enclosure; (void)title;
            (void)description; (void)pub_date; (void)image_url;
            
            if (link && item_link == link && (item_title.empty() || (title && item_title == title))) {
                auto publish_date = parse_db_date(pub_date ? pub_date : "");
                
                result = FeedItem{
                    .title = title ? title : "",
                    .description = description ? description : "",
                    .clean_description = "",
                    .link = link,
                    .enclosure = enclosure ? enclosure : "",
                    .image_url = image_url ? image_url : "",
                    .publish_date = publish_date,
                    .extracted_media_urls = {},
                    .watermark = watermark,
                    .media_duration_seconds = media_duration_seconds,
                    .feed_id = feed_id,
                    .feed_title = ""
                };
                
                // Enhanced: Extract media URLs from description content
                if (!result->description.empty()) {
                    result->extracted_media_urls = media::html::extract_media_urls(result->description);
                    result->clean_description = ::helpers::StringHelper::strip_html_tags(result->description);
                    if (result->clean_description.length() > 100) {
                        result->clean_description = result->clean_description.substr(0, 97) + "...";
                    }
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
                {
                    std::lock_guard<std::mutex> lock(last_refresh_mutex_);
                    feed_last_refresh_times_[*feed_url] = std::chrono::system_clock::now();
                }
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

    void refreshFeeds(std::vector<std::string> urls, bool open_added_card = false) {
        std::lock_guard<std::mutex> threads_lock(fetch_threads_mutex_);
        
        // Clean up any finished threads
        active_fetch_threads_.erase(
            std::remove_if(active_fetch_threads_.begin(), active_fetch_threads_.end(),
                           [](const auto& t) { return !t.joinable(); }),
            active_fetch_threads_.end());
            
        active_fetch_threads_.emplace_back([this, url_list = std::move(urls), open_added_card] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            
            int success_count = 0;
            int error_count = 0;
            
            // Process feeds sequentially with spacing to prevent rate-limiting and DB locking conflicts
            for (size_t j = 0; j < url_list.size(); ++j) {
                if (quit_job()) break;
                
                // Add a polite spacing delay between requests (except the first request)
                if (j > 0) {
                    int delay_ms = 500 + (std::rand() % 500); // 500ms to 1000ms
                    for (int d = 0; d < delay_ms; d += 50) {
                        if (quit_job()) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
                
                if (quit_job()) break;
                
                try {
                    RSS_INFO_FMT("Starting to process feed: {}", url_list[j]);

                    // Respect per-feed backoff if set (e.g., after recent 429)
                    {
                        std::lock_guard<std::mutex> lock(backoff_mutex_);
                        if (auto it = feed_backoff_until_.find(url_list[j]); it != feed_backoff_until_.end()) {
                            auto now = std::chrono::system_clock::now();
                            if (now < it->second) {
                                auto secs_left = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
                                RSS_WARN_FMT("Skipping feed {} due to backoff ({}s remaining)", url_list[j], secs_left);
                                ++error_count;
                                continue;
                            } else {
                                // Backoff expired, remove entry
                                feed_backoff_until_.erase(it);
                            }
                        }
                    }
                    
                    auto feed_ptr = addFeedSync(url_list[j], quit_job);
                    
                    if (feed_ptr) {
                        RSS_INFO_FMT("Successfully fetched and processed feed: {}", url_list[j]);
                        ++success_count;
                        if (open_added_card && feed_ptr->repo_id > 0) {
                            std::string feed_uri = std::format("rss-feed:{}", feed_ptr->repo_id);
                            "create_card"_sfn(feed_uri);
                        }
                    } else {
                        ++error_count;
                    }
                } catch (const std::exception& e) {
                    RSS_ERROR_FMT("Failed to add feed {}: {}", url_list[j], e.what());
                    ++error_count;
                } catch (...) {
                    RSS_ERROR_FMT("Failed to add feed {} with unknown error", url_list[j]);
                    ++error_count;
                }
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
        std::string resolved_url;
        try {
            resolved_url = resolveYoutubeUrl(std::string(url));
        } catch (const std::exception& e) {
            RSS_WARN_FMT("Failed to resolve YouTube URL {}: {}", url, e.what());
            resolved_url = std::string(url);
        }

        try {
            std::shared_ptr<media::rss::feed> feed_ptr;
            try {
                // Download and parse the feed
                feed_ptr = std::make_shared<media::rss::feed>(getFeed(resolved_url));
            } catch (const std::exception& e) {
                // If this is a YouTube feed, create a placeholder instead of failing completely,
                // since YouTube RSS endpoints frequently return 404/500 errors.
                bool is_youtube = (resolved_url.find("youtube.com/feeds/videos.xml") != std::string::npos);
                if (is_youtube) {
                    RSS_WARN_FMT("YouTube feed fetch failed: {}. Creating placeholder feed.", e.what());
                    feed_ptr = std::make_shared<media::rss::feed>();
                    feed_ptr->source_link = resolved_url;
                    feed_ptr->feed_link = resolved_url;
                    feed_ptr->is_placeholder = true;
                    
                    // Extract channel name/handle for title
                    std::string title = std::string(url);
                    size_t at_pos = title.find("@");
                    if (at_pos != std::string::npos) {
                        title = title.substr(at_pos);
                    } else {
                        size_t ch_pos = title.find("channel_id=");
                        if (ch_pos != std::string::npos) {
                            title = "YouTube Channel: " + title.substr(ch_pos + 11);
                        }
                    }
                    feed_ptr->feed_title = title;
                } else {
                    throw; // Rethrow for other feeds
                }
            }
            
            if (quitting()) return nullptr;

            if (feed_ptr->is_permanently_redirected) {
                RSS_INFO_FMT("Permanent 301/308 redirect detected: {} -> {}. Updating database.", resolved_url, feed_ptr->source_link);
                repo_.update_feed_url(resolved_url, feed_ptr->source_link);
            }

            // Resolve missing duration metadata before taking the feeds mutex to avoid
            // blocking UI reads while running network/process probes.
            constexpr size_t k_max_duration_probe_attempts_per_feed = 12;
            constexpr auto k_duration_probe_budget_per_feed = std::chrono::milliseconds(2500);
            const auto duration_probe_deadline = std::chrono::steady_clock::now() + k_duration_probe_budget_per_feed;
            size_t duration_probe_attempts = 0;

            for (auto & item : feed_ptr->items) {
                if (quitting()) {
                    break;
                }
                if (duration_probe_attempts >= k_max_duration_probe_attempts_per_feed ||
                    std::chrono::steady_clock::now() >= duration_probe_deadline) {
                    break;
                }
                if (!item.media_duration_seconds.has_value() || item.media_duration_seconds.value() <= 0.0) {
                    std::string media_url = item.get_best_media_url();
                    if (!media_url.empty() && is_youtube_media_url(media_url)) {
                        if (quitting()) break;
                        if (duration_probe_attempts >= k_max_duration_probe_attempts_per_feed ||
                            std::chrono::steady_clock::now() >= duration_probe_deadline) {
                            break;
                        }
                        ++duration_probe_attempts;
                        auto probed = probe_youtube_duration(media_url);
                        if (probed) {
                            item.media_duration_seconds = *probed;
                        }
                    }
                    if (!media_url.empty() && (media_url.find(".mp4") != std::string::npos || media_url.find(".MP4") != std::string::npos)) {
                        if (quitting()) break;
                        if (duration_probe_attempts >= k_max_duration_probe_attempts_per_feed ||
                            std::chrono::steady_clock::now() >= duration_probe_deadline) {
                            break;
                        }
                        ++duration_probe_attempts;
                        auto probed = probe_mp4_duration(media_url);
                        if (probed) {
                            item.media_duration_seconds = *probed;
                        }
                    }
                    if (!item.media_duration_seconds.has_value() || item.media_duration_seconds.value() <= 0.0) {
                        if (quitting()) break;
                        if (duration_probe_attempts >= k_max_duration_probe_attempts_per_feed ||
                            std::chrono::steady_clock::now() >= duration_probe_deadline) {
                            break;
                        }
                        ++duration_probe_attempts;
                        auto probed = probe_media_duration_ffprobe(media_url);
                        if (probed) {
                            item.media_duration_seconds = *probed;
                        }
                    }
                }
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
                
                // Only update metadata if the fetched feed is NOT a placeholder
                if (!feed_ptr->is_placeholder) {
                    (*pos)->feed_title = feed_ptr->feed_title;
                    (*pos)->feed_description = feed_ptr->feed_description;
                    (*pos)->feed_link = feed_ptr->feed_link;
                    (*pos)->source_link = feed_ptr->source_link; // Keep updated to final URL
                    (*pos)->set_image(feed_ptr->image_url());
                }
                
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
            
            {
                std::lock_guard<std::mutex> lock(last_refresh_mutex_);
                feed_last_refresh_times_[feed_ptr->source_link] = std::chrono::system_clock::now();
            }
            
            // Prepare items for batch insert
            std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::optional<double>>> items_batch;
            items_batch.reserve(feed_ptr->items.size());
            
            for (auto & item : feed_ptr->items) {
                // Format the date time
                auto const pub_date = std::format("{:%F %T}", item.updated, item.updated);
                
                // Add to batch collection
                items_batch.emplace_back(
                    item.title,
                    item.enclosure,
                    item.link,
                    item.description,
                    pub_date,
                    item.image_url,
                    item.media_duration_seconds
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
            RSS_ERROR_FMT("Error processing feed {}: {}", url, e.what());
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
            
            // Reset failure count on success and clear any backoff
            {
                std::lock_guard<std::mutex> lock(failure_counts_mutex_);
                feed_failure_counts_[url_str] = 0;
            }
            {
                std::lock_guard<std::mutex> lock(backoff_mutex_);
                feed_backoff_until_.erase(url_str);
            }

            RSS_INFO_FMT("Successfully fetched feed: {} - Title: {}", url, parser.feed_title);
            return parser;
        } catch (const std::exception& e) {
            // Increment failure count on failure
            {
                std::lock_guard<std::mutex> lock(failure_counts_mutex_);
                feed_failure_counts_[url_str] = failure_count + 1;
            }

            // If this was a rate-limit (HTTP 429) we should back off intelligently
            try {
                std::string err = e.what();
                if (err.find("HTTP error 429") != std::string::npos) {
                    using namespace std::chrono;
                    auto now = system_clock::now();
                    // Exponential backoff in minutes: 5, 10, 20, 40, 60 (cap at 60)
                    int minutes = std::min((failure_count + 1) * 5, 60);
                    // Add small jitter of up to 30% to avoid herd effects
                    int max_jitter = std::max(1, (minutes * 30) / 100);
                    int jitter = std::rand() % (max_jitter + 1);
                    minutes += jitter;

                    auto until = now + std::chrono::minutes(minutes);

                    {
                        std::lock_guard<std::mutex> lock(backoff_mutex_);
                        feed_backoff_until_[url_str] = until;
                    }
                    RSS_WARN_FMT("Received HTTP 429 for {}. Backing off for {} minutes (with jitter).", url, minutes);
                }
            } catch (...) {
                // Ignore any parsing errors
            }

            RSS_ERROR_FMT("Failed to fetch feed {}: {}", url, e.what());
            throw std::runtime_error(std::string("Failed to fetch feed: ") + e.what());
        }
    }

    void initializeAsync(std::stop_token stoken, std::vector<std::string> urls) {
        auto quit_job = [stoken]() -> bool {
            return "quitting"_fnb() || stoken.stop_requested();
        };

        if (quit_job()) return;

        // Load podcasts from podcasts.txt file if it exists
        loadPodcastsFromFile();
        
        if (quit_job()) return;

        // Start the periodic background refresh loop
        startRefreshLoop();

        // Start background yt-dlp backfill for items with missing YouTube duration
        startDurationBackfill();
        
        // Defer initial feed refresh to allow the startup phase to complete smoothly
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (quit_job()) return;
        }
        
        RSS_INFO("RSSHost starting initial feed refresh asynchronously...");
        refreshFeeds(std::move(urls));
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

        // Music
        if (lower_url.find("music") != std::string::npos ||
            lower_url.find("bandcamp") != std::string::npos ||
            lower_url.find("soundcloud") != std::string::npos ||
            lower_url.find("spotify") != std::string::npos ||
            lower_url.find("mixcloud") != std::string::npos ||
            lower_url.find("rollingstone") != std::string::npos ||
            lower_url.find("billboard") != std::string::npos ||
            lower_url.find("pitchfork") != std::string::npos ||
            lower_url.find("nme.com") != std::string::npos ||
            lower_url.find("stereogum") != std::string::npos) {
            tags.push_back("Music");
        }

        // Comedy
        if (lower_url.find("comedy") != std::string::npos ||
            lower_url.find("standup") != std::string::npos ||
            lower_url.find("funny") != std::string::npos ||
            lower_url.find("satire") != std::string::npos ||
            lower_url.find("theonion") != std::string::npos ||
            lower_url.find("collegehumor") != std::string::npos ||
            lower_url.find("cracked") != std::string::npos) {
            tags.push_back("Comedy");
        }

        // Documentary
        if (lower_url.find("documentary") != std::string::npos ||
            lower_url.find("docu") != std::string::npos ||
            lower_url.find("pbs.org") != std::string::npos ||
            lower_url.find("nationalgeographic") != std::string::npos ||
            lower_url.find("history.com") != std::string::npos ||
            lower_url.find("arte.tv") != std::string::npos) {
            tags.push_back("Documentary");
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

    void startRefreshLoop() {
        periodic_refresh_thread_ = std::jthread([this] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            
            // Wait 5 seconds after startup to allow other initialization tasks to complete
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            while (!quit_job()) {
                // Wake up every 30 seconds to check if any feed is due for refresh
                for (int i = 0; i < 60; ++i) {
                    if (quit_job() || should_force_refresh_.load()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                
                if (quit_job()) break;
                
                bool force = should_force_refresh_.exchange(false);
                std::vector<std::string> urls_to_refresh;
                auto now = std::chrono::system_clock::now();
                
                {
                    std::lock_guard<std::mutex> lock(feeds_mutex_);
                    std::lock_guard<std::mutex> r_lock(last_refresh_mutex_);
                    
                    for (auto const& f : feeds_) {
                        bool due = false;
                        if (force) {
                            due = true;
                        } else {
                            auto it = feed_last_refresh_times_.find(f->source_link);
                            if (it == feed_last_refresh_times_.end()) {
                                due = true; // Never refreshed
                            } else {
                                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                                if (elapsed >= refresh_interval_s_) {
                                    due = true;
                                }
                            }
                        }
                        
                        if (due) {
                            urls_to_refresh.push_back(f->source_link);
                            // Mark as updated now to prevent double scheduling
                            feed_last_refresh_times_[f->source_link] = now;
                        }
                    }
                }
                
                if (!urls_to_refresh.empty()) {
                    RSS_INFO_FMT("Starting periodic refresh of {} due feeds (staggered)...", urls_to_refresh.size());
                    refreshFeeds(urls_to_refresh);
                }
            }
        });
    }

    void startDurationBackfill() {
        duration_backfill_thread_ = std::jthread([this](std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };

            // Wait 30 seconds for initial feed load to settle before backfilling.
            for (int i = 0; i < 60; ++i) {
                if (quit_job()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            static const std::string ytdlp_path = rouen::platform::find_executable("yt-dlp");
            if (ytdlp_path.empty()) {
                RSS_WARN("yt-dlp not found — YouTube duration backfill skipped");
                return;
            }

            RSS_INFO("Starting YouTube duration backfill via yt-dlp...");
            int total_updated = 0;

            while (!quit_job()) {
                // Fetch a small batch of items still missing duration.
                std::vector<std::pair<std::string, std::string>> batch; // (link, enclosure)
                repo_.scan_items_missing_youtube_duration(20, [&batch](const std::string& link, const std::string& enc) {
                    batch.emplace_back(link, enc);
                });

                if (batch.empty()) {
                    RSS_INFO_FMT("YouTube duration backfill complete — {} items updated", total_updated);
                    return;
                }

                for (auto& [link, enclosure] : batch) {
                    if (quit_job()) return;

                    // Prefer the stored enclosure; fall back to deriving a watch URL from the item link.
                    std::string probe_url = enclosure.empty() ? link : enclosure;
                    auto dur = probe_youtube_duration(probe_url);
                    if (!dur.has_value() && probe_url != link) {
                        dur = probe_youtube_duration(link);
                    }

                    if (dur.has_value()) {
                        repo_.update_item_duration(link, *dur);
                        ++total_updated;
                        RSS_INFO_FMT("Backfilled duration {:.0f}s for {}", *dur, link);
                    } else {
                        // Mark unavailable/deleted videos with 0 so they are excluded
                        // from future backfill queries and don't cause an infinite retry loop.
                        repo_.update_item_duration(link, 0.0);
                        RSS_INFO_FMT("Marking unavailable video with duration=0 for {}", link);
                    }

                    // Rate-limit: one probe per second to avoid hammering YouTube.
                    for (int i = 0; i < 10; ++i) {
                        if (quit_job()) return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            }
        });
    }
};

} // namespace rouen::hosts
