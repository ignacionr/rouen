#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include "../../helpers/imgui_include.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <iostream>
#include <future>
#include <regex>
#include <thread>
#include <set>

#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../hosts/rss_host.hpp"
#include "../../models/rss/feed.hpp"
#include "../../models/rss/rss_date_parser.hpp"
#include "../../registrar.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"

namespace rouen::cards {

// Main RSS card that displays all feeds
class rss : public card {
public:
    rss() {
        // Set custom colors for the RSS card
        colors[0] = {0.8f, 0.3f, 0.3f, 1.0f}; // Red primary color
        colors[1] = {0.9f, 0.4f, 0.4f, 0.7f}; // Lighter red secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(1.0f, 0.6f, 0.6f, 1.0f)); // Light red for titles
        get_color(3, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Green for active/positive elements
        get_color(4, ImVec4(0.8f, 0.8f, 0.3f, 1.0f)); // Yellow for warnings/highlights
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        
        // Feed freshness colors
        get_color(6, ImVec4(0.0f, 0.8f, 0.0f, 1.0f)); // Fresh (less than 1 hour)
        get_color(7, ImVec4(0.6f, 0.8f, 0.0f, 1.0f)); // Recent (less than 1 day)
        get_color(8, ImVec4(0.8f, 0.6f, 0.0f, 1.0f)); // Stale (less than 3 days)
        get_color(9, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Old (older than 3 days)
        
        name("RSS Reader");
        requested_fps = 1;  // Update once per second
        width = 805.0f;
        
        // Use the shared host instance instead of creating a new one
        rss_host = getHost();

        // Register handler for cache invalidation messages
        registrar::add<std::function<void(std::string const&)>>(
            "invalidate_freshness_cache",
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& feed_url) {
                    this->invalidate_freshness_cache(feed_url);
                }
            )
        );
    }
    
    ~rss() override {
        clear_feed_textures();
    }

    void clear_feed_textures() {
        for (auto& [url, lt] : feed_textures) {
            if (lt.texture) {
                SDL_DestroyTexture(lt.texture);
            }
        }
        feed_textures.clear();
    }

    void set_renderer(SDL_Renderer* r) {
        renderer = r;
        if (renderer && !image_cache) {
            auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
            auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();
            image_cache = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
        }
    }

    std::string get_uri() const override
    {
        return "rss";
    }
    
    void render_add_feed() {
        static char url_buffer[512] = "";
        
        // Add a new feed section
        ImGui::TextColored(colors[0], "Add RSS Feed:");
        
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        float add_btn_w = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float ai_btn_w = ImGui::CalcTextSize("AI Search").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float input_w = ImGui::GetContentRegionAvail().x - add_btn_w - ai_btn_w - ImGui::GetStyle().ItemSpacing.x * 2.0f - 8.0f * dpi_scale;
        
        ImGui::PushItemWidth(input_w);
        bool url_entered = ImGui::InputText("##url", url_buffer, sizeof(url_buffer), 
            ImGuiInputTextFlags_EnterReturnsTrue);
        
        // Check for Cmd+Enter (macOS) or Ctrl+Enter (Windows/Linux) to trigger AI search
        bool ai_search_requested = false;
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGuiIO& io = ImGui::GetIO();
            if ((io.KeySuper && !io.KeyCtrl) || (!io.KeySuper && io.KeyCtrl)) { // Cmd on Mac, Ctrl on others
                ai_search_requested = true;
            }
        }
        
