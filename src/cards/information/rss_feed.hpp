#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <thread>
#include <set>
#include <mutex>
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../external/IconsMaterialDesign.h" // Added icon header

#include "../interface/card.hpp"
#include "rss.hpp"
#include "../../hosts/rss_host.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/rss/rss_date_parser.hpp"

namespace rouen::cards
{

    // Card to display items from a specific feed
    class rss_feed : public card
    {
    public:
        rss_feed(const std::string &feed_id_str)
        {
            // Set custom colors for the feed card
            colors[0] = {0.3f, 0.5f, 0.8f, 1.0f}; // Blue primary color
            colors[1] = {0.4f, 0.6f, 0.9f, 0.7f}; // Lighter blue secondary color

            // Additional colors
            get_color(2, ImVec4(0.6f, 0.8f, 1.0f, 1.0f)); // Light blue for titles
            get_color(3, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray for descriptions
            get_color(4, ImVec4(0.6f, 0.9f, 0.6f, 1.0f)); // Light green for links

            // Parse the feed ID
            try
            {
                feed_id = std::stoll(feed_id_str);
            }
            catch (...)
            {
                feed_id = -1;
            }

            // Initialize with a temporary name
            name("Feed Items");

            // Get the RSS host controller
            rss_host = rss::getHost();

            // Initialize the image cache with paths in user data directory
            auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
            auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();
            
            image_cache = std::make_shared<::helpers::ImageCache>(
                db_path,          // SQLite database for image cache in user's data directory
                cache_dir,        // Cache directory for image files in user's data directory
                30                // Expire images after 30 days
            );

            // Load feed information and items
            loadFeed();
            width *= 1.5f; // Adjust width for better display
        }

        ~rss_feed() override
        {
            if (feed_image_texture)
            {
                SDL_DestroyTexture(feed_image_texture);
                feed_image_texture = nullptr;
            }
            clear_item_textures();
        }

        void clear_item_textures() {
            for (auto& [url, lt] : item_textures) {
                if (lt.texture) {
                    SDL_DestroyTexture(lt.texture);
                }
            }
            item_textures.clear();
        }

        void calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1) {
            if (tex_w <= 0.0f || tex_h <= 0.0f || target_w <= 0.0f || target_h <= 0.0f) {
                uv0 = ImVec2(0.0f, 0.0f);
                uv1 = ImVec2(1.0f, 1.0f);
                return;
            }
            float target_aspect = target_w / target_h;
            float tex_aspect = tex_w / tex_h;

            if (tex_aspect > target_aspect) {
                float f = target_aspect / tex_aspect;
                float c = (1.0f - f) * 0.5f;
                uv0 = ImVec2(c, 0.0f);
                uv1 = ImVec2(1.0f - c, 1.0f);
            } else {
                float f = tex_aspect / target_aspect;
                float c = (1.0f - f) * 0.5f;
                uv0 = ImVec2(0.0f, c);
                uv1 = ImVec2(1.0f, 1.0f - c);
            }
        }

        static std::string item_texture_cache_key(const std::string& url, ::helpers::ImageCache::Variant variant) {
            return variant == ::helpers::ImageCache::Variant::Grayscale ? url + "#grayscale" : url;
        }

        SDL_Texture* get_item_texture(const std::string& url, ::helpers::ImageCache::Variant variant, int& texture_width, int& texture_height) {
            texture_width = 0;
            texture_height = 0;

            if (!renderer || !image_cache || url.empty()) {
                return nullptr;
            }

            const auto cache_key = item_texture_cache_key(url, variant);
            if (item_textures.contains(cache_key)) {
                auto& loaded = item_textures[cache_key];
                texture_width = loaded.width;
                texture_height = loaded.height;
                return loaded.texture;
            }

            SDL_Texture* texture = image_cache->getTexture(renderer, url, texture_width, texture_height, false, variant);
            if (texture) {
                item_textures[cache_key] = {texture, texture_width, texture_height};
            }
            return texture;
        }

