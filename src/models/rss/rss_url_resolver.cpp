#include "rss_url_resolver.hpp"

#include <cstddef>
#include <exception>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "../../helpers/fetch.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::hosts {

std::string trim_copy(std::string value) {
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    if (!value.empty()) {
        auto end_pos = value.find_last_not_of(" \t\r\n");
        if (end_pos != std::string::npos) {
            value.erase(end_pos + 1);
        }
    }
    return value;
}

namespace {

std::string extract_youtube_channel_id(const std::string& html) {
    std::smatch match;
    
    // 1. Try to find the RSS feed link directly
    std::regex const r1(R"raw(youtube\.com/feeds/videos\.xml\?channel_id=(UC[A-Za-z0-9_-]{22}))raw");
    if (std::regex_search(html, match, r1) && match.size() > 1) {
        return match.str(1);
    }
    
    // 2. Try to find channel_id in meta tag / page content
    std::regex const r2(R"raw(("channelId"|"externalId")\s*:\s*"(UC[A-Za-z0-9_-]{22})")raw");
    if (std::regex_search(html, match, r2) && match.size() > 2) {
        return match.str(2);
    }
    
    // 3. Alternate meta content format
    std::regex const r3(R"raw(<meta\s+itemprop="channelId"\s+content="(UC[A-Za-z0-9_-]{22})">)raw");
    if (std::regex_search(html, match, r3) && match.size() > 1) {
        return match.str(1);
    }

    return "";
}

} // namespace

std::string resolve_youtube_url(const std::string& input_url) {
    std::string const url = trim_copy(input_url);
    if (url.empty()) return url;
    
    if (url.find("youtube.com/feeds/videos.xml") != std::string::npos) {
        return url;
    }
    
    bool const is_youtube = (url.find("youtube.com") != std::string::npos || 
                       url.find("youtu.be") != std::string::npos);
    if (!is_youtube) {
        return input_url;
    }
    
    std::regex const channel_url_regex(R"raw(youtube\.com/channel/(UC[A-Za-z0-9_-]{22}))raw");
    std::smatch match;
    if (std::regex_search(url, match, channel_url_regex) && match.size() > 1) {
        return "https://www.youtube.com/feeds/videos.xml?channel_id=" + match.str(1);
    }
    
    try {
        std::string fetch_url = url;
        if (!fetch_url.starts_with("http://") && !fetch_url.starts_with("https://")) {
            fetch_url = "https://" + fetch_url;
        }
        
        http::fetch client{10};
        std::vector<std::string> const headers = {
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
        };
        
        std::string const html = client(fetch_url, headers);
        std::string const channel_id = extract_youtube_channel_id(html);
        if (!channel_id.empty()) {
            return "https://www.youtube.com/feeds/videos.xml?channel_id=" + channel_id;
        }
    } catch (const std::exception& e) {
        HTTP_WARN_FMT("Failed to resolve YouTube channel URL {}: {}", url, e.what());
    }
    
    return input_url;
}

std::string resolve_nyt_podcast_url(const std::string& input_url) {
    std::string const url = trim_copy(input_url);
    if (url.empty()) return url;

    std::string const lower = ::helpers::StringHelper::to_lower(url);
    if (lower.find("nytimes.com") == std::string::npos) {
        return input_url;
    }

    struct NytPodcastMap {
        std::string slug;
        std::string feed_url;
    };

    static const std::vector<NytPodcastMap> mappings = {
        {"modern-love-podcast", "https://feeds.simplecast.com/0N8Hs1MH"},
        {"modern-love", "https://feeds.simplecast.com/0N8Hs1MH"},
        {"the-daily", "https://feeds.simplecast.com/54nAGcIl"},
        {"serial", "https://feeds.simplecast.com/w1704nrm"},
        {"hard-fork", "https://feeds.simplecast.com/l2i9YnTd"},
        {"ezra-klein-podcast", "https://feeds.simplecast.com/82FI35Px"},
        {"ezra-klein-show", "https://feeds.simplecast.com/82FI35Px"},
        {"ezra-klein", "https://feeds.simplecast.com/82FI35Px"},
        {"matter-of-opinion", "https://feeds.simplecast.com/39l2Lz_d"},
        {"the-interview", "https://feeds.simplecast.com/ksGYZ_Z3"},
        {"first-person", "https://feeds.simplecast.com/7650O5jY"}
    };

    for (const auto& m : mappings) {
        if (lower.find(m.slug) != std::string::npos) {
            return m.feed_url;
        }
    }

    return input_url;
}

std::string resolve_relative_url(std::string_view href, std::string_view base_url) {
    std::string h = trim_copy(std::string(href));
    if (h.empty()) return "";
    if (h.starts_with("http://") || h.starts_with("https://")) {
        return h;
    }

    std::string base = trim_copy(std::string(base_url));
    if (!base.starts_with("http://") && !base.starts_with("https://")) {
        base = "https://" + base;
    }

    size_t const scheme_end = base.find("://");
    std::string const scheme = (scheme_end != std::string::npos) ? base.substr(0, scheme_end) : "https";

    if (h.starts_with("//")) {
        return scheme + ":" + h;
    }

    size_t const host_start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
    size_t const path_start = base.find('/', host_start);
    std::string const origin = (path_start != std::string::npos) ? base.substr(0, path_start) : base;

    if (h[0] == '/') {
        return origin + h;
    }

    if (path_start == std::string::npos) {
        return origin + "/" + h;
    }

    size_t const last_slash = base.find_last_of('/');
    if (last_slash != std::string::npos && last_slash >= host_start) {
        return base.substr(0, last_slash + 1) + h;
    }

    return origin + "/" + h;
}

