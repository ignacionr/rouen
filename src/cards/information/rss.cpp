#include "rss.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <future>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../helpers/tag_manager.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../../helpers/debug.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../models/rss/rss_date_parser.hpp"
#include "../../registrar.hpp"
#include "image_cache.hpp"
#include "llm_host.hpp"
#include "models/rss/feed.hpp"
#include "rss_host.hpp"
#include "sdl_compat.hpp"

namespace rouen::cards {

rss::rss() {
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
    width = 835.0f;
    
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

rss::~rss() {
    on_close();
    clear_feed_textures();
}

void rss::on_close() {
    media_player::stopForOwner(this);
    rouen::platform::stop_speech();
}

void rss::clear_feed_textures() {
    for (auto& [url, lt] : feed_textures) {
        if (lt.texture) {
            SDL_DestroyTexture(lt.texture);
        }
    }
    feed_textures.clear();
}

void rss::set_renderer(SDL_Renderer* r) {
    renderer = r;
    if (renderer && !image_cache) {
        auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
        auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();
        image_cache = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
    }
}

std::string rss::get_uri() const {
    return "rss";
}

void rss::render_add_feed() {
    static char url_buffer[512] = "";
    
    // Add a new feed section
    ImGui::TextColored(colors[0], "Add RSS Feed:");
    
    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float const add_btn_w = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float const ai_btn_w = ImGui::CalcTextSize("AI Search").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float const input_w = ImGui::GetContentRegionAvail().x - add_btn_w - ai_btn_w - ImGui::GetStyle().ItemSpacing.x * 2.0f - 8.0f * dpi_scale;
    
    ImGui::PushItemWidth(input_w);
    bool const url_entered = ImGui::InputText("##url", url_buffer, sizeof(url_buffer), 
        ImGuiInputTextFlags_EnterReturnsTrue);
    
    // Check for Cmd+Enter (macOS) or Ctrl+Enter (Windows/Linux) to trigger AI search
    bool ai_search_requested = false;
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        ImGuiIO const& io = ImGui::GetIO();
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
    bool const add_clicked = ImGui::Button("Add");
    
    ImGui::SameLine();
    bool const ai_search_clicked = ImGui::Button("AI Search");
    
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

bool rss::render() {
    if (image_downloaded_signal_.exchange(false)) {
        for (auto it = feed_textures.begin(); it != feed_textures.end(); ) {
            if (it->second.status == TextureStatus::Pending) {
                it = feed_textures.erase(it);
            } else {
                ++it;
            }
        }
    }

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
    // We do this based on time elapsed (every 2 minutes) rather than frame count
    // to handle variable frame rates gracefully
    auto now_steady = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::minutes>(now_steady - last_freshness_cache_invalidation_time_).count() >= 2) {
        invalidate_freshness_cache();
        last_freshness_cache_invalidation_time_ = now_steady;
    }
    
    return render_window([this] {
        // Draw thin progress line on top of the window content
        {
            auto last_refresh = rss_host->last_refresh_time();
            auto interval = rss_host->refresh_interval_s();
            auto const now {std::chrono::system_clock::now()};
            
            auto elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh).count()) / 1000.0;
            float ratio = std::clamp(static_cast<float>(elapsed / interval), 0.0f, 1.0f);
            
            ImVec2 const pos = ImGui::GetCursorScreenPos();
            float const bar_width = ImGui::GetContentRegionAvail().x;
            float const height = 2.0f;
            
            ImVec2 const p_min = pos;
            ImVec2 const p_max = ImVec2(pos.x + bar_width * ratio, pos.y + height);
            
            ImGui::GetWindowDrawList()->AddRectFilled(
                p_min, 
                ImVec2(pos.x + bar_width, pos.y + height), 
                ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.3f))
            );
            ImGui::GetWindowDrawList()->AddRectFilled(
                p_min, 
                p_max, 
                ImGui::GetColorU32(colors[0])
            );
            
            // Invisible button to capture hover for tooltip & double click to force refresh
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton("##refresh_progress_bar", ImVec2(bar_width, height + 4.0f));
            if (ImGui::IsItemHovered()) {
                int const seconds_left = std::max(0, static_cast<int>(interval - elapsed));
                int const minutes = seconds_left / 60;
                int const seconds = seconds_left % 60;
                ImGui::SetTooltip("Next auto-refresh in %02d:%02d\nDouble-click to refresh now", minutes, seconds);
                
                if (ImGui::IsMouseDoubleClicked(0)) {
                    rss_host->trigger_manual_refresh();
                }
            }
            
            ImGui::Dummy(ImVec2(0.0f, height + 4.0f));
        }