        void loadFeed()
        {
            if (feed_id < 0 || !rss_host)
                return;

            // Get feed information
            auto feed_info = rss_host->getFeedInfo(feed_id);
            if (!feed_info)
                return;

            // Update feed details
            feed_title = feed_info->title;
            feed_url = feed_info->url;
            feed_image_url = feed_info->image_url;

            // Update the card title with the feed title
            name(std::format("{} - Feed", feed_title));

            items_limit = 20; // Reset display limit

            // Load feed items asynchronously to prevent UI thread blocking
            std::jthread([this, f_id = feed_id]() {
                try {
                    auto loaded_items = rss_host->getFeedItems(f_id, 100); // Limit to 100 items by default
                    std::lock_guard<std::mutex> lock(items_mutex_);
                    pending_items_ = std::move(loaded_items);
                    items_loaded_ = true;
                } catch (...) {}
            }).detach();

            // Load the feed image if available
            loadFeedImage();
        }

        void loadFeedImage()
        {
            if (feed_image_texture)
            {
                SDL_DestroyTexture(feed_image_texture);
                feed_image_texture = nullptr;
                feed_image_width = 0;
                feed_image_height = 0;
            }

            // If there's no image URL or we don't have a renderer, we're done
            if (feed_image_url.empty() || !renderer)
            {
                return;
            }

            int cached_w = 0, cached_h = 0;
            if (image_cache->isCached(feed_image_url, cached_w, cached_h))
            {
                // Use the image cache to load the image (fast from cache)
                feed_image_texture = image_cache->getTexture(
                    renderer,
                    feed_image_url,
                    feed_image_width,
                    feed_image_height);
            }
            else
            {
                // Download in background to prevent startup freezes
                auto cache = image_cache;
                auto url = feed_image_url;
                std::jthread([this, cache, url]() {
                    try {
                        if (cache->downloadAndCache(url)) {
                            feed_image_downloaded_ = true;
                        }
                    } catch (...) {}
                }).detach();
            }
        }

        void set_renderer(SDL_Renderer *r)
        {
            renderer = r;

            // If we have a renderer and feed image URL, load the image
            if (renderer && !feed_image_url.empty())
            {
                loadFeedImage();
            }
        }

        bool render() override
        {
            if (feed_image_downloaded_.load())
            {
                feed_image_downloaded_ = false;
                loadFeedImage();
            }

            if (items_loaded_.load())
            {
                items_loaded_ = false;
                std::lock_guard<std::mutex> lock(items_mutex_);
                items = std::move(pending_items_);
            }
            
            try
            {
                return render_window([this]()
                                     {
                try {
                    // Search and refresh section
                    ImGui::BeginGroup();
                    
                    float available_width = ImGui::GetContentRegionAvail().x;
                    float refresh_button_width = 30.0f;
                    float clear_button_width = 20.0f;
                    float search_input_width = available_width - refresh_button_width - clear_button_width - ImGui::GetStyle().ItemSpacing.x * 2;
                    
                    ImGui::PushItemWidth(search_input_width);
                    if (ImGui::InputText("##item_search", search_buffer, sizeof(search_buffer))) {
                        items_limit = 20; // Reset limit when search query changes
                    }
                    
                    // Search placeholder
                    if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                        auto pos = ImGui::GetItemRectMin();
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2(pos.x + 5, pos.y + 2),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            "Search items..."
                        );
                    }
                    ImGui::PopItemWidth();
                    
                    // Clear button
                    ImGui::SameLine();
                    if (ImGui::SmallButton("×")) {
                        search_buffer[0] = '\0';
                        items_limit = 20; // Reset limit
                    }
                    
                    // Refresh button
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x, colors[0].y, colors[0].z, 1.0f));
                    if (ImGui::Button(ICON_MD_REFRESH)) {
                        refreshFeed();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Refresh feed");
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::EndGroup();
                    
                    // Display the feed image (or placeholder) and tags to the side of it
                    ImGui::BeginGroup();
                    
                    float img_display_w = 100.0f;
                    float img_display_h = 100.0f;
                    ImVec2 cur_pos = ImGui::GetCursorScreenPos();
                    
                    if (feed_image_texture && feed_image_width > 0 && feed_image_height > 0) {
                        float aspect_ratio = static_cast<float>(feed_image_width) / static_cast<float>(feed_image_height);
                        float display_width = img_display_h * aspect_ratio;
                        if (display_width > 120.0f) display_width = 120.0f;
                        img_display_w = display_width;
                        
                        ImGui::Image(
                            rouen::helpers::texture_id_cast(feed_image_texture),
                            ImVec2(img_display_w, img_display_h)
                        );
                    } else {
                        // Draw a placeholder cover box
                        ImGui::GetWindowDrawList()->AddRectFilled(cur_pos, ImVec2(cur_pos.x + img_display_w, cur_pos.y + img_display_h), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.5f)), 4.0f);
                        std::string placeholder_icon = ICON_MD_RSS_FEED;
                        ImVec2 icon_size = ImGui::CalcTextSize(placeholder_icon.c_str());
                        ImVec2 icon_pos = ImVec2(cur_pos.x + (img_display_w - icon_size.x) * 0.5f, cur_pos.y + (img_display_h - icon_size.y) * 0.5f);
                        ImGui::GetWindowDrawList()->AddText(icon_pos, ImGui::GetColorU32(colors[1]), placeholder_icon.c_str());
                        ImGui::Dummy(ImVec2(img_display_w, img_display_h));
                    }
                    
