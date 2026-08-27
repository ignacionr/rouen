#pragma once

#include <string>
#include <string_view>

namespace rouen::hosts {

std::string trim_copy(std::string value);
std::string resolve_relative_url(std::string_view href, std::string_view base_url);
std::string resolve_youtube_url(const std::string& input_url);
std::string resolve_nyt_podcast_url(const std::string& input_url);
std::string extract_rss_url_from_html(std::string_view html, std::string_view base_url);
std::string resolve_feed_url(const std::string& input_url);

} // namespace rouen::hosts
