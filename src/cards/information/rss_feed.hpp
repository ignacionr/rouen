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
#include <sstream>
#include "../../helpers/llm_config.hpp"
#include "../../helpers/fetch.hpp"
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

            std::string clean_feed_str = feed_id_str;
            if (clean_feed_str.find(":play") != std::string::npos) {
                play_on_load = true;
                size_t colon_play = clean_feed_str.find(":play");
                clean_feed_str = clean_feed_str.substr(0, colon_play);
            }

            // Parse the feed ID
            try
            {
                feed_id = std::stoll(clean_feed_str);
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

        void on_close() override {
            media_player::stopAll();
            rouen::platform::stop_speech();
        }

        ~rss_feed() override
        {
            on_close();
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
            if (should_close)
            {
                return false;
            }
            if (feed_image_downloaded_.load())
            {
                feed_image_downloaded_ = false;
                loadFeedImage();
            }

            if (items_loaded_.load())
            {
                items_loaded_ = false;
                {
                    std::lock_guard<std::mutex> lock(items_mutex_);
                    items = std::move(pending_items_);
                }
                
                if (play_on_load) {
                    play_on_load = false; // Only trigger once
                    if (!items.empty()) {
                        // Find the freshest item (the one with the latest publish_date)
                        auto freshest_it = std::max_element(items.begin(), items.end(), [](const auto& a, const auto& b) {
                            return a.publish_date < b.publish_date;
                        });
                        
                        if (freshest_it != items.end()) {
                            std::string item_uri = std::format("rss-item:{}|||{}|||{}|||play", feed_id, freshest_it->link, freshest_it->title);
                            "create_card"_sfn(item_uri);
                        }
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(ai_tags_mutex_);
                if (!pending_ai_tags_.empty()) {
                    auto current = rss_host->getFeedTags(feed_id);
                    for (const auto& t : current) {
                        rss_host->removeFeedTag(feed_id, t);
                    }
                    for (const auto& t : pending_ai_tags_) {
                        rss_host->addFeedTag(feed_id, t);
                    }
                    pending_ai_tags_.clear();
                }
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
                    if (ImGui::Button(refresh_in_progress_.load() ? ICON_MD_REFRESH "..." : ICON_MD_REFRESH) && !refresh_in_progress_.load()) {
                        refreshFeed();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(refresh_in_progress_.load() ? "Refreshing feed..." : "Refresh feed");
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::EndGroup();
                    
                    ImGui::Checkbox("Continuous Play", &continuous_play);
                    ImGui::Spacing();
                    
                    // Display the feed image (or placeholder) and tags to the side of it
                    ImGui::BeginGroup();
                    
                    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
                    float img_display_w = 100.0f * dpi_scale;
                    float img_display_h = 100.0f * dpi_scale;
                    ImVec2 cur_pos = ImGui::GetCursorScreenPos();
                    
                    if (feed_image_texture && feed_image_width > 0 && feed_image_height > 0) {
                        float aspect_ratio = static_cast<float>(feed_image_width) / static_cast<float>(feed_image_height);
                        float display_width = img_display_h * aspect_ratio;
                        if (display_width > 120.0f * dpi_scale) display_width = 120.0f * dpi_scale;
                        img_display_w = display_width;
                        
                        ImGui::Image(
                            rouen::helpers::texture_id_cast(feed_image_texture),
                            ImVec2(img_display_w, img_display_h)
                        );
                    } else {
                        // Draw a placeholder cover box
                        ImGui::GetWindowDrawList()->AddRectFilled(cur_pos, ImVec2(cur_pos.x + img_display_w, cur_pos.y + img_display_h), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.5f)), 4.0f * dpi_scale);
                        std::string placeholder_icon = ICON_MD_RSS_FEED;
                        ImVec2 icon_size = ImGui::CalcTextSize(placeholder_icon.c_str());
                        ImVec2 icon_pos = ImVec2(cur_pos.x + (img_display_w - icon_size.x) * 0.5f, cur_pos.y + (img_display_h - icon_size.y) * 0.5f);
                        ImGui::GetWindowDrawList()->AddText(icon_pos, ImGui::GetColorU32(colors[1]), placeholder_icon.c_str());
                        ImGui::Dummy(ImVec2(img_display_w, img_display_h));
                    }
                    
                    ImGui::SameLine(img_display_w + 16.0f * dpi_scale);
                    
                    // Right Side: Language and Tags editor group
                    ImGui::BeginGroup();
                    
                    // Gear Icon (Edit Mode toggle) in the top-right corner of the group
                    float const start_x = ImGui::GetCursorPosX();
                    float const gear_width = ImGui::CalcTextSize(ICON_MD_SETTINGS).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    ImGui::SetCursorPosX(start_x + ImGui::GetContentRegionAvail().x - gear_width);
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.3f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.5f));
                    if (ImGui::Button(ICON_MD_SETTINGS)) {
                        editing_mode = !editing_mode;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(editing_mode ? "Exit editing mode" : "Edit feed settings");
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::SetCursorPosX(start_x);
                    
                    if (editing_mode) {
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
                        std::vector<std::string> all_possible_tags = rss_host->getAvailableTags();
                        std::sort(all_possible_tags.begin(), all_possible_tags.end());
                        const float tags_row_width = ImGui::GetContentRegionAvail().x;
                        float used_row_width = 0.0f;
                        
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
                        for (size_t t_idx = 0; t_idx < all_possible_tags.size(); ++t_idx) {
                            const auto& tag_name = all_possible_tags[t_idx];
                            bool has_tag = current_tags.contains(tag_name);
                            
                            // Keep tags on one line when space allows; wrap to next line otherwise.
                            const auto& style = ImGui::GetStyle();
                            const float chk_width =
                                ImGui::GetFrameHeight() +
                                style.ItemInnerSpacing.x +
                                ImGui::CalcTextSize(tag_name.c_str()).x +
                                style.FramePadding.x * 2.0f;
                            constexpr float tag_spacing = 12.0f;

                            const bool fits_same_line =
                                t_idx > 0 &&
                                (used_row_width + tag_spacing + chk_width <= tags_row_width);
                            if (fits_same_line) {
                                ImGui::SameLine(0.0f, tag_spacing);
                                used_row_width += tag_spacing + chk_width;
                            } else if (t_idx > 0) {
                                  used_row_width = 0.0f;
                            }
                            
                            std::string chk_id = std::format("{}##tag_chk_{}", tag_name, tag_name);
                            if (ImGui::Checkbox(chk_id.c_str(), &has_tag)) {
                                if (has_tag) {
                                    rss_host->addFeedTag(feed_id, tag_name);
                                } else {
                                    rss_host->removeFeedTag(feed_id, tag_name);
                                }
                            }

                            if (!fits_same_line) {
                                used_row_width = chk_width;
                            }
                        }
                        ImGui::PopStyleVar();
                        
                        // Add new tag input field
                        ImGui::Spacing();
                        ImGui::PushItemWidth(120.0f * dpi_scale);
                        bool add_tag_enter = ImGui::InputText("##new_tag", new_tag_buffer, sizeof(new_tag_buffer), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        bool add_tag_btn = ImGui::Button("Add Tag");
                        if ((add_tag_enter || add_tag_btn) && new_tag_buffer[0] != '\0') {
                            std::string tag_to_add = new_tag_buffer;
                            tag_to_add.erase(tag_to_add.begin(), std::find_if(tag_to_add.begin(), tag_to_add.end(), [](unsigned char ch) {
                                return !std::isspace(ch);
                            }));
                            tag_to_add.erase(std::find_if(tag_to_add.rbegin(), tag_to_add.rend(), [](unsigned char ch) {
                                return !std::isspace(ch);
                            }).base(), tag_to_add.end());
                            if (!tag_to_add.empty()) {
                                rss_host->addFeedTag(feed_id, tag_to_add);
                                new_tag_buffer[0] = '\0';
                            }
                        }

                        ImGui::SameLine();
                        if (ai_tagging_in_progress_) {
                            ImGui::Text("AI Thinking...");
                        } else {
                            if (ImGui::Button(ICON_MD_AUTO_AWESOME " AI Suggest")) {
                                suggestTagsWithAI();
                            }
                        }
                        if (!ai_tagging_error_.empty()) {
                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "AI Error: %s", ai_tagging_error_.c_str());
                        }
                        
                        // Show total item count
                        ImGui::Spacing();
                        ImGui::TextColored(colors[2], "Items: %zu", items.size());
                        
                        // Delete Feed Button
                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.9f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("Delete Feed")) {
                            ImGui::OpenPopup("Delete Feed?");
                        }
                        ImGui::PopStyleColor(3);
                        
                        if (ImGui::BeginPopupModal("Delete Feed?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::Text("Are you sure you want to permanently delete this feed?\n\"%s\"", feed_title.c_str());
                            ImGui::Separator();
                            
                            if (ImGui::Button("Delete", ImVec2(120.0f * dpi_scale, 0))) {
                                if (rss_host) {
                                    rss_host->deleteFeed(feed_url);
                                }
                                should_close = true;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SetItemDefaultFocus();
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel", ImVec2(120.0f * dpi_scale, 0))) {
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                    } else {
                        // Non-editing mode: only associated tags appear as pills, no checkboxes, no Language selector, no delete feed button
                        auto current_tags = rss_host->getFeedTags(feed_id);
                        if (!current_tags.empty()) {
                            ImGui::TextColored(colors[2], "Tags:");
                            
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * dpi_scale); // Pill-shaped!
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * dpi_scale, 3.0f * dpi_scale));
                            
                            // Style button to not react to hover/active (flat pill design)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.25f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.25f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.25f));
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
                            
                            const float tags_row_width = ImGui::GetContentRegionAvail().x;
                            float used_row_width = 0.0f;
                            const float tag_spacing = ImGui::GetStyle().ItemSpacing.x;
                            
                            size_t t_idx = 0;
                            for (const auto& tag_name : current_tags) {
                                const float pill_width =
                                    ImGui::CalcTextSize(tag_name.c_str()).x +
                                    ImGui::GetStyle().FramePadding.x * 2.0f;
                                
                                const bool fits_same_line =
                                    t_idx > 0 &&
                                    (used_row_width + tag_spacing + pill_width <= tags_row_width);
                                
                                if (fits_same_line) {
                                    ImGui::SameLine(0.0f, tag_spacing);
                                    used_row_width += tag_spacing + pill_width;
                                } else {
                                    used_row_width = pill_width;
                                }
                                
                                std::string button_id = std::format("{}##pill_{}", tag_name, tag_name);
                                ImGui::Button(button_id.c_str());
                                
                                t_idx++;
                            }
                            
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar(2);
                        }
                        
                        // Show total item count
                        ImGui::Spacing();
                        ImGui::TextColored(colors[2], "Items: %zu", items.size());
                    }
                    
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
                            const float add_item_section_height =
                                ImGui::GetFrameHeightWithSpacing() * 2.0f + 42.0f;
                            const float items_child_height =
                                std::max(120.0f, ImGui::GetContentRegionAvail().y - add_item_section_height);
                            const bool feed_items_scroll_open = ImGui::BeginChild(
                                "FeedItemsScroll",
                                ImVec2(0, items_child_height),
                                false,
                                ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NavFlattened
                            );
                            if (feed_items_scroll_open) {
                                size_t count = std::min(static_cast<size_t>(items_limit), filtered_items.size());
                                for (size_t i = 0; i < count; ++i) {
                                    auto& item = *filtered_items[i];
                                    ImGui::PushID(std::format("{}##{}", item.link, i).c_str());
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
                                            float const left_width = has_item_image ? avail_width - 130.0f * dpi_scale : avail_width;
                                            
                                            if (has_item_image) {
                                                ImGui::BeginGroup();
                                                ImGui::PushTextWrapPos(left_width);
                                            } else {
                                                ImGui::PushTextWrapPos(avail_width);
                                            }
                                            
                                            // Title (selectable to open item)
                                            if (ImGui::Selectable(item.title.c_str(), false, 0, ImVec2(left_width, 0))) {
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
                                                media_player::player(item.enclosure, colors[0], "Play Audio", item.feed_id, item.link, item.title, item.watermark, false, left_width);
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
                                                    media_player::player(best_media_url, colors[0], media_title, item.feed_id, item.link, item.title, item.watermark, false, left_width);
                                                }
                                            }
                                            // Enhanced: Check if this is a YouTube/Vimeo link without enclosure
                                            else if (item.link.find("youtube.com") != std::string::npos || 
                                                     item.link.find("youtu.be") != std::string::npos ||
                                                     item.link.find("vimeo.com") != std::string::npos) {
                                                media_player::player(item.link, colors[0], "Play Video", item.feed_id, item.link, item.title, item.watermark, false, left_width);
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
                                                
                                                if (ImGui::Button(btn_label.c_str(), ImVec2(left_width, 0))) {
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
                                                ImGui::SameLine(avail_width - 120.0f * dpi_scale);
                                                
                                                ImVec2 thumb_size(120.0f * dpi_scale, 80.0f * dpi_scale);
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
                                
                            }
                            ImGui::EndChild();

                        }
                        // Continuous Play logic
                        {
                            bool found_playing = false;
                            std::string active_url = "";
                            double active_pos = 0.0;
                            double active_dur = 0.0;
                            
                            for (const auto* item_ptr : filtered_items) {
                                if (!item_ptr) continue;
                                const auto& item = *item_ptr;
                                std::string media_url = get_item_media_url(item);
                                if (media_url.empty()) continue;
                                
                                // Look up in media_player::items()
                                for (const auto& [id, player_item] : media_player::items()) {
                                    if (player_item && player_item->url == media_url) {
                                        if (player_item->player_pid > 0 && !player_item->is_paused.load()) {
                                            found_playing = true;
                                            active_url = media_url;
                                            active_pos = player_item->position.load();
                                            active_dur = player_item->duration.load();
                                            break;
                                        }
                                    }
                                }
                                if (found_playing) break;
                            }
                            
                            if (found_playing) {
                                last_playing_url = active_url;
                                was_playing = true;
                                last_pos = active_pos;
                                last_dur = active_dur;

                                // Auto-expand items_limit if the playing item is beyond it
                                for (size_t i = 0; i < filtered_items.size(); ++i) {
                                    if (filtered_items[i] && get_item_media_url(*filtered_items[i]) == active_url) {
                                        if (static_cast<int>(i) >= items_limit) {
                                            items_limit = static_cast<int>(i) + 1;
                                        }
                                        break;
                                    }
                                }
                            } else if (was_playing) {
                                was_playing = false;
                                bool finished_naturally = (last_dur > 0.0 && last_pos >= last_dur - 4.0);
                                if (finished_naturally && continuous_play) {
                                    size_t found_idx = filtered_items.size();
                                    for (size_t i = 0; i < filtered_items.size(); ++i) {
                                        if (filtered_items[i] && get_item_media_url(*filtered_items[i]) == last_playing_url) {
                                            found_idx = i;
                                            break;
                                        }
                                    }
                                    if (found_idx != filtered_items.size()) {
                                        // Look for the next item in filtered_items that has playable media
                                        for (size_t next_idx = found_idx + 1; next_idx < filtered_items.size(); ++next_idx) {
                                            if (!filtered_items[next_idx]) continue;
                                            const auto& next_item_ref = *filtered_items[next_idx];
                                            std::string next_url = get_item_media_url(next_item_ref);
                                            if (!next_url.empty()) {
                                                media_player::stopAll();
                                                
                                                // Compute ImGui ID for next_url using correct ID stack (with std::format)
                                                std::string id_str = std::format("{}##{}", next_item_ref.link, next_idx);
                                                ImGui::PushID(id_str.c_str());
                                                ImGui::PushID(next_url.data(), next_url.data() + next_url.size());
                                                ImGuiID next_id = ImGui::GetID("MediaPlayer");
                                                ImGui::PopID();
                                                ImGui::PopID();
                                                
                                                auto& next_item = media_player::get_item(next_id);
                                                next_item.url = next_url;
                                                next_item.feed_id = next_item_ref.feed_id;
                                                next_item.item_link = next_item_ref.link;
                                                next_item.item_title = next_item_ref.title;
                                                next_item.watermark = next_item_ref.watermark;
                                                next_item.start_offset = next_item_ref.watermark.value_or(0.0);
                                                next_item.playMedia();
                                                break;
                                            }
                                        }
                                    }
                                }
                                last_playing_url.clear();
                            }
                        }

                        render_manual_add_item_section();
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

                if (refresh_in_progress_.exchange(true)) {
                    return;
                }

                std::jthread([this, f_id = feed_id, f_title = feed_title, f_url = feed_url]() {
                    try {
                        if (rss_host && rss_host->refreshFeed(f_id))
                        {
                            // Load the updated feed items
                            loadFeed();
                            RSS_INFO_FMT("Successfully refreshed RSS feed: {}", f_title);
                            
                            // Notify all main RSS cards to invalidate their cache for this feed
                            "invalidate_freshness_cache"_sfn(f_url);
                        }
                        else
                        {
                            RSS_ERROR_FMT("Failed to refresh RSS feed: {}", f_title);
                        }
                    } catch (const std::exception &e) {
                        RSS_ERROR_FMT("Exception while refreshing feed: {}", e.what());
                    } catch (...) {
                        RSS_ERROR("Unknown exception while refreshing feed");
                    }
                    refresh_in_progress_ = false;
                }).detach();
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
        bool play_on_load = false;
        bool editing_mode = false;
        bool should_close = false;
        int items_limit = 20;
        char search_buffer[256] = "";
        std::string feed_title;
        std::string feed_url;
        std::string feed_image_url;
        std::shared_ptr<rouen::hosts::RSSHost> rss_host;
        std::vector<rouen::hosts::RSSHost::FeedItem> items;

        char new_tag_buffer[256] = "";
        bool continuous_play = false;
        std::string last_playing_url;
        bool was_playing = false;
        double last_pos = 0.0;
        double last_dur = 0.0;

        bool ai_tagging_in_progress_ = false;
        std::string ai_tagging_error_;
        std::vector<std::string> pending_ai_tags_;
        std::mutex ai_tags_mutex_;

        void suggestTagsWithAI() {
            if (ai_tagging_in_progress_) return;

            if (!helpers::LLMConfig::is_configured()) {
                ai_tagging_error_ = "LLM not configured";
                return;
            }

            ai_tagging_in_progress_ = true;
            ai_tagging_error_.clear();

            std::jthread([this]() {
                try {
                    auto llm_instance = helpers::LLMConfig::create_llm_instance();
                    if (!llm_instance) {
                        ai_tagging_error_ = "Failed to create LLM";
                        ai_tagging_in_progress_ = false;
                        return;
                    }

                    std::string feed_info = std::format("Feed Title: {}\nFeed URL: {}\n", feed_title, feed_url);
                    feed_info += "Sample Feed Items:\n";
                    {
                        std::vector<rouen::hosts::RSSHost::FeedItem> items_copy;
                        {
                            std::lock_guard<std::mutex> lock(items_mutex_);
                            items_copy = items;
                        }
                        for (size_t i = 0; i < std::min(static_cast<size_t>(5), items_copy.size()); ++i) {
                            std::string desc = ::helpers::StringHelper::strip_html_tags(items_copy[i].description);
                            if (desc.length() > 150) {
                                desc = desc.substr(0, 150) + "...";
                            }
                            feed_info += std::format("- Title: {}\n  Description: {}\n", items_copy[i].title, desc);
                        }
                    }

                    std::string prompt = std::format(
                        "You are an expert content classifier. Please analyze this RSS feed and suggest exactly 5 classification tags for it.\n\n"
                        "{}\n"
                        "Here are the existing candidate tags that you should prefer if they are relevant:\n",
                        feed_info
                    );
                    
                    std::vector<std::string> all_tags = rss_host->getAvailableTags();
                    for (const auto& tag : all_tags) {
                        prompt += std::format("- {}\n", tag);
                    }
                    prompt += "\n"
                              "Choose exactly 5 tags. You can select relevant tags from the existing ones, or generate new ones if they fit better.\n"
                              "Return the tags as a comma-separated list on a single line (e.g. 'News, Tech, Podcasts, Uruguay, Apple').\n"
                              "Return ONLY the comma-separated list of tags and absolutely nothing else. Do not use quotes or prefixes.";

                    auto settings = helpers::LLMConfig::get_current_config();
                    auto fetcher = std::make_shared<http::fetch>();

                    auto response = llm_instance->sendMessage(
                        prompt,
                        [fetcher](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher->post(url, data, header_client);
                        },
                        "user",
                        settings.model_name,
                        "",
                        0.5f
                    );

                    if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                        std::string generated = response.choices[0].message.content;
                        generated.erase(0, generated.find_first_not_of(" \t\r\n\"'"));
                        generated.erase(generated.find_last_not_of(" \t\r\n\"'") + 1);

                        std::vector<std::string> suggested_tags;
                        std::stringstream ss(generated);
                        std::string token;
                        while (std::getline(ss, token, ',')) {
                            token.erase(0, token.find_first_not_of(" \t\r\n\"'"));
                            token.erase(token.find_last_not_of(" \t\r\n\"'") + 1);
                            if (!token.empty()) {
                                suggested_tags.push_back(token);
                            }
                        }

                        if (!suggested_tags.empty()) {
                            std::lock_guard<std::mutex> lock(ai_tags_mutex_);
                            pending_ai_tags_ = std::move(suggested_tags);
                        } else {
                            ai_tagging_error_ = "Failed to parse tags from AI response";
                        }
                    } else {
                        ai_tagging_error_ = "Empty response from AI";
                    }
                } catch (const std::exception& e) {
                    ai_tagging_error_ = e.what();
                }
                ai_tagging_in_progress_ = false;
            }).detach();
        }

        // Image handling
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *feed_image_texture = nullptr;
        int feed_image_width = 0;
        int feed_image_height = 0;
        std::shared_ptr<::helpers::ImageCache> image_cache;
        std::atomic<bool> feed_image_downloaded_{false};
        std::atomic<bool> items_loaded_{false};
        std::atomic<bool> refresh_in_progress_{false};
        std::vector<rouen::hosts::RSSHost::FeedItem> pending_items_;
        std::mutex items_mutex_;

        struct LoadedItemTexture {
            SDL_Texture* texture = nullptr;
            int width = 0;
            int height = 0;
        };
        std::unordered_map<std::string, LoadedItemTexture> item_textures;
        char manual_item_url_buffer[1024] = "";
        std::string add_item_feedback;
        bool add_item_feedback_success = false;

        void render_manual_add_item_section() {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(colors[2], "Add item URL to this feed:");

            const float add_button_width =
                ImGui::CalcTextSize("Add Item").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float add_input_width =
                ImGui::GetContentRegionAvail().x - add_button_width - ImGui::GetStyle().ItemSpacing.x;
            ImGui::PushItemWidth(std::max(140.0f, add_input_width));
            const bool submit_from_enter = ImGui::InputText(
                "##manual_item_url",
                manual_item_url_buffer,
                sizeof(manual_item_url_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue
            );
            ImGui::PopItemWidth();

            ImGui::SameLine();
            const bool submit_from_button = ImGui::Button("Add Item");
            if (submit_from_enter || submit_from_button) {
                if (manual_item_url_buffer[0] == '\0') {
                    add_item_feedback = "Please enter a URL.";
                    add_item_feedback_success = false;
                } else {
                    const bool added = rss_host && rss_host->addFeedItem(feed_id, manual_item_url_buffer);
                    if (added) {
                        add_item_feedback = "Item added to feed.";
                        add_item_feedback_success = true;
                        manual_item_url_buffer[0] = '\0';
                        loadFeed();
                    } else {
                        add_item_feedback = "Failed to add item.";
                        add_item_feedback_success = false;
                    }
                }
            }

            if (!add_item_feedback.empty()) {
                const ImVec4 feedback_color = add_item_feedback_success
                    ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f)
                    : ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
                ImGui::TextColored(feedback_color, "%s", add_item_feedback.c_str());
            }
        }

        static std::string get_item_media_url(const rouen::hosts::RSSHost::FeedItem& item) {
            if (!item.enclosure.empty()) {
                return item.enclosure;
            }
            std::string best = item.get_best_media_url();
            if (!best.empty()) {
                return best;
            }
            if (item.link.find("youtube.com") != std::string::npos || 
                item.link.find("youtu.be") != std::string::npos ||
                item.link.find("vimeo.com") != std::string::npos) {
                return item.link;
            }
            return "";
        }

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
