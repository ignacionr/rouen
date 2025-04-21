#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>

#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
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
        
        // Initialize the RSS host controller with a system runner
        rss_host = std::make_unique<hosts::RSSHost>([](std::string_view cmd) -> std::string {
            // Simple system runner implementation
            return ""; // Not using system commands in this implementation
        });
    }
    
    ~rss() override = default;
    
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
            
            // Create scrollable area for feeds
            if (ImGui::BeginChild("FeedsScrollArea", ImVec2(0, 0), true)) {
                auto feeds = rss_host->feeds();
                if (feeds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds added yet");
                } else {
                    // Display all feeds
                    for (const auto& feed : feeds) {
                        ImGui::PushID(feed->source_link.c_str());
                        
                        ImGui::BeginGroup();
                        
                        // Feed title with truncation if needed
                        std::string title = feed->feed_title;
                        if (title.empty()) {
                            title = feed->source_link;
                        }
                        
                        // Limit title length and add ellipsis if too long
                        if (title.length() > 40) {
                            title = title.substr(0, 37) + "...";
                        }
                        
                        if (ImGui::Selectable(title.c_str(), false)) {
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
        static std::shared_ptr<hosts::RSSHost> shared_host = nullptr;
        
        if (!shared_host) {
            shared_host = std::make_shared<hosts::RSSHost>([](std::string_view cmd) -> std::string {
                return ""; // Not using system commands in this implementation
            });
        }
        
        return shared_host;
    }
    
private:
    std::unique_ptr<hosts::RSSHost> rss_host;
    std::vector<std::string> feeds_to_delete;
};

} // namespace rouen::cards