        // Show placeholder text when input is empty
        if (url_buffer[0] == '\0' && !ImGui::IsItemActive()) {
            auto pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + 5, pos.y + 2),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "RSS feed URL or topic (Cmd/Ctrl+Enter for AI search)..."
            );
        }
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        bool add_clicked = ImGui::Button("Add");
        
        ImGui::SameLine();
        bool ai_search_clicked = ImGui::Button("AI Search");
        
        // Show AI search status on the following line (when needed)
        if (ai_search_in_progress_) {
            ImGui::TextColored(colors[4], "AI is searching...");
        }
        
        // Handle AI search trigger
        if ((ai_search_requested || ai_search_clicked) && url_buffer[0] != '\0' && !ai_search_in_progress_) {
            triggerAIFeedSearch(std::string(url_buffer));
        }
        
        // Handle regular URL entry
        if ((url_entered || add_clicked) && url_buffer[0] != '\0' && !ai_search_in_progress_) {
            // Only proceed if this doesn't look like an AI search request
            if (!ai_search_requested) {
                // Add the feed
                if (addFeed(url_buffer)) {
                    // Clear the input field on success
                    url_buffer[0] = '\0';
                }
            }
        }
        
        // Display AI search results if available
        if (!ai_search_results_.empty()) {
            ImGui::Separator();
            ImGui::TextColored(colors[0], "AI Found Feeds:");
            
            for (size_t i = 0; i < ai_search_results_.size(); ++i) {
                const auto& result = ai_search_results_[i];
                ImGui::PushID(static_cast<int>(i));
                
                if (ImGui::Button("Add")) {
                    if (addFeed(result.url)) {
                        // Remove this result after successful addition
                        ai_search_results_.erase(ai_search_results_.begin() + static_cast<ptrdiff_t>(i));
                        ImGui::PopID();
                        break;
                    }
                }
                
                ImGui::SameLine();
                ImGui::TextWrapped("%s - %s", result.title.c_str(), result.url.c_str());
                
                if (!result.description.empty()) {
                    ImGui::Indent();
                    ImGui::TextColored(colors[1], "%s", result.description.c_str());
                    ImGui::Unindent();
                }
                
                ImGui::PopID();
            }
            
            if (ImGui::Button("Clear Results")) {
                ai_search_results_.clear();
            }
        }
    }

    bool render() override {
        // Check if AI search is complete
        if (ai_search_in_progress_ && ai_search_future_.valid()) {
            if (ai_search_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                // AI search completed, get results
                ai_search_results_ = ai_search_future_.get();
                ai_search_in_progress_ = false;
                RSS_INFO_FMT("AI search completed with {} results", ai_search_results_.size());
            }
        }
        
        // Periodically invalidate the cache to ensure freshness colors update
        // We do this based on frame count to avoid doing it every single frame
        static int frame_counter = 0;
        frame_counter++;
        
        // Every 60 frames (roughly every minute at 1 FPS), clear the entire cache
        // This ensures that even if feeds are refreshed in the background, we'll
        // eventually pick up the new update times
        if (frame_counter >= 60) {
            invalidate_freshness_cache();
            frame_counter = 0;
        }
        
        return render_window([this] {
            // Draw thin progress line on top of the window content
            {
                auto last_refresh = rss_host->last_refresh_time();
                auto interval = rss_host->refresh_interval_s();
                auto now = std::chrono::system_clock::now();
                
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh).count() / 1000.0;
                float ratio = static_cast<float>(elapsed / interval);
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float width = ImGui::GetContentRegionAvail().x;
                float height = 2.0f;
                
                ImVec2 p_min = pos;
                ImVec2 p_max = ImVec2(pos.x + width * ratio, pos.y + height);
                
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p_min, 
                    ImVec2(pos.x + width, pos.y + height), 
                    ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.3f))
                );
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p_min, 
                    p_max, 
                    ImGui::GetColorU32(colors[0])
                );
                
                // Invisible button to capture hover for tooltip & double click to force refresh
                ImGui::SetCursorScreenPos(pos);
                ImGui::InvisibleButton("##refresh_progress_bar", ImVec2(width, height + 4.0f));
                if (ImGui::IsItemHovered()) {
                    int seconds_left = std::max(0, static_cast<int>(interval - elapsed));
                    int minutes = seconds_left / 60;
                    int seconds = seconds_left % 60;
                    ImGui::SetTooltip("Next auto-refresh in %02d:%02d\nDouble-click to refresh now", minutes, seconds);
                    
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        rss_host->triggerManualRefresh();
                    }
                }
                
                ImGui::Dummy(ImVec2(0.0f, height + 4.0f));
            }

            // Tag Filter Pills
            {
                std::vector<std::string> tags = {"All", "News", "Tech / Dev", "Podcasts", "YouTube", "Other"};
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f); // Pill-shaped!
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
                
                for (size_t i = 0; i < tags.size(); ++i) {
                    const auto& tag = tags[i];
                    bool is_selected = (selected_tag_ == tag);
                    
                    if (is_selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, colors[0]); // Primary theme color
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x * 1.1f, colors[0].y * 1.1f, colors[0].z * 1.1f, colors[0].w));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x * 0.9f, colors[0].y * 0.9f, colors[0].z * 0.9f, colors[0].w));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.15f, 0.6f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.2f, 0.9f));
                    }
                    
                    if (ImGui::Button(tag.c_str())) {
                        selected_tag_ = tag;
                    }
                    
                    ImGui::PopStyleColor(3);
                    
                    if (i < tags.size() - 1) {
                        ImGui::SameLine();
                    }
                }
                ImGui::PopStyleVar(2);
                ImGui::Spacing();
            }

            // Feeds section title
            ImGui::TextColored(colors[0], "Your RSS Feeds:");
            
            // Search functionality
            static char search_buffer[256] = "";
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
            
            // Calculate width for input field to leave space for clear button
            float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
            float clear_button_width = 20.0f * dpi_scale;
            float input_width = ImGui::GetContentRegionAvail().x - clear_button_width - ImGui::GetStyle().ItemSpacing.x;
            ImGui::PushItemWidth(input_width);
            
            ImGui::InputText("##search", search_buffer, static_cast<int>(sizeof(search_buffer)));
            
            // Handle ESC key to clear search while maintaining focus
            if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                search_buffer[0] = '\0';
                // Focus will naturally stay on the input field since we're not changing focus
            }

            // for the placeholder
            auto pos = ImGui::GetItemRectMin();
            
            // Clear button (soft X)
            ImGui::SameLine();
            if (ImGui::SmallButton("×")) {
                search_buffer[0] = '\0'; // Clear the search buffer
                ImGui::SetKeyboardFocusHere(-1); // Focus the previous item (the InputText)
            }
            
            // Show placeholder text when input is empty
            if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Search feeds..."
                );
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(); // Pop FrameBg
            
            ImGui::Separator();
            
            static bool settings_open = false;
            float bottom_margin = (settings_open ? 210.0f : 120.0f) * dpi_scale;
            
            // Create scrollable area for feeds
            auto available_size = ImGui::GetContentRegionAvail();
            ImVec2 scroll_area_size = ImVec2(available_size.x, available_size.y - bottom_margin);
            if (ImGui::BeginChild("FeedsScrollArea", scroll_area_size, true)) {
                auto all_feeds = rss_host->feeds();
                
                // Filter feeds based on selected_tag_
                std::vector<std::shared_ptr<media::rss::feed>> feeds;
                if (selected_tag_ == "All") {
                    feeds = all_feeds;
                } else {
                    for (const auto& feed : all_feeds) {
                        bool matches = false;
                        std::string url = feed->source_link;
                        std::transform(url.begin(), url.end(), url.begin(), ::tolower);
                        
                        if (selected_tag_ == "Podcasts") {
                            matches = (url.find("megaphone.fm") != std::string::npos ||
                                       url.find("spreaker.com") != std::string::npos ||
                                       url.find("libsyn.com") != std::string::npos ||
                                       url.find("simplecast.com") != std::string::npos ||
                                       url.find("transistor.fm") != std::string::npos ||
                                       url.find("anchor.fm") != std::string::npos ||
                                       url.find("audioboom.com") != std::string::npos ||
                                       url.find("podbean.com") != std::string::npos ||
                                       url.find("acast.com") != std::string::npos ||
                                       url.find("podplaystudio.com") != std::string::npos);
                            if (!matches) {
                                for (const auto& item : feed->items) {
                                    if (!item.enclosure.empty() && 
                                        (item.enclosure.find(".mp3") != std::string::npos || 
                                         item.enclosure.find(".wav") != std::string::npos ||
                                         item.enclosure.find("audio") != std::string::npos)) {
                                        matches = true;
                                        break;
                                    }
                                }
                            }
                        } else if (selected_tag_ == "YouTube") {
                            matches = (url.find("youtube.com") != std::string::npos ||
                                       url.find("youtu.be") != std::string::npos);
                        } else if (selected_tag_ == "Tech / Dev") {
                            matches = (url.find("cpp") != std::string::npos ||
                                       url.find("changelog") != std::string::npos ||
                                       url.find("softwareengineering") != std::string::npos ||
                                       url.find("devsarg") != std::string::npos ||
                                       url.find("osnews") != std::string::npos ||
                                       url.find("computerhistory") != std::string::npos ||
                                       url.find("pythontest") != std::string::npos);
                        } else if (selected_tag_ == "News") {
                            matches = (url.find("wsj") != std::string::npos ||
                                       url.find("npr") != std::string::npos ||
                                       url.find("elpais") != std::string::npos ||
                                       url.find("rt.com") != std::string::npos ||
                                       url.find("repubblica") != std::string::npos ||
                                       url.find("leftcom") != std::string::npos ||
                                       url.find("themoscowtimes") != std::string::npos ||
                                       url.find("aljazeera") != std::string::npos ||
                                       url.find("channel4") != std::string::npos ||
                                       url.find("c5n") != std::string::npos ||
                                       url.find("clarin") != std::string::npos ||
                                       url.find("dw.com") != std::string::npos ||
                                       url.find("bbci.co.uk") != std::string::npos);
                        } else if (selected_tag_ == "Other") {
                            bool is_podcast = (url.find("megaphone.fm") != std::string::npos ||
                                               url.find("spreaker.com") != std::string::npos ||
                                               url.find("libsyn.com") != std::string::npos ||
                                               url.find("simplecast.com") != std::string::npos ||
                                               url.find("transistor.fm") != std::string::npos ||
                                               url.find("anchor.fm") != std::string::npos ||
                                               url.find("audioboom.com") != std::string::npos ||
                                               url.find("podbean.com") != std::string::npos ||
                                               url.find("acast.com") != std::string::npos ||
                                               url.find("podplaystudio.com") != std::string::npos);
                            if (!is_podcast) {
                                for (const auto& item : feed->items) {
                                    if (!item.enclosure.empty() && 
                                        (item.enclosure.find(".mp3") != std::string::npos || 
                                         item.enclosure.find(".wav") != std::string::npos ||
                                         item.enclosure.find("audio") != std::string::npos)) {
                                        is_podcast = true;
                                        break;
                                    }
                                }
                            }
                            bool is_youtube = (url.find("youtube.com") != std::string::npos ||
                                               url.find("youtu.be") != std::string::npos);
                            bool is_tech = (url.find("cpp") != std::string::npos ||
                                            url.find("changelog") != std::string::npos ||
                                            url.find("softwareengineering") != std::string::npos ||
                                            url.find("devsarg") != std::string::npos ||
                                            url.find("osnews") != std::string::npos ||
                                            url.find("computerhistory") != std::string::npos ||
                                            url.find("pythontest") != std::string::npos);
                            bool is_news = (url.find("wsj") != std::string::npos ||
                                            url.find("npr") != std::string::npos ||
                                            url.find("elpais") != std::string::npos ||
                                            url.find("rt.com") != std::string::npos ||
                                            url.find("repubblica") != std::string::npos ||
                                            url.find("leftcom") != std::string::npos ||
                                            url.find("themoscowtimes") != std::string::npos ||
                                            url.find("aljazeera") != std::string::npos ||
                                            url.find("channel4") != std::string::npos ||
                                            url.find("c5n") != std::string::npos ||
                                            url.find("clarin") != std::string::npos ||
                                            url.find("dw.com") != std::string::npos ||
                                            url.find("bbci.co.uk") != std::string::npos);
                            matches = (!is_podcast && !is_youtube && !is_tech && !is_news);
                        }
                        
                        if (matches) {
                            feeds.push_back(feed);
                        }
                    }
                }

                if (feeds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds in this category");
                } else {
                    std::string search_text = search_buffer;
                    if (search_text.empty()) {
                        bool has_matches = false;
                        render_feed_list(feeds, search_text, has_matches);
                    } else {
                        // 1. Search matching Feeds within current category
                        std::vector<std::shared_ptr<media::rss::feed>> matching_feeds;
                        for (const auto& feed : feeds) {
                            std::string title = feed->feed_title.empty() ? feed->source_link : feed->feed_title;
                            if (::helpers::StringHelper::contains_case_insensitive(title, search_text) ||
                                ::helpers::StringHelper::contains_case_insensitive(feed->source_link, search_text)) {
                                matching_feeds.push_back(feed);
                            }
                        }

                        if (!matching_feeds.empty()) {
                            ImGui::TextColored(colors[0], "Matching Feeds (%d):", static_cast<int>(matching_feeds.size()));
                            ImGui::Spacing();
                            bool has_matches = false;
                            render_feed_list(matching_feeds, search_text, has_matches);
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                        }

                        // 2. Deep Search matching Articles within category
                        auto raw_matching_items = rss_host->searchItems(search_text);
                        std::vector<hosts::RSSHost::FeedItem> matching_items;
                        if (selected_tag_ == "All") {
                            matching_items = raw_matching_items;
                        } else {
                            std::set<long long> active_ids;
                            for (const auto& f : feeds) {
                                active_ids.insert(f->repo_id);
                            }
                            for (const auto& item : raw_matching_items) {
                                if (active_ids.contains(item.feed_id)) {
                                    matching_items.push_back(item);
                                }
                            }
                        }
                        ImGui::TextColored(colors[0], "Matching Articles (%d):", static_cast<int>(matching_items.size()));
                        ImGui::Spacing();

                        if (matching_items.empty()) {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No articles found matching your search");
                        } else {
                            for (size_t i = 0; i < matching_items.size(); ++i) {
                                const auto& item = matching_items[i];
                                ImGui::PushID(item.link.c_str());

                                try {
                                    ImGui::BeginGroup();
                                    try {
                                        // Try to load item thumbnail
                                        SDL_Texture* item_tex = nullptr;
                                        int item_tex_w = 0, item_tex_h = 0;
                                        if (renderer && image_cache && !item.image_url.empty()) {
                                            if (feed_textures.contains(item.image_url)) {
                                                auto& lt = feed_textures[item.image_url];
                                                item_tex = lt.texture;
                                                item_tex_w = lt.width;
                                                item_tex_h = lt.height;
                                            } else {
                                                int cached_w = 0, cached_h = 0;
                                                if (image_cache->isCached(item.image_url, cached_w, cached_h)) {
                                                    item_tex = image_cache->getTexture(renderer, item.image_url, item_tex_w, item_tex_h);
                                                    if (item_tex) {
                                                        feed_textures[item.image_url] = {item_tex, item_tex_w, item_tex_h};
                                                    }
                                                } else {
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

                                        // Display feed title as source tag
                                        ImGui::TextColored(colors[0], "[%s]", item.feed_title.c_str());
                                        ImGui::SameLine();
                                        
                                        // Title (selectable to open item)
                                        if (ImGui::Selectable(item.title.c_str(), false, 0, ImVec2(has_item_image ? avail_width - 130.0f : avail_width, 0))) {
                                            std::string item_uri = std::format("rss-item:{}|||{}|||{}", item.feed_id, item.link, item.title);
                                            "create_card"_sfn(item_uri);
                                        }

                                        // Date (with Age)
                                        auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                                        std::tm* tm = std::localtime(&time);
                                        char date_str[64];
                                        std::strftime(date_str, sizeof(date_str), "%d %b %Y %H:%M", tm);
                                        std::string age_str = media::rss::format_rss_age(item.publish_date);
                                        ImGui::TextColored(colors[1], "%s (%s)", date_str, age_str.c_str());

                                        // Description
                                        if (!item.description.empty()) {
                                            std::string desc = item.description;
                                            desc.erase(std::remove_if(desc.begin(), desc.end(),
                                                [](char c) { return c == '<' || c == '>'; }), desc.end());
                                            if (desc.length() > 100) {
                                                desc = desc.substr(0, 97) + "...";
                                            }
                                            ImGui::TextWrapped("%s", desc.c_str());
                                        }

                                        ImGui::PopTextWrapPos();

                                        if (has_item_image) {
                                            ImGui::EndGroup();
                                            ImGui::SameLine(avail_width - 120.0f);

                                            ImVec2 thumb_size(120.0f, 80.0f);
                                            ImVec2 thumb_pos = ImGui::GetCursorScreenPos();

                                            ImVec2 uv0, uv1;
                                            calculate_cover_uvs(thumb_size.x, thumb_size.y, static_cast<float>(item_tex_w), static_cast<float>(item_tex_h), uv0, uv1);

                                            ImDrawList* d_list = ImGui::GetWindowDrawList();
                                            d_list->AddImage(
                                                rouen::helpers::texture_id_cast(item_tex),
                                                thumb_pos,
                                                ImVec2(thumb_pos.x + thumb_size.x, thumb_pos.y + thumb_size.y),
                                                uv0,
                                                uv1
                                            );

                                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.05f));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.1f));
                                            if (ImGui::Button(std::format("##thumb_btn_{}", i).c_str(), thumb_size)) {
                                                std::string item_uri = std::format("rss-item:{}|||{}|||{}", item.feed_id, item.link, item.title);
                                                "create_card"_sfn(item_uri);
                                            }
                                            ImGui::PopStyleColor(3);
                                        }
                                    }
                                    catch (const std::exception& e) {
                                        RSS_ERROR_FMT("Exception in search item rendering: {}", e.what());
                                    }

                                    ImGui::EndGroup();
                                }
                                catch (const std::exception& e) {
                                    RSS_ERROR_FMT("Exception in search item group: {}", e.what());
                                    ImGui::EndGroup();
                                }

                                ImGui::Separator();
                                ImGui::PopID();
                            }
                        }
                    }

                    // Process deletion requests
                    for (const auto& url : feeds_to_delete) {
                        rss_host->deleteFeed(url);
                    }
                    feeds_to_delete.clear();
                }
            }
            ImGui::EndChild();

            ImGui::Separator();

            render_add_feed();
            
            ImGui::Separator();
            settings_open = ImGui::CollapsingHeader("Connection Settings");
            if (settings_open) {
                int timeout = rss_host->get_timeout();
                ImGui::SetNextItemWidth(120.0f * dpi_scale);
                if (ImGui::SliderInt("Timeout (s)##rss_timeout", &timeout, 5, 180)) {
                    rss_host->set_timeout(timeout);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(5s to 180s)");
                
                bool auto_timeout = rss_host->is_auto_timeout_enabled();
                if (ImGui::Checkbox("Auto-increase timeout on slow connection", &auto_timeout)) {
                    rss_host->set_auto_timeout_enabled(auto_timeout);
                }
                if (auto_timeout) {
                    ImGui::TextDisabled("Adjusts timeout dynamically (adds +20s per failure, up to 180s).");
                }
            }
        });
    }

    std::string truncate_text(const std::string& text, float max_width) {
        if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
        std::string truncated = text;
        while (!truncated.empty() && ImGui::CalcTextSize((truncated + "...").c_str()).x > max_width) {
            truncated.pop_back();
        }
        return truncated + "...";
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
            // Texture is wider than target, crop sides
            float f = target_aspect / tex_aspect;
            float c = (1.0f - f) * 0.5f;
            uv0 = ImVec2(c, 0.0f);
            uv1 = ImVec2(1.0f - c, 1.0f);
        } else {
            // Texture is taller than target, crop top/bottom
            float f = tex_aspect / target_aspect;
            float c = (1.0f - f) * 0.5f;
            uv0 = ImVec2(0.0f, c);
            uv1 = ImVec2(1.0f, 1.0f - c);
        }
    }

    void render_feed_list(auto &feeds, std::string &search_text, bool &has_matches)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        auto now = std::chrono::system_clock::now();

        float card_width = 180.0f;
        float card_height = 290.0f;
        float spacing = 12.0f;
        float avail_width = ImGui::GetContentRegionAvail().x;
        int cols = std::max(1, static_cast<int>(avail_width / (card_width + spacing)));
        
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
        
        int visible_index = 0;
        for (const auto &feed : feeds)
        {
            ImGui::PushID(feed->source_link.c_str());

            std::string title = feed->feed_title.empty() ? feed->source_link : feed->feed_title;

            // Filter based on search query if search text is present
            if (!search_text.empty() &&
                !::helpers::StringHelper::contains_case_insensitive(title, search_text) &&
                !::helpers::StringHelper::contains_case_insensitive(feed->source_link, search_text))
            {
                ImGui::PopID();
                continue; // Skip items that don't match the search
            }

            has_matches = true;

            if (visible_index > 0 && (visible_index % cols) != 0) {
                ImGui::SameLine();
            }

            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            ImVec2 end_pos = ImVec2(start_pos.x + card_width, start_pos.y + card_height);
            
            bool is_hovered = ImGui::IsMouseHoveringRect(start_pos, end_pos);
            
            // Draw background and borders
            ImVec4 bg_color = is_hovered ? ImVec4(0.22f, 0.22f, 0.26f, 0.8f) : ImVec4(0.14f, 0.14f, 0.17f, 0.6f);
            ImVec4 border_color = is_hovered ? colors[0] : ImVec4(0.24f, 0.24f, 0.27f, 0.6f);
            
            draw_list->AddRectFilled(start_pos, end_pos, ImGui::GetColorU32(bg_color), 8.0f);
            draw_list->AddRect(start_pos, end_pos, ImGui::GetColorU32(border_color), 8.0f);
            
            // Padding
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f, start_pos.y + 6.0f));
            ImGui::BeginGroup();
            
            // 1. Draw Image / Placeholder
            ImVec2 img_size(card_width - 12.0f, 220.0f);
            ImVec2 img_pos = ImGui::GetCursorScreenPos();
            
            // Invisible button to capture click on cover
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.05f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.1f));
            bool cover_clicked = ImGui::Button(std::format("##cover_btn_{}", feed->repo_id).c_str(), img_size);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            
            // Render the actual image or placeholder on the draw list
            std::string img_url = feed->image_url();
            SDL_Texture* tex = nullptr;
            int img_w = 0, img_h = 0;
            if (renderer && image_cache && !img_url.empty()) {
                if (feed_textures.contains(img_url)) {
                    auto& lt = feed_textures[img_url];
                    tex = lt.texture;
                    img_w = lt.width;
                    img_h = lt.height;
                } else {
                    int cached_w = 0, cached_h = 0;
                    if (image_cache->isCached(img_url, cached_w, cached_h)) {
                        tex = image_cache->getTexture(renderer, img_url, img_w, img_h);
                        if (tex) {
                            feed_textures[img_url] = {tex, img_w, img_h};
                        }
                    } else {
                        request_image_download(img_url);
                    }
                }
            }
            
            if (tex) {
                // Draw texture in the image region, cropped to fit the aspect ratio
                ImVec2 uv0, uv1;
                calculate_cover_uvs(img_size.x, img_size.y, static_cast<float>(img_w), static_cast<float>(img_h), uv0, uv1);
                draw_list->AddImage(rouen::helpers::texture_id_cast(tex), img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), uv0, uv1);
            } else {
                // Draw placeholder
                draw_list->AddRectFilled(img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.5f)), 4.0f);
                std::string placeholder_icon = ICON_MD_RSS_FEED;
                ImVec2 icon_size = ImGui::CalcTextSize(placeholder_icon.c_str());
                ImVec2 icon_pos = ImVec2(img_pos.x + (img_size.x - icon_size.x) * 0.5f, img_pos.y + (img_size.y - icon_size.y) * 0.5f);
                draw_list->AddText(icon_pos, ImGui::GetColorU32(colors[1]), placeholder_icon.c_str());
            }
            
            ImGui::Spacing();
            
            // 2. Draw Title
            std::string truncated_title = truncate_text(title, card_width - 12.0f);
            bool title_clicked = ImGui::Selectable(std::format("{}##title_{}", truncated_title, feed->repo_id).c_str(), false, ImGuiSelectableFlags_None, ImVec2(card_width - 12.0f, 0));
            
            // Right-click context menu
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                ImGui::OpenPopup(std::format("FeedContextMenu_{}", feed->repo_id).c_str());
            }
            if (ImGui::BeginPopup(std::format("FeedContextMenu_{}", feed->repo_id).c_str())) {
                if (ImGui::MenuItem("Copy Feed URL")) {
                    ImGui::SetClipboardText(feed->source_link.c_str());
                }
                if (ImGui::MenuItem("Copy Feed Title")) {
                    ImGui::SetClipboardText(title.c_str());
                }
                ImGui::EndPopup();
            }
            
            // 3. Draw Footer
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f, start_pos.y + card_height - 24.0f));
            
            ImVec4 freshness_color = get_freshness_color(feed, now);
            ImGui::TextColored(freshness_color, "●");
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Text, colors[1]);
            ImGui::Text("%zu items", feed->items.size());
            ImGui::PopStyleColor();
            
            ImGui::SameLine(card_width - 24.0f);
            if (ImGui::SmallButton(std::format("×##del_{}", feed->repo_id).c_str())) {
                feeds_to_delete.push_back(feed->source_link);
            }
            
            ImGui::EndGroup();
            
            if (cover_clicked || title_clicked) {
                std::string feed_uri = std::format("rss-feed:{}", feed->repo_id);
                "create_card"_sfn(feed_uri);
            }
            
            // Reset cursor back to the start of the card box but advanced by width + spacing
            ImGui::SetCursorScreenPos(start_pos);
            ImGui::Dummy(ImVec2(card_width, card_height));

            visible_index++;
            ImGui::PopID();
        }
        
        ImGui::PopStyleVar();
    }

    // Add a new feed by URL
    bool addFeed(const std::string& url) {
        try {
            // Use the RSSHost controller to add the feed
            return rss_host->addFeed(url);
        } catch (const std::exception&) {
            // Handle error (could show in UI)
            return false;
        }
    }
    
    // Get access to the RSS host controller (needed for other RSS card classes)
    static std::shared_ptr<hosts::RSSHost> getHost() {
        static std::mutex host_mutex;
        static std::weak_ptr<hosts::RSSHost> weak_host;
        
        RSS_DEBUG("Entering getHost(), acquiring lock...");
        std::lock_guard<std::mutex> lock(host_mutex);
        RSS_DEBUG("Lock acquired in getHost()");
        
        // Try to get a shared_ptr from the weak_ptr
        auto shared_host = weak_host.lock();
        
        // If the weak_ptr has expired or was never initialized, create a new instance
        if (!shared_host) {
            RSS_INFO("Creating new shared RSSHost instance");
            shared_host = std::make_shared<hosts::RSSHost>();
            weak_host = shared_host; // Store a weak_ptr, not keeping the object alive
            RSS_INFO("Shared RSSHost instance created");
        } else {
            RSS_DEBUG("Reusing existing shared RSSHost instance");
        }
        
        RSS_DEBUG("Exiting getHost()");
        return shared_host;
    }
    
    // Force a cache refresh for a specific feed or all feeds
    void invalidate_freshness_cache(std::string_view feed_url = "") {
        if (feed_url.empty()) {
            // Invalidate all cache entries by clearing the cache
            RSS_TRACE("Invalidating all freshness cache entries");
            freshness_cache.clear();
        } else {
            // Invalidate a specific feed's cache entry
            RSS_TRACE_FMT("Invalidating freshness cache for feed: {}", feed_url);
            freshness_cache.erase(std::string(feed_url));
        }
    }

    // Structure to hold AI search results
    struct AISearchResult {
        std::string title;
        std::string url;
        std::string description;
    };

    // Trigger AI search for RSS feeds based on a topic
    void triggerAIFeedSearch(const std::string& topic) {
        if (ai_search_in_progress_) {
            return; // Already searching
        }
        
        ai_search_in_progress_ = true;
        ai_search_results_.clear();
        
        // Start async AI search
        ai_search_future_ = std::async(std::launch::async, [this, topic]() {
            try {
                return performAIFeedSearch(topic);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("AI feed search failed: {}", e.what());
                return std::vector<AISearchResult>{};
            }
        });
    }
    
    // Perform the actual AI search
    std::vector<AISearchResult> performAIFeedSearch(const std::string& topic) {
        std::vector<AISearchResult> results;
        
        // Check if LLM is configured
        if (!helpers::LLMConfig::is_configured()) {
            RSS_ERROR("LLM not configured for AI feed search");
            return results;
        }
        
        // Create LLM instance
        auto llm_instance = helpers::LLMConfig::create_llm_instance();
        if (!llm_instance) {
            RSS_ERROR("Failed to create LLM instance for AI feed search");
            return results;
        }
        
        // Prepare the search prompt
        std::string search_prompt = std::format(
            "Find RSS feeds related to the topic: {}\n\n"
            "Please search the internet and provide a list of RSS feed URLs related to this topic. "
            "Include podcasts, news feeds, blogs, and other relevant RSS sources. "
            "For each feed, provide:\n"
            "1. The RSS feed URL\n"
            "2. A brief title/name\n"
            "3. A short description\n\n"
            "Format your response as a list where each entry is on a new line in this exact format:\n"
            "URL: [feed_url]\n"
            "TITLE: [feed_title]\n"
            "DESCRIPTION: [feed_description]\n"
            "---\n\n"
            "Only include feeds that are currently active and publicly accessible. "
            "Focus on high-quality, well-maintained feeds.", 
            topic
        );
        
        // Get current LLM settings
        auto settings = helpers::LLMConfig::get_current_config();
        
        try {
            // Create a fetcher instance for AI search
            auto fetcher = std::make_shared<http::fetch>();
            if (!fetcher) {
                RSS_ERROR("Failed to create fetcher for AI search");
                return results;
            }
            
            // Determine search mode (only for Grok)
            std::string search_mode_str;
            if (settings.provider == helpers::LLMConfig::Provider::GROK) {
                search_mode_str = "on"; // Enable internet search for Grok
            }
            
            // Send message to LLM
            auto response = llm_instance->sendMessage(
                search_prompt,
                [fetcher](const std::string& url, const std::string& data, auto header_client) {
                    return fetcher->post(url, data, header_client);
                },
                "user",
                settings.model_name,
                search_mode_str,
                0.7f // Use higher temperature for more creative search
            );
            
            // Parse the response
            if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                results = parseAIFeedResponse(response.choices[0].message.content);
            }
            
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Error during AI feed search: {}", e.what());
        }
        
        return results;
    }
    
    // Parse AI response to extract feed information
    std::vector<AISearchResult> parseAIFeedResponse(const std::string& response) {
        std::vector<AISearchResult> results;
        
        // Split response into entries separated by "---"
        std::regex entry_separator(R"(---\s*)");
        std::sregex_token_iterator iter(response.begin(), response.end(), entry_separator, -1);
        std::sregex_token_iterator end;
        
        for (; iter != end; ++iter) {
            std::string entry = iter->str();
            if (entry.empty()) continue;
            
            AISearchResult result;
            
            // Extract URL
            std::regex url_regex(R"(URL:\s*(.+))");
            std::smatch url_match;
            if (std::regex_search(entry, url_match, url_regex)) {
                result.url = url_match[1].str();
                // Trim whitespace
                result.url.erase(0, result.url.find_first_not_of(" \t\r\n"));
                result.url.erase(result.url.find_last_not_of(" \t\r\n") + 1);
            }
            
            // Extract title
            std::regex title_regex(R"(TITLE:\s*(.+))");
            std::smatch title_match;
            if (std::regex_search(entry, title_match, title_regex)) {
                result.title = title_match[1].str();
                // Trim whitespace
                result.title.erase(0, result.title.find_first_not_of(" \t\r\n"));
                result.title.erase(result.title.find_last_not_of(" \t\r\n") + 1);
            }
            
            // Extract description
            std::regex desc_regex(R"(DESCRIPTION:\s*(.+))");
            std::smatch desc_match;
            if (std::regex_search(entry, desc_match, desc_regex)) {
                result.description = desc_match[1].str();
                // Trim whitespace
                result.description.erase(0, result.description.find_first_not_of(" \t\r\n"));
                result.description.erase(result.description.find_last_not_of(" \t\r\n") + 1);
            }
            
            // Only add if we have at least a URL
            if (!result.url.empty()) {
                results.push_back(result);
            }
        }
        
        return results;
    }