                    ImGui::SameLine(img_display_w + 16.0f);
                    
                    // Right Side: Language and Tags editor group
                    ImGui::BeginGroup();
                    
                    // 1. Language Selector
                    ImGui::TextColored(colors[2], "Language:");
                    ImGui::SameLine();
                    
                    std::vector<std::pair<std::string, std::string>> languages = {
                        {"", "Auto Detect"},
                        {"en", "English"},
                        {"es", "Spanish"},
                        {"fr", "French"},
                        {"de", "German"},
                        {"it", "Italian"}
                    };
                    
                    std::string current_lang = rss_host->getFeedLanguage(feed_id);
                    std::string current_display = "Auto Detect";
                    for (const auto& [code, label] : languages) {
                        if (code == current_lang) {
                            current_display = label;
                            break;
                        }
                    }
                    
                    ImGui::SetNextItemWidth(130.0f);
                    if (ImGui::BeginCombo("##feed_lang", current_display.c_str())) {
                        for (const auto& [code, label] : languages) {
                            bool is_selected = (code == current_lang);
                            if (ImGui::Selectable(label.c_str(), is_selected)) {
                                rss_host->setFeedLanguage(feed_id, code);
                            }
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    
                    ImGui::Spacing();
                    
                    // 2. Tags Selector (with wrapping layout)
                    ImGui::TextColored(colors[2], "Tags:");
                    
                    auto current_tags = rss_host->getFeedTags(feed_id);
                    std::vector<std::string> all_possible_tags = {"News", "Tech / Dev", "Podcasts", "YouTube", "Other"};
                    
                    float window_max_x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                    
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
                    for (size_t t_idx = 0; t_idx < all_possible_tags.size(); ++t_idx) {
                        const auto& tag_name = all_possible_tags[t_idx];
                        bool has_tag = current_tags.contains(tag_name);
                        
                        // Estimate checkbox width: checkbox box frame (GetFrameHeight) + text + paddings
                        float chk_width = ImGui::CalcTextSize(tag_name.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
                        
                        float current_x = ImGui::GetCursorScreenPos().x;
                        if (t_idx > 0 && current_x + chk_width > window_max_x - 20.0f) {
                            // Wrapping to next line!
                        } else if (t_idx > 0) {
                            ImGui::SameLine(0.0f, 12.0f);
                        }
                        
                        std::string chk_id = std::format("{}##tag_chk_{}", tag_name, tag_name);
                        if (ImGui::Checkbox(chk_id.c_str(), &has_tag)) {
                            if (has_tag) {
                                rss_host->addFeedTag(feed_id, tag_name);
                            } else {
                                rss_host->removeFeedTag(feed_id, tag_name);
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndGroup();
                    
                    ImGui::EndGroup();
                                
                    ImGui::Separator();
                    
                    // Filter items
                    std::vector<rouen::hosts::RSSHost::FeedItem*> filtered_items;
                    std::string search_text = search_buffer;
                    if (search_text.empty()) {
                        for (auto& item : items) {
                            filtered_items.push_back(&item);
                        }
                    } else {
                        for (auto& item : items) {
                            if (::helpers::StringHelper::contains_case_insensitive(item.title, search_text) ||
                                ::helpers::StringHelper::contains_case_insensitive(item.description, search_text)) {
                                filtered_items.push_back(&item);
                            }
                        }
                    }

                    if (!search_text.empty()) {
                        ImGui::TextColored(colors[1], "%zu items found", filtered_items.size());
                    }

                    // Items in a scrollable area
                    try {
                        if (filtered_items.empty()) {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No items found");
                        } else {
                            if (ImGui::BeginChild("FeedItemsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                                size_t count = std::min(static_cast<size_t>(items_limit), filtered_items.size());
                                for (size_t i = 0; i < count; ++i) {
                                    auto& item = *filtered_items[i];
                                    ImGui::PushID(item.link.c_str());
                                    const ImVec2 row_start = ImGui::GetCursorScreenPos();
                                    
                                    try {
                                        ImGui::BeginGroup();
                                        
                                        try {
                                            // Try to load item thumbnail
                                            SDL_Texture* item_tex = nullptr;
                                            int item_tex_w = 0, item_tex_h = 0;
                                            if (renderer && image_cache && !item.image_url.empty()) {
                                                item_tex = get_item_texture(item.image_url, ::helpers::ImageCache::Variant::Color, item_tex_w, item_tex_h);
                                                if (!item_tex) {
                                                    int cached_w = 0, cached_h = 0;
                                                    if (!image_cache->isCached(item.image_url, cached_w, cached_h)) {
                                                        request_image_download(item.image_url);
                                                    }
                                                }
                                            }

                                            bool has_item_image = (item_tex != nullptr);
                                            float avail_width = ImGui::GetContentRegionAvail().x;
                                            
                                            if (has_item_image) {
                                                ImGui::BeginGroup();
                                                ImGui::PushTextWrapPos(avail_width - 130.0f);
                                            } else {
                                                ImGui::PushTextWrapPos(avail_width);
                                            }
                                            
                                            // Title (selectable to open item)
                                            if (ImGui::Selectable(item.title.c_str(), false, 0, ImVec2(has_item_image ? avail_width - 130.0f : avail_width, 0))) {
                                                // Open item in a new card
                                                std::string item_uri = std::format("rss-item:{}|||{}|||{}", feed_id, item.link, item.title);
                                                "create_card"_sfn(item_uri);
                                            }
                                            
                                            // Date - format as "Day Month Year Hour:Minute (Age)"
                                            auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                                            std::tm* tm = std::localtime(&time);
                                            char date_str[64];
                                            std::strftime(date_str, sizeof(date_str), "%d %b %Y %H:%M", tm);
                                            std::string age_str = media::rss::format_rss_age(item.publish_date);
                                            
                                            ImGui::TextColored(colors[1], "%s (%s)", date_str, age_str.c_str());
                                            
                                            // Truncated description (if available)
                                            if (!item.description.empty()) {
                                                std::string desc = ::helpers::StringHelper::strip_html_tags(item.description);
                                                
                                                // Limit length
                                                if (desc.length() > 100) {
                                                    desc = desc.substr(0, 97) + "...";
                                                }
                                                
                                                ImGui::TextWrapped("%s", desc.c_str());
                                            }
 
                                            // if there's a playable enclosure, offer media controls
                                            if (!item.enclosure.empty()) {
                                                media_player::player(item.enclosure, colors[0], "Play Audio", item.feed_id, item.link, item.title, item.watermark);
                                            }
                                            // Enhanced: Check for extracted media URLs if no direct enclosure
                                            else if (!item.extracted_media_urls.empty()) {
                                                // Use the best available media URL
                                                std::string best_media_url = item.get_best_media_url();
                                                if (!best_media_url.empty()) {
                                                    // Determine appropriate title based on media type
                                                    std::string media_title = "Play Media";
                                                    for (const auto& extracted_media : item.extracted_media_urls) {
                                                        if (extracted_media.url == best_media_url) {
                                                            if (extracted_media.type == "video") {
                                                                media_title = "Play Video";
                                                            } else if (extracted_media.type == "audio") {
                                                                media_title = "Play Audio";
                                                            }
                                                            break;
                                                        }
                                                    }
                                                    media_player::player(best_media_url, colors[0], media_title, item.feed_id, item.link, item.title, item.watermark);
                                                }
                                            }
                                            // Enhanced: Check if this is a YouTube/Vimeo link without enclosure
                                            else if (item.link.find("youtube.com") != std::string::npos || 
                                                     item.link.find("youtu.be") != std::string::npos ||
                                                     item.link.find("vimeo.com") != std::string::npos) {
                                                media_player::player(item.link, colors[0], "Play Video", item.feed_id, item.link, item.title, item.watermark);
                                            }
                                            else {
                                                // Check if currently speaking
                                                bool is_this_speaking = false;
                                                {
                                                    std::lock_guard<std::mutex> lock(speaking_mutex);
                                                    is_this_speaking = (currently_speaking_link == item.link);
                                                }
                                                
                                                std::string btn_label = is_this_speaking ? std::format("{} Stop Reading", ICON_MD_VOLUME_OFF) : std::format("{} Read Article", ICON_MD_VOLUME_UP);
                                                
                                                if (is_this_speaking) {
                                                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x * 1.1f, colors[0].y * 1.1f, colors[0].z * 1.1f, colors[0].w));
                                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x * 0.9f, colors[0].y * 0.9f, colors[0].z * 0.9f, colors[0].w));
                                                } else {
                                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.15f, 0.6f));
                                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
                                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.18f, 0.9f));
                                                }
                                                
                                                if (ImGui::Button(btn_label.c_str())) {
                                                    if (is_this_speaking) {
                                                        rouen::platform::stop_speech();
                                                        std::lock_guard<std::mutex> lock(speaking_mutex);
                                                        currently_speaking_link = "";
                                                    } else {
                                                        rouen::platform::stop_speech();
                                                        {
                                                            std::lock_guard<std::mutex> lock(speaking_mutex);
                                                            currently_speaking_link = item.link;
                                                        }
                                                        std::string clean_desc = ::helpers::StringHelper::strip_html_tags(item.description);
                                                        std::string speech_text = item.title + ". " + clean_desc;
                                                        auto lang_voice = detect_language_and_select_voice(speech_text, item.link);
                                                        rouen::platform::speak_text_async(speech_text, lang_voice.second, lang_voice.first, [item_link = item.link]() {
                                                            std::lock_guard<std::mutex> lock(speaking_mutex);
                                                            if (currently_speaking_link == item_link) {
                                                                currently_speaking_link = "";
                                                            }
                                                        });
                                                    }
                                                }
                                                ImGui::PopStyleColor(3);
                                            }

                                            ImGui::PopTextWrapPos();

                                            // Draw thumbnail on the right
                                            if (has_item_image) {
                                                ImGui::EndGroup();
                                                ImGui::SameLine(avail_width - 120.0f);
                                                
                                                ImVec2 thumb_size(120.0f, 80.0f);
                                                ImVec2 thumb_pos = ImGui::GetCursorScreenPos();
                                                const float row_bottom = std::max(ImGui::GetCursorScreenPos().y, thumb_pos.y + thumb_size.y);
                                                const bool row_hovered = ImGui::IsMouseHoveringRect(
                                                    row_start,
                                                    ImVec2(row_start.x + avail_width, row_bottom),
                                                    false
                                                );

                                                SDL_Texture* display_tex = item_tex;
                                                int display_tex_w = item_tex_w;
                                                int display_tex_h = item_tex_h;
                                                if (!row_hovered) {
                                                    int grayscale_w = 0;
                                                    int grayscale_h = 0;
                                                    if (SDL_Texture* grayscale_tex = get_item_texture(item.image_url, ::helpers::ImageCache::Variant::Grayscale, grayscale_w, grayscale_h)) {
                                                        display_tex = grayscale_tex;
                                                        display_tex_w = grayscale_w;
                                                        display_tex_h = grayscale_h;
                                                    }
                                                }
                                                
                                                ImVec2 uv0, uv1;
                                                calculate_cover_uvs(thumb_size.x, thumb_size.y, static_cast<float>(display_tex_w), static_cast<float>(display_tex_h), uv0, uv1);
                                                
                                                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                                                draw_list->AddImage(
                                                    rouen::helpers::texture_id_cast(display_tex),
                                                    thumb_pos,
                                                    ImVec2(thumb_pos.x + thumb_size.x, thumb_pos.y + thumb_size.y),
                                                    uv0,
                                                    uv1
                                                );
                                                
                                                // Make thumbnail clickable
                                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.05f));
                                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.1f));
                                                if (ImGui::Button(std::format("##thumb_btn_{}", i).c_str(), thumb_size)) {
                                                    std::string item_uri = std::format("rss-item:{}|||{}|||{}", feed_id, item.link, item.title);
                                                    "create_card"_sfn(item_uri);
                                                }
                                                ImGui::PopStyleColor(3);
                                            }
                                        }
                                        catch (const std::exception& e) {
                                            RSS_ERROR_FMT("Exception in RSS feed item rendering: {}", e.what());
                                        }
                                        
