#include "rss_item.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <format>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

#include "../../helpers/debug.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/platform_utils.hpp"
#include "media_player.hpp"
#include "registrar.hpp"
#include "rss.hpp"
#include "sdl_compat.hpp"
#include "texture_utils.hpp"

namespace rouen::cards {

namespace {

[[nodiscard]] bool is_video_link(std::string_view url) noexcept {
    return url.find("youtube.com") != std::string_view::npos ||
           url.find("youtu.be") != std::string_view::npos ||
           url.find("vimeo.com") != std::string_view::npos;
}

[[nodiscard]] std::string strip_html_and_entities(std::string_view html) {
    std::string plain_text(html);

    // Remove HTML tags
    size_t tag_start = 0;
    while ((tag_start = plain_text.find('<', tag_start)) != std::string::npos) {
        const size_t tag_end = plain_text.find('>', tag_start);
        if (tag_end != std::string::npos) {
            plain_text.erase(tag_start, tag_end - tag_start + 1);
        } else {
            break;
        }
    }

    // Replace common HTML entities
    constexpr std::pair<std::string_view, std::string_view> kEntities[] = {
        {"&nbsp;", " "},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&amp;", "&"},
        {"&quot;", "\""}
    };

    for (const auto& [from, to] : kEntities) {
        size_t pos = 0;
        while ((pos = plain_text.find(from, pos)) != std::string::npos) {
            plain_text.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    return plain_text;
}

} // anonymous namespace

rss_item::rss_item(const std::string& item_info) {
    // Set custom colors
    colors[0] = {0.3f, 0.7f, 0.5f, 1.0f}; // Green primary color
    colors[1] = {0.4f, 0.8f, 0.6f, 0.7f}; // Lighter green secondary color

    get_color(2, ImVec4(0.6f, 1.0f, 0.8f, 1.0f)); // Light green for titles
    get_color(3, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray for descriptions
    get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green for playing status
    get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for stop/error status

    // Check for play hint suffix
    std::string clean_info = item_info;
    const size_t play_pos = clean_info.rfind("|||play");
    if (play_pos != std::string::npos && play_pos + 7 == clean_info.length()) {
        auto_play = true;
        clean_info = clean_info.substr(0, play_pos);
    } else {
        const size_t comma_play_pos = clean_info.rfind(",play");
        if (comma_play_pos != std::string::npos && comma_play_pos + 5 == clean_info.length()) {
            auto_play = true;
            clean_info = clean_info.substr(0, comma_play_pos);
        }
    }

    // Parse the feed_id,link,title info (separated by ||| or fallback to comma)
    const size_t first_delim = clean_info.find("|||");
    const size_t second_delim = (first_delim != std::string::npos) ? clean_info.find("|||", first_delim + 3) : std::string::npos;

    if (first_delim != std::string::npos && second_delim != std::string::npos) {
        try {
            feed_id = std::stoll(clean_info.substr(0, first_delim));
            item_link = clean_info.substr(first_delim + 3, second_delim - (first_delim + 3));
            item_title = clean_info.substr(second_delim + 3);

            rss_host = rss::getHost();
            loadItem();
        } catch (...) {
            name("RSS Item");
        }
    } else {
        // Fallback to comma parsing for compatibility with legacy URLs
        const size_t comma_pos = clean_info.find(',');
        if (comma_pos != std::string::npos) {
            try {
                feed_id = std::stoll(clean_info.substr(0, comma_pos));
                item_link = clean_info.substr(comma_pos + 1);
                rss_host = rss::getHost();
                loadItem();
            } catch (...) {
                name("RSS Item");
            }
        } else {
            name("RSS Item");
        }
    }

    // Initialize the image cache with paths in user data directory
    const auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
    const auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();

    constexpr int kImageCacheExpireDays = 30;
    image_cache = std::make_shared<::helpers::ImageCache>(
        db_path,
        cache_dir,
        kImageCacheExpireDays
    );

    // Adjust size to be larger for content display
    width *= 2.0f;

    // Set refresh rate to check media playback status
    requested_fps = 1;
}

rss_item::~rss_item() {
    rss_item::on_close();
    clear_item_textures();
}

void rss_item::on_close() {
    media_player::stopForOwner(this);
    rouen::platform::stop_speech();
    if (!item.enclosure.empty()) {
        media.stopMedia();
    }
}

void rss_item::clear_item_textures() {
    for (auto& [url, lt] : item_textures) {
        if (lt.texture) {
            SDL_DestroyTexture(lt.texture);
        }
    }
    item_textures.clear();
}

void rss_item::set_renderer(SDL_Renderer* r) noexcept {
    renderer = r;
}

void rss_item::calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1) noexcept {
    if (tex_w <= 0.0f || tex_h <= 0.0f || target_w <= 0.0f || target_h <= 0.0f) {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
        return;
    }
    const float target_aspect = target_w / target_h;
    const float tex_aspect = tex_w / tex_h;

    if (tex_aspect > target_aspect) {
        const float f = target_aspect / tex_aspect;
        const float c = (1.0f - f) * 0.5f;
        uv0 = ImVec2(c, 0.0f);
        uv1 = ImVec2(1.0f - c, 1.0f);
    } else {
        const float f = tex_aspect / target_aspect;
        const float c = (1.0f - f) * 0.5f;
        uv0 = ImVec2(0.0f, c);
        uv1 = ImVec2(1.0f, 1.0f - c);
    }
}

void rss_item::request_image_download(const std::string& url) {
    static std::unordered_set<std::string> downloading_urls;
    static std::mutex downloading_mutex;

    {
        std::lock_guard<std::mutex> const lock(downloading_mutex);
        if (downloading_urls.contains(url)) {
            return; // Already downloading
        }
        downloading_urls.insert(url);
    }

    auto cache = image_cache;
    std::thread([cache, url]() {
        try {
            cache->downloadAndCache(url);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // Ignore download failures, fallback image will be shown
        }

        {
            std::lock_guard<std::mutex> const lock(downloading_mutex);
            downloading_urls.erase(url);
        }
    }).detach();
}

void rss_item::loadItem() {
    if (feed_id < 0 || item_link.empty() || !rss_host) return;

    auto found_item = rss_host->get_feed_item(feed_id, item_link, item_title);
    if (!found_item) return;

    item = *found_item;
    name(std::format("{} - Article", item.title));

    if (!item.enclosure.empty()) {
        media.url = item.enclosure;
    }

    item_loaded = true;
}

std::string rss_item::get_playable_media_url() const {
    if (!item.enclosure.empty()) {
        return item.enclosure;
    }
    if (!item.extracted_media_urls.empty()) {
        return item.extracted_media_urls[0].url;
    }
    if (is_video_link(item.link)) {
        return item.link;
    }
    return "";
}

bool rss_item::render() {
    try {
        return render_window([this]() {
            try {
                if (!item_loaded) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load item");
                    return;
                }

                // Auto-play trigger on first render frame
                const std::string playable_url = get_playable_media_url();
                if (auto_play && !playable_url.empty()) {
                    auto_play = false; // Only trigger once

                    auto& global_item = media_player::get_item(playable_url);

                    global_item.url = playable_url;
                    global_item.feed_id = feed_id;
                    global_item.item_link = item_link;
                    global_item.item_title = item_title;
                    global_item.watermark = item.watermark;

                    media_player::stopAll();
                    global_item.start_offset = global_item.watermark.value_or(0.0);
                    global_item.playMedia(this);
                }

                // Original URL link
                ImGui::TextColored(colors[1], "Source: ");
                ImGui::SameLine();
                if (ImGui::SmallButton("Open in Browser")) {
                    rouen::platform::open_url(item_link);
                }
                if (feed_id >= 0) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Open Feed")) {
                        "create_card"_sfn(std::format("rss-feed:{}", feed_id));
                    }
                }

                // Date and time
                const auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                const std::tm* tm = std::localtime(&time); // NOLINT(concurrency-mt-unsafe)
                char date_str[64]{};
                if (tm) {
                    (void)std::strftime(date_str, sizeof(date_str), "%A, %d %B %Y %H:%M", tm);
                }
                ImGui::TextColored(colors[1], "Published: %s", date_str);

                // Media enclosure playback controls
                if (!item.enclosure.empty()) {
                    ImGui::Separator();

                    try {
                        media_player::player(item.enclosure, colors[0], "Play Audio", item.feed_id, item.link, item.title, item.watermark, true, 0.0f, this);
                    } catch (const std::exception& e) {
                        RSS_ERROR_FMT("Exception in media player: {}", e.what());
                    }

                    ImGui::Separator();
                }
                // Enhanced: Check for extracted media URLs if no direct enclosure
                else if (!item.extracted_media_urls.empty()) {
                    ImGui::Separator();

                    ImGui::TextColored(colors[0], "Media Content:");

                    const size_t num_media = item.extracted_media_urls.size();
                    for (size_t i = 0; i < num_media; ++i) {
                        const auto& extracted_media = item.extracted_media_urls[i];
                        std::string media_type_label = "Media";
                        if (extracted_media.type == "video") {
                            media_type_label = "Video";
                        } else if (extracted_media.type == "audio") {
                            media_type_label = "Audio";
                        }

                        const std::string media_title = std::format("Play {} ({})",
                            media_type_label,
                            extracted_media.format);

                        try {
                            media_player::player(extracted_media.url, colors[0], media_title, item.feed_id, item.link, item.title, item.watermark, true, 0.0f, this);
                            if (i < num_media - 1) {
                                ImGui::Spacing();
                            }
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Exception in extracted media player: {}", e.what());
                        }
                    }

                    ImGui::Separator();
                }
                // Enhanced: Check if this is a YouTube/Vimeo link without enclosure
                else if (is_video_link(item.link)) {
                    ImGui::Separator();

                    try {
                        media_player::player(item.link, colors[0], "Play Video", item.feed_id, item.link, item.title, item.watermark, true, 0.0f, this);
                    } catch (const std::exception& e) {
                        RSS_ERROR_FMT("Exception in video link player: {}", e.what());
                    }

                    ImGui::Separator();
                }

                // Content in a scrollable area
                try {
                    const bool is_visible = ImGui::BeginChild("ContentScrollArea", ImVec2(0, 0), true, ImGuiWindowFlags_NavFlattened);

                    if (is_visible) {
                        const bool is_playing_media = !item.enclosure.empty() ||
                                                    !item.extracted_media_urls.empty() ||
                                                    is_video_link(item.link);

                        if (!is_playing_media) {
                            SDL_Texture* item_tex = nullptr;
                            int item_tex_w = 0;
                            int item_tex_h = 0;
                            if (renderer && image_cache && !item.image_url.empty()) {
                                const auto it = item_textures.find(item.image_url);
                                if (it != item_textures.end()) {
                                    item_tex = it->second.texture;
                                    item_tex_w = it->second.width;
                                    item_tex_h = it->second.height;
                                } else {
                                    int cached_w = 0;
                                    int cached_h = 0;
                                    if (image_cache->isCached(item.image_url, cached_w, cached_h)) {
                                        item_tex = image_cache->getTexture(renderer, item.image_url, item_tex_w, item_tex_h);
                                        if (item_tex) {
                                            item_textures[item.image_url] = {item_tex, item_tex_w, item_tex_h};
                                        }
                                    } else {
                                        request_image_download(item.image_url);
                                    }
                                }
                            }

                            if (item_tex) {
                                const float avail_w = ImGui::GetContentRegionAvail().x;
                                const ImVec2 banner_size(avail_w, 240.0f);
                                const ImVec2 banner_pos = ImGui::GetCursorScreenPos();

                                ImVec2 uv0, uv1;
                                calculate_cover_uvs(banner_size.x, banner_size.y, static_cast<float>(item_tex_w), static_cast<float>(item_tex_h), uv0, uv1);

                                ImGui::GetWindowDrawList()->AddImage(
                                    rouen::helpers::texture_id_cast(item_tex),
                                    banner_pos,
                                    ImVec2(banner_pos.x + banner_size.x, banner_pos.y + banner_size.y),
                                    uv0,
                                    uv1
                                );
                                ImGui::Dummy(banner_size);
                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Spacing();
                            }
                        }

                        // Use description as content
                        if (!item.description.empty()) {
                            try {
                                const std::string plainText = strip_html_and_entities(item.description);
                                ImGui::TextWrapped("%s", plainText.c_str());
                            } catch (const std::exception& e) {
                                RSS_ERROR_FMT("Exception in content processing: {}", e.what());
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Error processing content");
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No content available");
                        }
                    }

                    ImGui::EndChild();
                } catch (const std::exception& e) {
                    RSS_ERROR_FMT("Exception in content scroll area: {}", e.what());
                    ImGui::EndChild();
                }
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Exception in RSS item rendering: {}", e.what());
            }
        });
    } catch (const std::exception& e) {
        RSS_ERROR_FMT("Exception in RSS item card: {}", e.what());
        return false;
    }
}

std::string rss_item::get_uri() const {
    return std::format("rss-item:{}|||{}|||{}", feed_id, item_link, item_title);
}

} // namespace rouen::cards