        // Periodically refresh cached tags, smart lists, and feed references (every 20 seconds) to avoid per-frame SQLite queries
        auto const cache_now {std::chrono::steady_clock::now()};
        if (cached_all_feeds_.empty() || std::chrono::duration_cast<std::chrono::seconds>(cache_now - last_gallery_cache_update_time_).count() >= 20) {
            last_gallery_cache_update_time_ = cache_now;
            cached_all_feeds_ = rss_host->feeds();
            cached_smart_lists_ = rss_host->get_smart_lists();
            
            std::vector<std::string> avail_tags = rouen::helpers::tag_manager::get().get_available_tags();
            std::unordered_map<std::string, int> tag_counts;
            for (const auto& feed : cached_all_feeds_) {
                if (feed) {
                    for (const auto& tag : feed->tags) {
                        tag_counts[tag]++;
                    }
                }
            }
            
            std::sort(avail_tags.begin(), avail_tags.end(), [&tag_counts](const std::string& a, const std::string& b) {
                int const count_a = tag_counts.contains(a) ? tag_counts.at(a) : 0;
                int const count_b = tag_counts.contains(b) ? tag_counts.at(b) : 0;
                if (count_a != count_b) {
                    return count_a > count_b;
                }
                return a < b;
            });
            
            avail_tags.insert(avail_tags.begin(), "All");
            cached_gallery_tags_ = std::move(avail_tags);
        }

        // Tag Filter Pills (with one-line truncation and toggleable expanded view)
        {
            const auto& tags = cached_gallery_tags_;
            if (std::find(tags.begin(), tags.end(), selected_tag_) == tags.end()) {
                selected_tag_ = "All";
            }
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f); // Pill-shaped!
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
            
            const float tags_row_width = ImGui::GetContentRegionAvail().x;
            float used_row_width = 0.0f;
            const float tag_spacing = ImGui::GetStyle().ItemSpacing.x;
            const float dots_pill_width = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            
            bool needs_toggle_pill = false;
            size_t render_count = tags.size();
            
