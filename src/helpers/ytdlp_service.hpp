#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace rouen::helpers {

struct ytdlp_search_result {
    std::string id;
    std::string title;
    std::string url;
    double duration{0.0};
    std::string duration_string;
    std::string channel;
    std::string channel_id;
    std::string upload_date;
    long long view_count{0};
};

struct ytdlp_stream_result {
    std::vector<std::string> urls;
    std::string raw_output;
    bool success{false};
};

class ytdlp_service {
public:
    // Check if yt-dlp executable exists and get path
    static std::string find_executable();

    // Check if yt-dlp supports --remote-components flag
    static bool supports_remote_components();

    // Build format specifier string based on preferred quality (4k, 1440p, 1080p, 720p, 360p)
    static std::string build_format_spec(std::string_view pref_quality);

    // Resolve YouTube watch URL to direct video + audio stream URLs (with multi-pass auto-healing)
    static ytdlp_stream_result resolve_stream_urls(
        const std::string& norm_url,
        std::string_view pref_quality = "360p"
    );

    // Search YouTube videos via yt-dlp JSON API
    static std::vector<ytdlp_search_result> search(
        const std::string& query,
        int max_results = 15
    );

    // Fetch subtitles/transcript SRT file via yt-dlp
    static std::filesystem::path fetch_subtitles(
        const std::string& video_url,
        const std::string& out_prefix,
        std::string_view custom_cookie_args = ""
    );
};

} // namespace rouen::helpers