std::string extract_rss_url_from_html(std::string_view html, std::string_view base_url) {
    std::string html_str(html);

    std::regex const link_regex(R"raw(<link\s+[^>]*rel=["']?alternate["']?[^>]*>)raw", std::regex::icase);
    auto words_begin = std::sregex_iterator(html_str.begin(), html_str.end(), link_regex);
    auto words_end = std::sregex_iterator();

    std::string main_feed_url;
    std::string fallback_feed_url;

    auto get_attr = [](const std::string& tag, const std::string& attr_name) -> std::string {
        std::regex const attr_regex(attr_name + R"raw(\s*=\s*["']([^"']+)["'])raw", std::regex::icase);
        std::smatch match;
        if (std::regex_search(tag, match, attr_regex) && match.size() > 1) {
            return match.str(1);
        }
        std::regex const attr_unquoted(attr_name + R"raw(\s*=\s*([^\s>]+))raw", std::regex::icase);
        if (std::regex_search(tag, match, attr_unquoted) && match.size() > 1) {
            return match.str(1);
        }
        return "";
    };

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch const match = *i;
        std::string const tag = match.str();

        std::string const type = ::helpers::StringHelper::to_lower(get_attr(tag, "type"));
        std::string const href = get_attr(tag, "href");
        std::string const title = ::helpers::StringHelper::to_lower(get_attr(tag, "title"));

        if (href.empty()) continue;

        bool const is_rss_type = (type.find("application/rss+xml") != std::string::npos ||
                            type.find("application/atom+xml") != std::string::npos ||
                            type.find("application/rdf+xml") != std::string::npos ||
                            type.find("application/feed+json") != std::string::npos ||
                            type.find("text/xml") != std::string::npos);

        if (!is_rss_type) continue;

        bool const is_comments = (title.find("comment") != std::string::npos ||
                            href.find("comments/feed") != std::string::npos ||
                            href.find("comments") != std::string::npos);

        if (!is_comments && main_feed_url.empty()) {
            main_feed_url = href;
        } else if (fallback_feed_url.empty()) {
            fallback_feed_url = href;
        }
    }

    std::string target_href = !main_feed_url.empty() ? main_feed_url : fallback_feed_url;

    if (target_href.empty()) {
        std::regex const a_regex(R"raw(<a\s+[^>]*href=["']([^"']+)["'][^>]*>(?:[^<]*RSS[^<]*)?</a>)raw", std::regex::icase);
        auto a_begin = std::sregex_iterator(html_str.begin(), html_str.end(), a_regex);
        for (std::sregex_iterator i = a_begin; i != words_end; ++i) {
            std::smatch const match = *i;
            std::string const href = match.str(1);
            std::string const lower_href = ::helpers::StringHelper::to_lower(href);
            if (lower_href.find("feed") != std::string::npos || lower_href.find("rss") != std::string::npos) {
                if (lower_href.find("comments") == std::string::npos) {
                    target_href = href;
                    break;
                }
            }
        }
    }

    if (target_href.empty()) return "";

    return resolve_relative_url(target_href, base_url);
}

std::string resolve_feed_url(const std::string& input_url) {
    std::string url = trim_copy(input_url);
    if (url.empty()) return url;

    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = "https://" + url;
    }

    std::string yt_resolved = resolve_youtube_url(url);
    if (yt_resolved != url && yt_resolved.find("youtube.com/feeds/videos.xml") != std::string::npos) {
        return yt_resolved;
    }

    std::string nyt_resolved = resolve_nyt_podcast_url(url);
    if (nyt_resolved != url) {
        RSS_INFO_FMT("Resolved NYT podcast collection feed URL {} -> {}", url, nyt_resolved);
        return nyt_resolved;
    }

    try {
        http::fetch client{10};
        std::vector<std::string> const headers = {
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,application/rss+xml;q=0.9,*/*;q=0.8"
        };

        std::string const content = client(url, headers);
        std::string effective_url = client.last_effective_url();
        if (effective_url.empty()) effective_url = url;

        bool const is_xml = (content.find("<?xml") != std::string::npos ||
                       content.find("<rss") != std::string::npos ||
                       content.find("<feed") != std::string::npos ||
                       content.find("<rdf:RDF") != std::string::npos);

        if (is_xml) {
            return effective_url;
        }

        std::string discovered_rss = extract_rss_url_from_html(content, effective_url);
        if (!discovered_rss.empty()) {
            RSS_INFO_FMT("Autodiscovered RSS feed link for webpage {}: {}", url, discovered_rss);
            return discovered_rss;
        }
    } catch (const std::exception& e) {
        HTTP_WARN_FMT("Failed to fetch/resolve feed URL for {}: {}", url, e.what());
    }

    return url;
}

} // namespace rouen::hosts
