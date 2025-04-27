#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <iostream>

#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../hosts/rss_host.hpp"
#include "../../models/rss/feed.hpp"
#include "../../registrar.hpp"

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
        
        name("RSS Reader");
        requested_fps = 1;  // Update once per second
        width = 400.0f;
        
        // Use the shared host instance instead of creating a new one
        rss_host = getHost();
    }
    
    ~rss() override = default;

    std::string get_uri() const override
    {
        return "rss";
    }
    
    void render_add_feed() {
        static char url_buffer[512] = "";
        
        // Add a new feed section
        ImGui::TextColored(colors[2], "Add RSS Feed:");
        ImGui::PushItemWidth(-1);
        bool url_entered = ImGui::InputText("##url", url_buffer, sizeof(url_buffer), 
            ImGuiInputTextFlags_EnterReturnsTrue);
        
        // Show placeholder text when input is empty
        if (url_buffer[0] == '\0' && !ImGui::IsItemActive()) {
            auto pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + 5, pos.y + 2),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "Enter RSS feed URL..."
            );
        }
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        bool add_clicked = ImGui::Button("Add");
        
        if ((url_entered || add_clicked) && url_buffer[0] != '\0') {
            // Add the feed
            if (addFeed(url_buffer)) {
                // Clear the input field on success
                url_buffer[0] = '\0';
            }
        }
    }

    bool render() override {
        return render_window([this]() {
            // Feeds section title
            ImGui::TextColored(colors[2], "Your RSS Feeds:");
            
            // Search functionality
            static char search_buffer[256] = "";
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
            ImGui::PushItemWidth(-1);
            
            ImGui::InputText("##search", search_buffer, IM_ARRAYSIZE(search_buffer));
            
            // Show placeholder text when input is empty
            if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Search feeds... (Type to filter)"
                );
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(); // Pop FrameBg
            
            ImGui::Separator();
            
            // Create scrollable area for feeds
            if (ImGui::BeginChild("FeedsScrollArea", ImVec2(0, 0), true)) {
                auto feeds = rss_host->feeds();
                if (feeds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds added yet");
                } else {
                    std::string search_text = search_buffer;
                    bool has_matches = false;
                    
                    // Display matching feeds
                    for (const auto& feed : feeds) {
                        ImGui::PushID(feed->source_link.c_str());
                        
                        // Feed title with truncation if needed
                        std::string title = feed->feed_title;
                        if (title.empty()) {
                            title = feed->source_link;
                        }
                        
                        // Filter based on search query if search text is present
                        if (!search_text.empty() && 
                            !helpers::StringHelper::contains_case_insensitive(title, search_text) && 
                            !helpers::StringHelper::contains_case_insensitive(feed->source_link, search_text)) {
                            ImGui::PopID();
                            continue; // Skip items that don't match the search
                        }
                        
                        has_matches = true;
                        
                        ImGui::BeginGroup();
                        
                        // Limit title length and add ellipsis if too long
                        std::string display_title = title;
                        if (display_title.length() > 40) {
                            display_title = display_title.substr(0, 37) + "...";
                        }
                        
                        if (ImGui::Selectable(display_title.c_str(), false)) {
                            // Open feed items in a new card
                            std::string feed_uri = std::format("rss-feed:{}", feed->repo_id);
                            "create_card"_sfn(feed_uri);
                        }
                        
                        // Item count
                        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                        ImGui::TextColored(
                            ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                            "%zu items", feed->items.size()
                        );
                        
                        // Delete button
                        ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                        if (ImGui::SmallButton("×")) {
                            feeds_to_delete.push_back(feed->source_link);
                        }
                        
                        ImGui::EndGroup();
                        
                        ImGui::PopID();
                    }
                    
                    // Show message when no feeds match the search
                    if (!search_text.empty() && !has_matches) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                            "No feeds match your search");
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
        });
    }
    
    // Add a new feed by URL
    bool addFeed(const std::string& url) {
        try {
            // Use the RSSHost controller to add the feed
            return rss_host->addFeed(url);
        } catch (const std::exception& e) {
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
    
private:
    std::shared_ptr<hosts::RSSHost> rss_host;
    std::vector<std::string> feeds_to_delete;
};

} // namespace rouen::cards