private:
    // AI search state
    bool ai_search_in_progress_ = false;
    std::vector<AISearchResult> ai_search_results_;
    std::future<std::vector<AISearchResult>> ai_search_future_;
    
    std::string selected_tag_ = "All";
    
    // Cache for feed freshness colors to avoid recalculating every frame
    mutable std::unordered_map<std::string, std::pair<ImVec4, std::chrono::system_clock::time_point>> freshness_cache;
    static constexpr size_t MAX_CACHE_SIZE = 1000; // Limit cache size to prevent unbounded growth
    
    // Helper method to determine the freshness level of a feed based on most recent item
    // Optimized with caching to reduce CPU usage but forces refresh when requested
    ImVec4 get_freshness_color(const std::shared_ptr<media::rss::feed>& feed, 
                               const std::chrono::system_clock::time_point& now) const {
        // Check if feed has been modified since last cache update by comparing item count
        // or if we need to force a refresh based on time
        auto cache_it = freshness_cache.find(feed->source_link);
        bool should_refresh = true;
        
        if (cache_it != freshness_cache.end()) {
            // In normal cases, refresh cache every minute instead of every 5 minutes
            // to ensure UI feels responsive to feed updates
            auto cache_age = now - cache_it->second.second;
            if (std::chrono::duration_cast<std::chrono::minutes>(cache_age).count() < 1) {
                should_refresh = false;
            }
        }
        
        // If we don't need to refresh, return the cached color
        if (!should_refresh && cache_it != freshness_cache.end()) {
            return cache_it->second.first;
        }
        
        // Calculate freshness color
        ImVec4 color;
        
        if (feed->items.empty()) {
            color = colors[9]; // Old color for feeds with no items
        } else {
            // Find the newest item in the feed
            auto newest_time = std::chrono::system_clock::time_point::min();
            
            // In most RSS feeds items are already sorted by time (newest first)
            // so we first check if the front item is the newest
            if (!feed->items.empty()) {
                newest_time = feed->items.front().updated;
                
                // Only scan through the rest if we have more than one item
                if (feed->items.size() > 1) {
                    for (const auto& item : feed->items) {
                        if (item.updated > newest_time) {
                            newest_time = item.updated;
                        }
                    }
                }
            }
            
            // Calculate time difference
            auto diff = now - newest_time;
            auto hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
            
            // Determine color based on freshness thresholds
            if (hours < 1) {
                color = colors[6]; // Fresh - less than 1 hour old
            } else if (hours < 24) {
                color = colors[7]; // Recent - less than 1 day old
            } else if (hours < 72) {
                color = colors[8]; // Stale - less than 3 days old
            } else {
                color = colors[9]; // Old - more than 3 days old
            }
        }
        
        // Update cache with size management
        if (freshness_cache.size() >= MAX_CACHE_SIZE && cache_it == freshness_cache.end()) {
            // Cache is full and this is a new entry, remove oldest entry
            auto oldest_it = freshness_cache.begin();
            auto oldest_time = oldest_it->second.second;
            
            // Find the oldest entry
            for (auto it = freshness_cache.begin(); it != freshness_cache.end(); ++it) {
                if (it->second.second < oldest_time) {
                    oldest_time = it->second.second;
                    oldest_it = it;
                }
            }
            
            // Remove oldest entry
            freshness_cache.erase(oldest_it);
        }
        
        // Add or update entry
        freshness_cache[feed->source_link] = {color, now};
        return color;
    }

    std::shared_ptr<hosts::RSSHost> rss_host;
    std::vector<std::string> feeds_to_delete;

    SDL_Renderer* renderer = nullptr;
    std::shared_ptr<::helpers::ImageCache> image_cache;

    struct LoadedFeedTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, LoadedFeedTexture> feed_textures;

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