                                        // Always end the group - regardless of exceptions
                                        ImGui::EndGroup();
                                    }
                                    catch (const std::exception& e) {
                                        RSS_ERROR_FMT("Exception in RSS feed group: {}", e.what());
                                        ImGui::EndGroup();
                                    }
                                    
                                    ImGui::Separator();
                                    
                                    // Always pop the ID
                                    ImGui::PopID();
                                }
                                
                                // Scroll check for lazy loading
                                float scroll_y = ImGui::GetScrollY();
                                float max_scroll_y = ImGui::GetScrollMaxY();
                                if (max_scroll_y > 0.0f && scroll_y >= max_scroll_y - 50.0f) {
                                    if (items_limit < static_cast<int>(filtered_items.size())) {
                                        items_limit = std::min(items_limit + 20, static_cast<int>(filtered_items.size()));
                                    }
                                }
                                
                                ImGui::EndChild();
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        RSS_ERROR_FMT("Exception in RSS feed items area: {}", e.what());
                    }
                }
                catch (const std::exception& e) {
                    RSS_ERROR_FMT("Exception in RSS feed rendering: {}", e.what());
                } });
            }
            catch (const std::exception &e)
            {
                RSS_ERROR_FMT("Exception in RSS feed card: {}", e.what());
                return false;
            }
        }

        std::string get_uri() const override
        {
            return std::format("rss-feed:{}", feed_id);
        }

        void refreshFeed()
        {
            try
            {
                RSS_INFO_FMT("Refreshing RSS feed: {}", feed_title);

                // Check if feed ID is valid
                if (feed_id < 0 || !rss_host)
                {
                    RSS_ERROR("Cannot refresh feed: invalid feed ID or RSS host not available");
                    return;
                }

                // Trigger a refresh in the RSS host
                if (rss_host->refreshFeed(feed_id))
                {
                    // Load the updated feed items
                    loadFeed();
                    RSS_INFO_FMT("Successfully refreshed RSS feed: {}", feed_title);
                    
                    // Notify all main RSS cards to invalidate their cache for this feed
                    "invalidate_freshness_cache"_sfn(feed_url);
                }
                else
                {
                    RSS_ERROR_FMT("Failed to refresh RSS feed: {}", feed_title);
                }
            }
            catch (const std::exception &e)
            {
                RSS_ERROR_FMT("Exception while refreshing feed: {}", e.what());
            }
        }

    private:
        inline static std::string currently_speaking_link;
        inline static std::mutex speaking_mutex;

        long long feed_id = -1;
        int items_limit = 20;
        char search_buffer[256] = "";
        std::string feed_title;
        std::string feed_url;
        std::string feed_image_url;
        std::shared_ptr<rouen::hosts::RSSHost> rss_host;
        std::vector<rouen::hosts::RSSHost::FeedItem> items;

        // Image handling
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *feed_image_texture = nullptr;
        int feed_image_width = 0;
        int feed_image_height = 0;
        std::shared_ptr<::helpers::ImageCache> image_cache;
        std::atomic<bool> feed_image_downloaded_{false};
        std::atomic<bool> items_loaded_{false};
        std::vector<rouen::hosts::RSSHost::FeedItem> pending_items_;
        std::mutex items_mutex_;

        struct LoadedItemTexture {
            SDL_Texture* texture = nullptr;
            int width = 0;
            int height = 0;
        };
        std::unordered_map<std::string, LoadedItemTexture> item_textures;

        std::pair<std::string, std::string> detect_language_and_select_voice(std::string_view text, std::string_view url) {
            std::string manual_lang = rss_host->getFeedLanguage(feed_id);
            if (manual_lang == "en") return {"en", ""}; // Use system default high-quality voice for English
            if (manual_lang == "es") return {"es", "Mónica"};
            if (manual_lang == "fr") return {"fr", "Flo"};
            if (manual_lang == "de") return {"de", "Eddy"};
            if (manual_lang == "it") return {"it", "Flo"};

            std::string lower_url = std::string(url);
            std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
            
            if (lower_url.find(".es") != std::string::npos || 
                lower_url.find("elpais.com") != std::string::npos ||
                lower_url.find("clarin.com") != std::string::npos ||
                lower_url.find("c5n.com") != std::string::npos ||
                lower_url.find("infobae.com") != std::string::npos) {
                return {"es", "Mónica"};
            }
            
            if (lower_url.find(".fr") != std::string::npos) {
                return {"fr", "Flo"};
            }
            
            if (lower_url.find(".de") != std::string::npos) {
                return {"de", "Eddy"};
            }
            
            if (lower_url.find(".it") != std::string::npos) {
                return {"it", "Flo"};
            }
            
            std::string lower_text = std::string(text);
            std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
            
            int es_score = 0;
            std::vector<std::string> es_words = {" el ", " la ", " de ", " en ", " y ", " que ", " los ", " las ", " con ", " para "};
            for (const auto& w : es_words) {
                size_t pos = 0;
                while ((pos = lower_text.find(w, pos)) != std::string::npos) {
                    es_score++;
                    pos += w.length();
                }
            }
            
            int en_score = 0;
            std::vector<std::string> en_words = {" the ", " of ", " and ", " to ", " a ", " in ", " is ", " that ", " with ", " for "};
            for (const auto& w : en_words) {
                size_t pos = 0;
                while ((pos = lower_text.find(w, pos)) != std::string::npos) {
                    en_score++;
                    pos += w.length();
                }
            }
            
            if (es_score > en_score && es_score > 2) {
                return {"es", "Mónica"};
            }
            
            return {"", ""}; // Default system voice
        }

        void request_image_download(const std::string& url) {
            static std::set<std::string> downloading_urls;
            static std::mutex downloading_mutex;

            {
                std::lock_guard<std::mutex> lock(downloading_mutex);
                if (downloading_urls.contains(url)) {
                    return; // Already downloading
                }
                downloading_urls.insert(url);
            }

            auto cache = image_cache;
            std::thread([cache, url]() {
                try {
                    cache->downloadAndCache(url);
                } catch (...) {}
                
                {
                    std::lock_guard<std::mutex> lock(downloading_mutex);
                    downloading_urls.erase(url);
                }
            }).detach();
        }
    };

} // namespace rouen::cards