            if (!show_all_tags_) {
                // Check if all tags fit on a single line
                float total_tags_width = 0.0f;
                for (size_t i = 0; i < tags.size(); ++i) {
                    const float pill_w = ImGui::CalcTextSize(tags[i].c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    total_tags_width += (i == 0 ? 0.0f : tag_spacing) + pill_w;
                }
                
                if (total_tags_width > tags_row_width) {
                    needs_toggle_pill = true;
                    // Find how many tags fit on line 1 alongside the "..." pill
                    float accum_width = 0.0f;
                    size_t fit_count = 0;
                    for (size_t i = 0; i < tags.size(); ++i) {
                        const float pill_w = ImGui::CalcTextSize(tags[i].c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        const float candidate_w = (i == 0 ? pill_w : accum_width + tag_spacing + pill_w);
                        if (candidate_w + tag_spacing + dots_pill_width <= tags_row_width) {
                            accum_width = candidate_w;
                            fit_count = i + 1;
                        } else {
                            break;
                        }
                    }
                    render_count = std::max<size_t>(1, fit_count);
                }
            } else {
                needs_toggle_pill = true; // In expanded mode, show "..." pill to allow collapsing back
            }

            for (size_t i = 0; i < render_count; ++i) {
                const auto& tag = tags[i];
                bool const is_selected = (selected_tag_ == tag);
                const bool is_top_fresh = (tag != "All" && top_fresh_tags_.contains(tag));
                
                const float pill_width =
                    ImGui::CalcTextSize(tag.c_str()).x +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                
                const bool fits_same_line =
                    i > 0 &&
                    (used_row_width + tag_spacing + pill_width <= tags_row_width);
                
                if (fits_same_line) {
                    ImGui::SameLine(0.0f, tag_spacing);
                    used_row_width += tag_spacing + pill_width;
                } else {
                    used_row_width = pill_width;
                }
                
                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]); // Primary theme color
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x * 1.1f, colors[0].y * 1.1f, colors[0].z * 1.1f, colors[0].w));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x * 0.9f, colors[0].y * 0.9f, colors[0].z * 0.9f, colors[0].w));
                } else if (is_top_fresh) {
                    // Distinctive styling for top 4 fresh tags: a warm rose/gold tone matching theme colors
                    ImVec4 const fresh_base = ImVec4(colors[0].x * 0.35f, colors[0].y * 0.15f, colors[0].z * 0.15f, 0.65f);
                    ImVec4 const fresh_hover = ImVec4(colors[0].x * 0.5f, colors[0].y * 0.22f, colors[0].z * 0.22f, 0.85f);
                    ImVec4 const fresh_active = ImVec4(colors[0].x * 0.3f, colors[0].y * 0.12f, colors[0].z * 0.12f, 0.95f);
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, fresh_base);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fresh_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, fresh_active);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.15f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.2f, 0.9f));
                }
                
                if (ImGui::Button(tag.c_str())) {
                    selected_tag_ = tag;
                }
                
                ImGui::PopStyleColor(3);
            }

            // Render "..." toggle pill if needed
            if (needs_toggle_pill) {
                const bool fits_same_line = (used_row_width > 0.0f) && (used_row_width + tag_spacing + dots_pill_width <= tags_row_width);
                if (fits_same_line) {
                    ImGui::SameLine(0.0f, tag_spacing);
                    used_row_width += tag_spacing + dots_pill_width;
                } else {
                    used_row_width = dots_pill_width;
                }
                
                // Check if selected tag is currently hidden in collapsed view
                bool hidden_tag_selected = false;
                if (!show_all_tags_) {
                    for (size_t i = render_count; i < tags.size(); ++i) {
                        if (tags[i] == selected_tag_) {
                            hidden_tag_selected = true;
                            break;
                        }
                    }
                }
                
                if (show_all_tags_ || hidden_tag_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors[0].x * 0.4f, colors[0].y * 0.4f, colors[0].z * 0.4f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x * 0.6f, colors[0].y * 0.6f, colors[0].z * 0.6f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[0].x * 0.8f, colors[0].y * 0.8f, colors[0].z * 0.8f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.35f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.22f, 0.28f, 0.95f));
                }

                if (ImGui::Button("...##toggle_tags")) {
                    show_all_tags_ = !show_all_tags_;
                }
                if (ImGui::IsItemHovered()) {
                    if (hidden_tag_selected) {
                        ImGui::SetTooltip("Filtered by '%s' (click to %s tag list)", selected_tag_.c_str(), show_all_tags_ ? "collapse" : "expand");
                    } else {
                        ImGui::SetTooltip("%s tag list", show_all_tags_ ? "Collapse" : "Show all");
                    }
                }
                ImGui::PopStyleColor(3);
            }

            // Action button to clean/delete empty unused RSS tags (show if expanded or if it fits on line 1)
            const float clean_btn_width = ImGui::CalcTextSize(ICON_MD_DELETE_SWEEP " Clean Unused Tags").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const bool clean_fits = (used_row_width + tag_spacing + clean_btn_width <= tags_row_width);
            if (show_all_tags_ || clean_fits) {
                if (clean_fits) {
                    ImGui::SameLine(0.0f, tag_spacing);
                }
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.15f, 0.15f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.2f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.25f, 0.25f, 0.9f));
                
                if (ImGui::Button(ICON_MD_DELETE_SWEEP " Clean Unused Tags")) {
                    if (rss_host) {
                        int count = rss_host->delete_unused_tags();
                        last_gallery_cache_update_time_ = std::chrono::steady_clock::time_point{};
                        cached_all_feeds_.clear();
                        status_message_ = std::format("Deleted {} unused tag(s)", count);
                        status_message_time_ = std::chrono::steady_clock::now();
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Delete all empty RSS tags not assigned to any feed");
                }
                ImGui::PopStyleColor(3);
            }

            ImGui::PopStyleVar(2);
            ImGui::Spacing();

            if (!status_message_.empty()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - status_message_time_).count();
                if (elapsed < 4) {
                    ImGui::TextColored(colors[3], "%s", status_message_.c_str());
                    ImGui::Spacing();
                } else {
                    status_message_.clear();
                }
            }
        }


        // Smart Lists section
        {
            const auto& smart_lists = cached_smart_lists_;
            if (!smart_lists.empty()) {
                ImGui::TextColored(colors[0], "Your Smart Lists:");
                ImGui::Spacing();
                
                const float sl_row_width = ImGui::GetContentRegionAvail().x;
                float used_sl_width = 0.0f;
                const float sl_spacing = ImGui::GetStyle().ItemSpacing.x;
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
                
                for (size_t i = 0; i < smart_lists.size(); ++i) {
                    const auto& sl = smart_lists[i];
                    std::string label = ICON_MD_FILTER_LIST " " + sl.title;
                    float const btn_width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    
                    const bool fits_same_line =
                        i > 0 &&
                        (used_sl_width + sl_spacing + btn_width <= sl_row_width);
                    
                    if (fits_same_line) {
                        ImGui::SameLine(0.0f, sl_spacing);
                        used_sl_width += sl_spacing + btn_width;
                    } else {
                        used_sl_width = btn_width;
                    }
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.3f, 0.4f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.4f, 0.3f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.4f, 0.3f, 0.9f));
                    
                    std::string const btn_id = std::format("{}##smart_list_btn_{}", label, sl.title);
                    if (ImGui::Button(btn_id.c_str())) {
                        "create_card"_sfn("rss-smart-list:" + sl.title);
                    }
                    
                    ImGui::PopStyleColor(3);
                }
                ImGui::PopStyleVar(2);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        // Feeds section title
        ImGui::TextColored(colors[0], "Your RSS Feeds:");
        
        // Search functionality
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
        
        // Calculate width for input field to leave space for clear button
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        float const clear_button_width = 20.0f * dpi_scale;
        float const input_width = ImGui::GetContentRegionAvail().x - clear_button_width - ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushItemWidth(input_width);
        
        bool const changed = ImGui::InputText("##search", search_buffer_, static_cast<int>(sizeof(search_buffer_)));
        if (changed) {
            last_search_type_time_ = std::chrono::system_clock::now();
            search_pending_ = true;
        }
        
        // Apply search immediately if Enter is pressed
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            debounced_search_query_ = search_buffer_;
            search_pending_ = false;
        }
        
        // Debounce logic: update query after 1 second of inactivity
        if (search_pending_) {
            auto elapsed = std::chrono::system_clock::now() - last_search_type_time_;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= 1000) {
                debounced_search_query_ = search_buffer_;
                search_pending_ = false;
            }
        }
        
        // Handle ESC key to clear search while maintaining focus
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            search_buffer_[0] = '\0';
            debounced_search_query_ = "";
            search_pending_ = false;
        }

        // for the placeholder
        auto pos = ImGui::GetItemRectMin();
        
        // Clear button (soft X)
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            search_buffer_[0] = '\0'; // Clear the search buffer
            debounced_search_query_ = "";
            search_pending_ = false;
            ImGui::SetKeyboardFocusHere(-1); // Focus the previous item (the InputText)
        }
        
        // Show placeholder text when input is empty
        if (search_buffer_[0] == '\0' && !ImGui::IsItemActive()) {
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
        float const bottom_margin = (settings_open ? 210.0f : 120.0f) * dpi_scale;
        
        // Create scrollable area for feeds
        auto available_size = ImGui::GetContentRegionAvail();
        ImVec2 const scroll_area_size = ImVec2(available_size.x, available_size.y - bottom_margin);
        if (ImGui::BeginChild("FeedsScrollArea", scroll_area_size, false, ImGuiWindowFlags_NavFlattened)) {
            const auto& all_feeds = cached_all_feeds_;
            
            // Check if we need to re-filter and re-sort feeds
            bool needs_update = false;
            if (selected_tag_ != last_selected_tag_ || 
                all_feeds.size() != last_all_feeds_size_ || 
                cached_feeds_.empty()) 
            {
                needs_update = true;
            } else {
                for (const auto& f : all_feeds) {
                    auto item_count = f->items.size();
                    auto latest_time = f->items.empty() ? std::chrono::system_clock::time_point::min() : f->items.front().updated;
                    
                    auto it = feed_states_.find(f->source_link);
                    if (it == feed_states_.end() || 
                        it->second.item_count != item_count || 
                        it->second.latest_item_time != latest_time ||
                        it->second.tags != f->tags) 
                    {
                        needs_update = true;
                        break;
                    }
                }
            }
            
            if (needs_update) {
                search_results_dirty_ = true;
                cached_feeds_.clear();
                if (selected_tag_ == "All") {
                    cached_feeds_ = all_feeds;
                } else {
                    for (const auto& feed : all_feeds) {
                        if (feed->tags.contains(selected_tag_)) {
                            cached_feeds_.push_back(feed);
                        }
                    }
                }
                
                std::sort(cached_feeds_.begin(), cached_feeds_.end(), [](const auto& a, const auto& b) {
                    auto get_freshness = [](const auto& f) {
                        if (f->items.empty()) {
                            return std::chrono::system_clock::time_point::min();
                        }
                        auto newest = f->items.front().updated;
                        for (const auto& item : f->items) {
                            if (item.updated > newest) {
                                newest = item.updated;
                            }
                        }
                        return newest;
                    };
                    auto freshness_a = get_freshness(a);
                    auto freshness_b = get_freshness(b);
                    if (freshness_a != freshness_b) {
                        return freshness_a > freshness_b;
                    }
                    std::string const title_a = a->feed_title.empty() ? a->source_link : a->feed_title;
                    std::string const title_b = b->feed_title.empty() ? b->source_link : b->feed_title;
                    return title_a < title_b;
                });
                
                // Calculate top 4 fresh tags (excluding "All")
                std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> tag_freshness;
                std::vector<std::string> const all_possible_tags = rouen::helpers::tag_manager::get().get_available_tags();
                
                for (const auto& tag : all_possible_tags) {
                    if (tag == "All") continue;
                    
                    auto tag_newest = std::chrono::system_clock::time_point::min();
                    for (const auto& feed : all_feeds) {
                        if (feed->tags.contains(tag)) {
                            for (const auto& item : feed->items) {
                                if (item.updated > tag_newest) {
                                    tag_newest = item.updated;
                                }
                            }
                        }
                    }
                    if (tag_newest != std::chrono::system_clock::time_point::min()) {
                        tag_freshness.push_back({tag, tag_newest});
                    }
                }
                
                std::sort(tag_freshness.begin(), tag_freshness.end(), [](const auto& a, const auto& b) {
                    return a.second > b.second;
                });
                
                top_fresh_tags_.clear();
                for (size_t t = 0; t < std::min(static_cast<size_t>(4), tag_freshness.size()); ++t) {
                    top_fresh_tags_.insert(tag_freshness[t].first);
                }
                
                // Update cache tracking state
                feed_states_.clear();
                for (const auto& f : all_feeds) {
                    auto item_count = f->items.size();
                    auto latest_time = f->items.empty() ? std::chrono::system_clock::time_point::min() : f->items.front().updated;
                    feed_states_[f->source_link] = FeedCacheState{item_count, latest_time, f->tags};
                }
                last_selected_tag_ = selected_tag_;
                last_all_feeds_size_ = all_feeds.size();
            }
            
            const auto& feeds = cached_feeds_;

            if (feeds.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds in this category");
            } else {
                std::string search_text = debounced_search_query_;
                if (search_text.empty()) {
                    bool has_matches = false;
                    render_feed_list(feeds, search_text, has_matches);
                } else {
                    // Re-run deep search only if query, tag, or feed cache changed
                    if (search_text != cached_search_query_ ||
                        selected_tag_ != cached_search_tag_ ||
                        search_results_dirty_)
                    {
                        cached_matching_feeds_.clear();
                        for (const auto& feed : feeds) {
                            std::string const title = feed->feed_title.empty() ? feed->source_link : feed->feed_title;
                            if (::helpers::StringHelper::contains_case_insensitive(title, search_text) ||
                                ::helpers::StringHelper::contains_case_insensitive(feed->source_link, search_text)) {
                                cached_matching_feeds_.push_back(feed);
                            }
                        }

                        auto raw_matching_items = rss_host->search_items(search_text);
                        cached_matching_items_.clear();
                        if (selected_tag_ == "All") {
                            cached_matching_items_ = std::move(raw_matching_items);
                        } else {
                            std::set<long long> active_ids;
                            for (const auto& f : feeds) {
                                active_ids.insert(f->repo_id);
                            }
                            for (auto& item : raw_matching_items) {
                                if (active_ids.contains(item.feed_id)) {
                                    cached_matching_items_.push_back(std::move(item));
                                }
                            }
                        }

                        cached_search_query_ = search_text;
                        cached_search_tag_ = selected_tag_;
                        search_results_dirty_ = false;
                    }

                    if (!cached_matching_feeds_.empty()) {
                        ImGui::TextColored(colors[0], "Matching Feeds (%d):", static_cast<int>(cached_matching_feeds_.size()));
                        ImGui::Spacing();
                        bool has_matches = false;
                        render_feed_list(cached_matching_feeds_, search_text, has_matches);
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    // 2. Deep Search matching Articles within category
                    ImGui::TextColored(colors[0], "Matching Articles (%d):", static_cast<int>(cached_matching_items_.size()));
                    ImGui::Spacing();

                    if (cached_matching_items_.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No articles found matching your search");
                    } else {
                        for (size_t i = 0; i < cached_matching_items_.size(); ++i) {
                            const auto& item = cached_matching_items_[i];
                            ImGui::PushID(std::format("{}##{}", item.link, i).c_str());

                            try {
                                ImGui::BeginGroup();
                                try {
                                    // Try to load item thumbnail
                                    SDL_Texture* item_tex = nullptr;
                                    int item_tex_w = 0, item_tex_h = 0;
                                    if (renderer && image_cache && !item.image_url.empty()) {
                                        item_tex = get_feed_texture(item.image_url, ::helpers::ImageCache::Variant::Color, item_tex_w, item_tex_h);
                                    }

                                    bool const has_item_image = (item_tex != nullptr);
                                    float const avail_width = ImGui::GetContentRegionAvail().x;

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
                                        std::string const item_uri = std::format("rss-item:{}|||{}|||{}", item.feed_id, item.link, item.title);
                                        "create_card"_sfn(item_uri);
                                    }

                                    // Date (with Age)
                                    auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                                    std::tm* tm = std::localtime(&time);
                                    char date_str[64];
                                    std::strftime(date_str, sizeof(date_str), "%d %b %Y %H:%M", tm);
                                    std::string const age_str = media::rss::format_rss_age(item.publish_date);
                                    ImGui::TextColored(colors[1], "%s (%s)", date_str, age_str.c_str());

                                    // Description
                                    if (!item.description.empty()) {
                                        std::string desc = ::helpers::StringHelper::strip_html_tags(item.description);
                                        if (desc.length() > 100) {
                                            desc = desc.substr(0, 97) + "...";
                                        }
                                        ImGui::TextWrapped("%s", desc.c_str());
                                    }

                                    ImGui::PopTextWrapPos();

                                    if (has_item_image) {
                                        ImGui::EndGroup();
                                        ImGui::SameLine(avail_width - 120.0f);

                                        ImVec2 const thumb_size(120.0f, 80.0f);
                                        ImVec2 const thumb_pos = ImGui::GetCursorScreenPos();

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
                                            std::string const item_uri = std::format("rss-item:{}|||{}|||{}", item.feed_id, item.link, item.title);
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

std::string rss::truncate_text(const std::string& text, float max_width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
    std::string truncated = text;
    while (!truncated.empty() && ImGui::CalcTextSize((truncated + "...").c_str()).x > max_width) {
        truncated.pop_back();
    }
    return truncated + "...";
}

void rss::calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1) {
    if (tex_w <= 0.0f || tex_h <= 0.0f || target_w <= 0.0f || target_h <= 0.0f) {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
        return;
    }
    float const target_aspect = target_w / target_h;
    float const tex_aspect = tex_w / tex_h;

    if (tex_aspect > target_aspect) {
        // Texture is wider than target, crop sides
        float const f = target_aspect / tex_aspect;
        float const c = (1.0f - f) * 0.5f;
        uv0 = ImVec2(c, 0.0f);
        uv1 = ImVec2(1.0f - c, 1.0f);
    } else {
        // Texture is taller than target, crop top/bottom
        float const f = tex_aspect / target_aspect;
        float const c = (1.0f - f) * 0.5f;
        uv0 = ImVec2(0.0f, c);
        uv1 = ImVec2(1.0f, 1.0f - c);
    }
}

std::string rss::feed_texture_cache_key(const std::string& url, ::helpers::ImageCache::Variant variant) {
    return variant == ::helpers::ImageCache::Variant::Grayscale ? url + "#grayscale" : url;
}

SDL_Texture* rss::get_feed_texture(const std::string& url, ::helpers::ImageCache::Variant variant, int& texture_width, int& texture_height) {
    texture_width = 0;
    texture_height = 0;

    if (!renderer || !image_cache || url.empty()) {
        return nullptr;
    }

    const auto cache_key = feed_texture_cache_key(url, variant);
    auto it = feed_textures.find(cache_key);
    if (it != feed_textures.end()) {
        if (it->second.status == TextureStatus::Loaded) {
            texture_width = it->second.width;
            texture_height = it->second.height;
            return it->second.texture;
        }
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> const lock(downloading_mutex_);
        if (failed_downloads_.contains(url)) {
            feed_textures[cache_key] = {nullptr, 0, 0, TextureStatus::Failed};
            return nullptr;
        }
    }

    int cached_w = 0, cached_h = 0;
    if (image_cache->isCached(url, cached_w, cached_h, variant)) {
        SDL_Texture* texture = image_cache->getTexture(renderer, url, texture_width, texture_height, false, variant);
        if (texture) {
            feed_textures[cache_key] = {texture, texture_width, texture_height, TextureStatus::Loaded};
            return texture;
        }
        feed_textures[cache_key] = {nullptr, 0, 0, TextureStatus::Failed};
        return nullptr;
    }

    feed_textures[cache_key] = {nullptr, 0, 0, TextureStatus::Pending};
    request_image_download(url);
    return nullptr;
}

void rss::render_feed_list(const std::vector<std::shared_ptr<media::rss::feed>>& feeds, std::string& search_text, bool& has_matches) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    auto now = std::chrono::system_clock::now();

    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float const card_width = 180.0f * dpi_scale;
    float const card_height = 290.0f * dpi_scale;
    float const spacing = 12.0f * dpi_scale;
    float const avail_width = ImGui::GetContentRegionAvail().x;
    int const cols = std::max(1, static_cast<int>(avail_width / (card_width + spacing)));
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    
    int visible_index = 0;
    for (const auto &feed : feeds)
    {
        ImGui::PushID(feed->source_link.c_str());

        std::string const title = feed->feed_title.empty() ? feed->source_link : feed->feed_title;

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

        ImVec2 const start_pos = ImGui::GetCursorScreenPos();
        ImVec2 const end_pos = ImVec2(start_pos.x + card_width, start_pos.y + card_height);

        ImGui::SetCursorScreenPos(start_pos);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        bool const card_activated = ImGui::Selectable(
            std::format("##feed_card_nav_{}", feed->repo_id).c_str(),
            false,
            ImGuiSelectableFlags_AllowOverlap,
            ImVec2(card_width, card_height)
        );
        bool const is_emphasized = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        if (is_emphasized) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::PopStyleColor(3);
        
        // Draw background
        ImVec4 const bg_color = is_emphasized ? ImVec4(0.22f, 0.22f, 0.26f, 0.8f) : ImVec4(0.14f, 0.14f, 0.17f, 0.6f);
        
        draw_list->AddRectFilled(start_pos, end_pos, ImGui::GetColorU32(bg_color), 8.0f * dpi_scale);
        
        // Padding
        ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f * dpi_scale, start_pos.y + 6.0f * dpi_scale));
        ImGui::BeginGroup();
        
        // 1. Draw Image / Placeholder
        ImVec2 const img_size(card_width - 12.0f * dpi_scale, 220.0f * dpi_scale);
        ImVec2 const img_pos = ImGui::GetCursorScreenPos();
        
        // Invisible button to capture click on cover
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * dpi_scale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.05f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.1f));
        bool const cover_clicked = ImGui::Button(std::format("##cover_btn_{}", feed->repo_id).c_str(), img_size);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        
        // Render the actual image or placeholder on the draw list
        std::string const img_url = feed->image_url();
        SDL_Texture* tex = nullptr;
        int img_w = 0, img_h = 0;
        if (renderer && image_cache && !img_url.empty()) {
            tex = get_feed_texture(
                img_url,
                is_emphasized ? ::helpers::ImageCache::Variant::Color : ::helpers::ImageCache::Variant::Grayscale,
                img_w,
                img_h
            );
        }
        
        if (tex) {
            // Draw texture in the image region, cropped to fit the aspect ratio
            ImVec2 uv0, uv1;
            calculate_cover_uvs(img_size.x, img_size.y, static_cast<float>(img_w), static_cast<float>(img_h), uv0, uv1);
            draw_list->AddImage(rouen::helpers::texture_id_cast(tex), img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), uv0, uv1);
        } else {
            // Draw placeholder
            draw_list->AddRectFilled(img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.5f)), 4.0f * dpi_scale);
            std::string const placeholder_icon = ICON_MD_RSS_FEED;
            ImVec2 const icon_size = ImGui::CalcTextSize(placeholder_icon.c_str());
            ImVec2 const icon_pos = ImVec2(img_pos.x + (img_size.x - icon_size.x) * 0.5f, img_pos.y + (img_size.y - icon_size.y) * 0.5f);
            draw_list->AddText(icon_pos, ImGui::GetColorU32(colors[1]), placeholder_icon.c_str());
        }
        
        // 2. Draw Title with proper spacing
        ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f * dpi_scale, start_pos.y + 230.0f * dpi_scale));
        const ImVec4 text_color = is_emphasized ? colors[0] : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::PushTextWrapPos(start_pos.x + card_width - 6.0f * dpi_scale);
        ImGui::TextColored(text_color, "%s", title.c_str());
        ImGui::PopTextWrapPos();
        
        // 3. Draw freshness right after title
        std::string const freshness_text = get_freshness_text(feed, now);
        ImGui::TextColored(text_color, "●");
        ImGui::SameLine();
        ImGui::TextColored(text_color, "%s", freshness_text.c_str());
        
        bool play_clicked = false;
        
        ImGui::SameLine(card_width - 54.0f * dpi_scale);
        if (ImGui::SmallButton(std::format("{}##play_{}", ICON_MD_PLAY_ARROW, feed->repo_id).c_str())) {
            play_clicked = true;
        }
        
        ImGui::EndGroup();
        
        if ((card_activated || cover_clicked) && !play_clicked) {
            std::string const feed_uri = std::format("rss-feed:{}", feed->repo_id);
            "create_card"_sfn(feed_uri);
        }
        if (play_clicked) {
            std::string const feed_uri = std::format("rss-feed:{}:play", feed->repo_id);
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

bool rss::addFeed(const std::string& url) {
    try {
        return rss_host->add_feed(url);
    } catch (const std::exception&) {
        return false;
    }
}

std::shared_ptr<hosts::RSSHost> rss::getHost() {
    static std::mutex host_mutex;
    static std::shared_ptr<hosts::RSSHost> instance;
    std::lock_guard<std::mutex> const lock(host_mutex);
    if (!instance) {
        RSS_INFO("Creating persistent shared RSSHost instance");
        instance = std::make_shared<hosts::RSSHost>();
    }
    return instance;
}

void rss::invalidate_freshness_cache(std::string_view feed_url) {
    if (feed_url.empty()) {
        RSS_TRACE("Invalidating all freshness cache entries");
        freshness_cache.clear();
    } else {
        RSS_TRACE_FMT("Invalidating freshness cache for feed: {}", feed_url);
        freshness_cache.erase(std::string(feed_url));
    }
}

void rss::triggerAIFeedSearch(const std::string& topic) {
    if (ai_search_in_progress_) {
        return;
    }
    
    ai_search_in_progress_ = true;
    ai_search_results_.clear();
    
    ai_search_future_ = std::async(std::launch::async, [this, topic]() {
        try {
            return performAIFeedSearch(topic);
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("AI feed search failed: {}", e.what());
            return std::vector<AISearchResult>{};
        }
    });
}

std::vector<rss::AISearchResult> rss::performAIFeedSearch(const std::string& topic) {
    std::vector<AISearchResult> results;
    
    if (!helpers::LLMConfig::is_configured()) {
        RSS_ERROR("LLM not configured for AI feed search");
        return results;
    }
    
    auto llm_instance = helpers::LLMConfig::create_llm_instance();
    if (!llm_instance) {
        RSS_ERROR("Failed to create LLM instance for AI feed search");
        return results;
    }
    
    std::string const search_prompt = std::format(
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
    
    auto settings = helpers::LLMConfig::get_current_config();
    
    try {
        auto fetcher = std::make_shared<http::fetch>();
        if (!fetcher) {
            RSS_ERROR("Failed to create fetcher for AI search");
            return results;
        }
        
        std::string search_mode_str;
        if (settings.provider == helpers::LLMConfig::Provider::GROK) {
            search_mode_str = "on";
        }
        
        auto response = llm_instance->sendMessage(
            search_prompt,
            [fetcher](const std::string& url, const std::string& data, auto header_client) {
                return fetcher->post(url, data, header_client);
            },
            "user",
            settings.model_name,
            search_mode_str,
            0.7f
        );
        
        if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
            results = parseAIFeedResponse(response.choices[0].message.content);
        }
        
    } catch (const std::exception& e) {
        RSS_ERROR_FMT("Error during AI feed search: {}", e.what());
    }
    
    return results;
}

std::vector<rss::AISearchResult> rss::parseAIFeedResponse(const std::string& response) {
    std::vector<AISearchResult> results;
    
    std::regex const entry_separator(R"(---\s*)");
    std::sregex_token_iterator iter(response.begin(), response.end(), entry_separator, -1);
    std::sregex_token_iterator const end;
    
    for (; iter != end; ++iter) {
        std::string const entry = iter->str();
        if (entry.empty()) continue;
        
        AISearchResult result;
        
        std::regex const url_regex(R"(URL:\s*(.+))");
        std::smatch url_match;
        if (std::regex_search(entry, url_match, url_regex)) {
            result.url = url_match[1].str();
            result.url.erase(0, result.url.find_first_not_of(" \t\r\n"));
            result.url.erase(result.url.find_last_not_of(" \t\r\n") + 1);
        }
        
        std::regex const title_regex(R"(TITLE:\s*(.+))");
        std::smatch title_match;
        if (std::regex_search(entry, title_match, title_regex)) {
            result.title = title_match[1].str();
            result.title.erase(0, result.title.find_first_not_of(" \t\r\n"));
            result.title.erase(result.title.find_last_not_of(" \t\r\n") + 1);
        }
        
        std::regex const desc_regex(R"(DESCRIPTION:\s*(.+))");
        std::smatch desc_match;
        if (std::regex_search(entry, desc_match, desc_regex)) {
            result.description = desc_match[1].str();
            result.description.erase(0, result.description.find_first_not_of(" \t\r\n"));
            result.description.erase(result.description.find_last_not_of(" \t\r\n") + 1);
        }
        
        if (!result.url.empty()) {
            results.push_back(result);
        }
    }
    
    return results;
}

ImVec4 rss::get_freshness_color(const std::shared_ptr<media::rss::feed>& feed, 
                               const std::chrono::system_clock::time_point& now) const {
    auto cache_it = freshness_cache.find(feed->source_link);
    bool should_refresh = true;
    
    if (cache_it != freshness_cache.end()) {
        auto cache_age = now - cache_it->second.second;
        if (std::chrono::duration_cast<std::chrono::minutes>(cache_age).count() < 2) {
            should_refresh = false;
        }
    }
    
    if (!should_refresh && cache_it != freshness_cache.end()) {
        return cache_it->second.first;
    }
    
    ImVec4 color;
    
    if (feed->items.empty()) {
        color = colors[9];
    } else {
        auto newest_time = std::chrono::system_clock::time_point::min();
        
        if (!feed->items.empty()) {
            newest_time = feed->items.front().updated;
            
            if (feed->items.size() > 1) {
                for (const auto& item : feed->items) {
                    if (item.updated > newest_time) {
                        newest_time = item.updated;
                    }
                }
            }
        }
        
        auto diff = now - newest_time;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
        
        if (hours < 1) {
            color = colors[6];
        } else if (hours < 24) {
            color = colors[7];
        } else if (hours < 72) {
            color = colors[8];
        } else {
            color = colors[9];
        }
    }
    
    if (freshness_cache.size() >= MAX_CACHE_SIZE && cache_it == freshness_cache.end()) {
        auto oldest_it = freshness_cache.begin();
        auto oldest_time = oldest_it->second.second;
        
        for (auto it = freshness_cache.begin(); it != freshness_cache.end(); ++it) {
            if (it->second.second < oldest_time) {
                oldest_time = it->second.second;
                oldest_it = it;
            }
        }
        
        freshness_cache.erase(oldest_it);
    }
    
    freshness_cache[feed->source_link] = {color, now};
    return color;
}

std::string rss::get_freshness_text(const std::shared_ptr<media::rss::feed>& feed, 
                                   const std::chrono::system_clock::time_point& now) const {
    if (feed->items.empty()) {
        return "No items";
    }
    
    auto newest_time = feed->items.front().updated;
    for (const auto& item : feed->items) {
        if (item.updated > newest_time) {
            newest_time = item.updated;
        }
    }
    
    auto diff = now - newest_time;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
    auto days = std::chrono::duration_cast<std::chrono::days>(diff).count();
    
    if (hours < 1) {
        return "Just now";
    }
    if (hours < 24) {
        return std::format("{}h ago", hours);
    }
    if (days < 7) {
        return std::format("{}d ago", days);
    }
    auto weeks = days / 7;
    return std::format("{}w ago", weeks);
}

void rss::request_image_download(const std::string& url) {
    static std::set<std::string> downloading_urls;

    {
        std::lock_guard<std::mutex> const lock(downloading_mutex_);
        if (downloading_urls.contains(url) || failed_downloads_.contains(url)) {
            return;
        }
        downloading_urls.insert(url);
    }

    auto cache = image_cache;
    std::thread([this, cache, url]() {
        bool success = false;
        try {
            success = cache->downloadAndCache(url);
        } catch (...) {}
        
        {
            std::lock_guard<std::mutex> const lock(downloading_mutex_);
            downloading_urls.erase(url);
            if (!success) {
                failed_downloads_.insert(url);
            }
        }

        if (success) {
            image_downloaded_signal_ = true;
        }
    }).detach();
}

std::vector<card::card_performance_metric> rss::get_performance_measurements() const {
    if (!rss_host) {
        return {};
    }
    double const rpm = rss_host->requests_per_minute();
    double const last_lat = rss_host->last_request_duration_ms();
    size_t const feed_count = cached_all_feeds_.size();

    cached_rpm_str_ = std::format("{:.1f} req/min", rpm);
    cached_latency_str_ = std::format("{:.1f} ms", last_lat);
    cached_feed_cnt_str_ = std::format("{} feeds", feed_count);

    return {
        {"Requests per minute", cached_rpm_str_},
        {"Last fetch latency", cached_latency_str_},
        {"Subscribed feeds", cached_feed_cnt_str_}
    };
}

} // namespace rouen::cards
