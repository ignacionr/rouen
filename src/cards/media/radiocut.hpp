#pragma once

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <regex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../external/IconsMaterialDesign.h"

namespace rouen::cards {

    struct ChunkInfo {
        std::string base_url;
        double start = 0.0;
        double length = 0.0;
        std::string filename;
    };

    struct FolderInfo {
        std::string baseURL;
        std::vector<ChunkInfo> chunks;
        bool is_full = false;
    };

    struct radiocut_cut {
        std::string id;
        std::string path;
        std::string title;
        std::string description;
        std::string image_url;
        std::string duration;
        std::string time_ago;
        std::string author;
        std::string plays;
        
        // Resolution state
        bool resolving = false;
        bool resolved = false;
        std::string error_message;
        std::string playlist_url;
        double start_offset = 0.0;
        bool should_autoplay = false;
        std::optional<double> watermark = std::nullopt;
    };

} // namespace rouen::cards

// Glaze reflection for RadioCut API chunks response
template <>
struct glz::meta<rouen::cards::ChunkInfo> {
    static constexpr auto values = glz::object(
        "base_url", &rouen::cards::ChunkInfo::base_url,
        "start", &rouen::cards::ChunkInfo::start,
        "length", &rouen::cards::ChunkInfo::length,
        "filename", &rouen::cards::ChunkInfo::filename
    );
    static constexpr auto options = glz::opts{ .error_on_unknown_keys = false };
};

template <>
struct glz::meta<rouen::cards::FolderInfo> {
    static constexpr auto values = glz::object(
        "baseURL", &rouen::cards::FolderInfo::baseURL,
        "chunks", &rouen::cards::FolderInfo::chunks,
        "is_full", &rouen::cards::FolderInfo::is_full
    );
    static constexpr auto options = glz::opts{ .error_on_unknown_keys = false };
};

namespace rouen::cards {

    using ChunkResponse = std::unordered_map<std::string, FolderInfo>;

    class radiocut : public card {
    public:
        radiocut() {
            colors[0] = ImVec4{0.12f, 0.58f, 0.89f, 1.0f}; // Premium Blue
            colors[1] = ImVec4{0.07f, 0.40f, 0.65f, 0.7f}; // Dark Blue secondary
            get_color(2, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));  // Green playing status
            
            name("RadioCut.fm Client");
            requested_fps = 10;
            width = 750.0f; // 250% of default 300.0f width
            
            auto db_path = rouen::platform::get_user_data_path("radiocut_images.db").string();
            auto cache_dir = rouen::platform::get_user_data_path("cache/radiocut_images").string();
            image_cache = std::make_shared<::helpers::ImageCache>(
                db_path,
                cache_dir,
                30
            );
        }
        
        void on_close() override {
            media_player::stopForOwner(this);
            rouen::platform::stop_speech();
        }

        ~radiocut() override {
            on_close();
        }
        
        std::string get_uri() const override {
            return "radiocut";
        }
        
        void set_renderer(SDL_Renderer* r) {
            renderer = r;
        }
        
        bool render() override {
            return render_window([this]() {
                ImGui::PushStyleColor(ImGuiCol_Header, colors[1]);
                
                // Search Input Field
                ImGui::PushItemWidth(-60.0f);
                bool enter_pressed = ImGui::InputTextWithHint("##RadioCutSearch", "Search RadioCut cuts (e.g. milei, dolina)...", 
                                                              search_buffer, sizeof(search_buffer), 
                                                              ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::PopItemWidth();
                
                ImGui::SameLine();
                bool search_clicked = ImGui::Button(ICON_MD_SEARCH " Search", ImVec2(52, 0));
                
                if (enter_pressed || search_clicked) {
                    if (search_buffer[0] != '\0') {
                        start_search(search_buffer);
                    }
                }
                
                ImGui::Separator();
                
                // Active State Message
                if (is_searching) {
                    ImGui::TextColored(colors[0], ICON_MD_SYNC " Searching cuts on RadioCut.fm...");
                } else if (!search_error.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_MD_ERROR " Error: %s", search_error.c_str());
                } else {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    if (cuts.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enter a search term to find cuts.");
                    } else {
                        ImGui::TextColored(colors[0], "Found %d cuts matching your search:", static_cast<int>(cuts.size()));
                    }
                }
                
                ImGui::Spacing();
                
                // Results List Container
                if (ImGui::BeginChild("CutsListChild", ImVec2(0, 0), true)) {
                    std::vector<radiocut_cut> local_cuts;
                    {
                        std::lock_guard<std::mutex> lock(data_mutex);
                        local_cuts = cuts;
                    }
                    
                    for (size_t idx = 0; idx < local_cuts.size(); ++idx) {
                        auto& cut = local_cuts[idx];
                        ImGui::PushID(cut.id.c_str());
                        
                        // Card background styling
                        bool is_playing = false;
                        std::string playlist_url = cut.playlist_url.empty() ? ("file:///tmp/radiocut_" + cut.id + ".m3u8") : cut.playlist_url;
                        
                        // Check if this cut is currently playing in the global media player
                        if (cut.resolved) {
                            auto it = media_player::items().find(media_player::get_item_id(playlist_url));
                            if (it != media_player::items().end() && it->second) {
                                is_playing = it->second->checkMediaStatus();
                            }
                        }
                        
                        // Render a premium border/background for the active cut
                        if (is_playing) {
                            ImGui::PushStyleColor(ImGuiCol_Border, colors[0]);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                            ImGui::BeginGroup();
                        }
                        
                        ImGui::BeginGroup();
                        
                        // Render image thumbnail on the left
                        SDL_Texture* thumb_tex = nullptr;
                        if (renderer && image_cache && !cut.image_url.empty()) {
                            int w = 0, h = 0;
                            thumb_tex = image_cache->getTexture(renderer, cut.image_url, w, h);
                        }
                        
                        if (thumb_tex) {
                            ImGui::Image(rouen::helpers::texture_id_cast(thumb_tex), ImVec2(50, 50));
                        } else {
                            // Render a nice colored icon box if thumbnail is loading/missing
                            ImGui::PushStyleColor(ImGuiCol_Button, colors[1]);
                            ImGui::Button(ICON_MD_RADIO, ImVec2(50, 50));
                            ImGui::PopStyleColor();
                        }
                        
                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        
                        // Title
                        ImGui::TextColored(is_playing ? colors[0] : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", cut.title.c_str());
                        
                        // Description
                        if (!cut.description.empty()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                            ImGui::TextUnformatted(cut.description.c_str());
                            ImGui::PopTextWrapPos();
                            ImGui::PopStyleColor();
                        }
                        
                        // Metadata Row (Author, Duration, Plays, Time Ago)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.7f, 0.9f));
                        
                        if (!cut.author.empty()) {
                            ImGui::Text(ICON_MD_PERSON " %s", cut.author.c_str());
                            ImGui::SameLine();
                        }
                        
                        if (!cut.duration.empty()) {
                            ImGui::Text(ICON_MD_ACCESS_TIME " %s", cut.duration.c_str());
                            ImGui::SameLine();
                        }
                        
                        if (!cut.plays.empty() && cut.plays != "0") {
                            ImGui::Text(ICON_MD_PLAY_ARROW " %s", cut.plays.c_str());
                            ImGui::SameLine();
                        }
                        
                        if (!cut.time_ago.empty()) {
                            ImGui::Text(ICON_MD_EVENT " %s", cut.time_ago.c_str());
                        }
                        
                        ImGui::PopStyleColor();
                        ImGui::EndGroup();
                        ImGui::EndGroup();
                        
                        // Controls Area (Play button, resolving progress, or active player widget)
                        ImGui::Spacing();
                        if (cut.resolving) {
                            ImGui::TextColored(colors[0], ICON_MD_SYNC " Resolving stream & loading audio chunks...");
                        } else if (!cut.error_message.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_MD_ERROR " Playback error: %s", cut.error_message.c_str());
                            if (ImGui::Button("Retry")) {
                                trigger_play(cut.id);
                            }
                        } else if (cut.resolved) {
                            // Autoplay trigger
                            if (cut.should_autoplay) {
                                std::lock_guard<std::mutex> lock(data_mutex);
                                auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto& c) { return c.id == cut.id; });
                                if (it != cuts.end()) {
                                    it->should_autoplay = false;
                                }
                                
                                media_player::stopAll();
                                
                                auto &item = media_player::get_item(playlist_url);
                                item.url = playlist_url;
                                item.start_offset = cut.start_offset;
                                item.playMedia(this);
                            }
                            
                            // Render native player controls
                            std::string cut_link = cut.path.empty() ? "" : ("https://radiocut.fm" + cut.path);
                            media_player::player(playlist_url, colors[0], cut.title, -1, cut_link, cut.title, cut.watermark, false, 0.0f, this);
                            
                            // Sync updated watermark back to shared state
                            {
                                std::lock_guard<std::mutex> lock(data_mutex);
                                auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto& c) { return c.id == cut.id; });
                                if (it != cuts.end()) {
                                    it->watermark = cut.watermark;
                                }
                            }
                        } else {
                            if (ImGui::Button(ICON_MD_PLAY_ARROW " Play")) {
                                trigger_play(cut.id);
                            }
                        }
                        
                        ImGui::Separator();
                        ImGui::Spacing();
                        
                        if (is_playing) {
                            ImGui::EndGroup();
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor();
                        }
                        
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                
                ImGui::PopStyleColor();
            });
        }
        
    private:
        char search_buffer[256] = "";
        std::vector<radiocut_cut> cuts;
        std::mutex data_mutex;
        
        std::atomic<bool> is_searching{false};
        std::string search_error;
        
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<::helpers::ImageCache> image_cache;
        
        // Percent-encode query strings safely
        static std::string url_encode(std::string_view s) {
            std::ostringstream oss;
            oss.fill('0');
            oss << std::hex << std::uppercase;
            for (char ch : s) {
                const unsigned char c = static_cast<unsigned char>(ch);
                if ((c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~') {
                    oss << static_cast<char>(c);
                } else if (c == ' ') {
                    oss << "%20";
                } else {
                    oss << '%' << std::setw(2) << int(c);
                    oss << std::setw(0);
                }
            }
            return oss.str();
        }
        
        // Basic HTML entity decoder
        static std::string decode_html_entities(std::string_view html) {
            std::string result(html);
            auto replace_all = [&](std::string_view from, std::string_view to) {
                size_t pos = 0;
                while ((pos = result.find(from, pos)) != std::string::npos) {
                    result.replace(pos, from.length(), to);
                    pos += to.length();
                }
            };
            replace_all("&quot;", "\"");
            replace_all("&#39;", "'");
            replace_all("&amp;", "&");
            replace_all("&lt;", "<");
            replace_all("&gt;", ">");
            replace_all("&nbsp;", " ");
            replace_all("&ntilde;", "ñ");
            replace_all("&Ntilde;", "Ñ");
            replace_all("&aacute;", "á");
            replace_all("&eacute;", "é");
            replace_all("&iacute;", "í");
            replace_all("&oacute;", "ó");
            replace_all("&uacute;", "ú");
            replace_all("&Aacute;", "Á");
            replace_all("&Eacute;", "É");
            replace_all("&Iacute;", "Í");
            replace_all("&Oacute;", "Ó");
            replace_all("&Uacute;", "Ú");
            return result;
        }
        
        // Parse search results HTML
        static std::vector<radiocut_cut> parse_search_results(const std::string& html) {
            std::vector<radiocut_cut> results;
            std::string card_delimiter = "<li class=\"search-card\">";
            
            size_t pos = 0;
            while ((pos = html.find(card_delimiter, pos)) != std::string::npos) {
                size_t next_pos = html.find(card_delimiter, pos + card_delimiter.length());
                std::string card_html = html.substr(pos, next_pos - pos);
                pos = next_pos;
                
                radiocut_cut cut;
                
                // 1. Path & Title
                std::regex title_rx(R"raw(class="title desktop"\s+href="([^"]+)"[^>]*>([^<]+)</a>)raw");
                std::smatch m;
                if (std::regex_search(card_html, m, title_rx)) {
                    cut.path = m[1].str();
                    cut.title = decode_html_entities(m[2].str());
                } else {
                    continue; // Skip if no title/path
                }
                
                // Extract clean ID from path
                std::regex id_rx(R"raw(/audiocut/([^/]+)/)raw");
                if (std::regex_search(cut.path, m, id_rx)) {
                    cut.id = m[1].str();
                } else {
                    cut.id = cut.path;
                }
                
                // 2. Image
                std::regex img_rx(R"raw(class="img-thumbnail"\s+src="([^"]+)")raw");
                if (std::regex_search(card_html, m, img_rx)) {
                    cut.image_url = m[1].str();
                }
                
                // 3. Description
                std::regex desc_rx(R"raw(class="description"\s+href="[^"]+"[^>]*>([\s\S]*?)</a>)raw");
                if (std::regex_search(card_html, m, desc_rx)) {
                    std::string raw_desc = m[1].str();
                    raw_desc = std::regex_replace(raw_desc, std::regex(R"raw(<br\s*/?>)raw"), "\n");
                    raw_desc = std::regex_replace(raw_desc, std::regex(R"raw(<[^>]+>)raw"), "");
                    cut.description = decode_html_entities(raw_desc);
                }
                
                // 4. Stats
                std::regex p_rx(R"raw(<p>([^<]+)</p>)raw");
                auto p_begin = std::sregex_iterator(card_html.begin(), card_html.end(), p_rx);
                auto p_end = std::sregex_iterator();
                int p_idx = 0;
                for (std::sregex_iterator i = p_begin; i != p_end; ++i) {
                    std::string val = (*i)[1].str();
                    if (p_idx == 0) cut.plays = val;
                    else if (p_idx == 1) cut.duration = val;
                    else if (p_idx == 2) cut.time_ago = val;
                    p_idx++;
                }
                
                // 5. Author
                std::regex user_rx(R"raw(href="/user/[^"]+"[^>]*>(.*?)</a>)raw");
                if (std::regex_search(card_html, m, user_rx)) {
                    std::string auth = m[1].str();
                    size_t por_pos = auth.find("por ");
                    if (por_pos != std::string::npos) {
                        auth = auth.substr(por_pos + 4);
                    }
                    cut.author = auth;
                }
                
                results.push_back(cut);
            }
            return results;
        }
        
        // Asynchronous search task
        void start_search(const std::string& query) {
            if (is_searching) return;
            is_searching = true;
            search_error.clear();
            
            std::thread([this, query]() {
                try {
                    http::fetch client(15);
                    std::vector<std::string> headers = {
                        "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
                    };
                    std::string url = "https://radiocut.fm/search/?search_term=" + url_encode(query) + "&type=cut";
                    std::string html = client(url, headers);
                    
                    auto parsed_cuts = parse_search_results(html);
                    
                    std::lock_guard<std::mutex> lock(data_mutex);
                    cuts = std::move(parsed_cuts);
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    search_error = e.what();
                    cuts.clear();
                }
                is_searching = false;
            }).detach();
        }
        
        // Trigger asynchronous resolving and playback
        void trigger_play(const std::string& cut_id) {
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto& c) { return c.id == cut_id; });
                if (it == cuts.end() || it->resolving) return;
                
                it->resolving = true;
                it->resolved = false;
                it->should_autoplay = true;
                it->error_message.clear();
            }
            
            std::thread([this, cut_id]() {
                std::string path;
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto& c) { return c.id == cut_id; });
                    if (it != cuts.end()) {
                        path = it->path;
                    }
                }
                
                std::string error;
                std::string playlist_url;
                double offset = 0.0;
                bool success = false;
                
                try {
                    http::fetch client(15);
                    std::vector<std::string> headers = {
                        "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
                    };
                    
                    std::string detail_url = "https://radiocut.fm" + path;
                    std::string html = client(detail_url, headers);
                    
                    // Parse station, seconds, duration, base_url
                    std::regex station_rx(R"raw(<li class="audio_station">([^<]+)</li>)raw");
                    std::regex seconds_rx(R"raw(<li class="audio_seconds">(\d+)</li>)raw");
                    std::regex duration_rx(R"raw(<li class="audio_duration">(\d+)</li>)raw");
                    std::regex base_url_rx(R"raw(<li class="audio_base_url">([^<]+)</li>)raw");
                    
                    std::smatch m;
                    std::string station, base_url;
                    long long start_time = 0;
                    double duration = 0.0;
                    
                    if (std::regex_search(html, m, station_rx)) station = m[1].str();
                    if (std::regex_search(html, m, seconds_rx)) start_time = std::stoll(m[1].str());
                    if (std::regex_search(html, m, duration_rx)) duration = std::stod(m[1].str());
                    if (std::regex_search(html, m, base_url_rx)) base_url = m[1].str();
                    
                    if (station.empty() || start_time == 0 || duration == 0.0) {
                        throw std::runtime_error("Could not parse audio configuration from cut page");
                    }
                    
                    if (base_url.empty()) {
                        base_url = "https://chunkserver-do.radiocut.site";
                    }
                    
                    long long end_time = start_time + static_cast<long long>(duration);
                    long long start_dir = start_time / 10000;
                    long long end_dir = end_time / 10000;
                    
                    std::vector<ChunkInfo> all_chunks;
                    
                    // Fetch chunk definitions for all dirs in cut duration
                    for (long long dir = start_dir; dir <= end_dir; ++dir) {
                        std::string chunk_url = "https://chunkserver-do.radiocut.site/server/get_chunks/" + station + "/" + std::to_string(dir) + "/";
                        std::string json_str = client(chunk_url, headers);
                        
                        ChunkResponse resp;
                        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(resp, json_str);
                        if (ec) {
                            continue; // skip failed folder loads
                        }
                        
                        auto it = resp.find(std::to_string(dir));
                        if (it != resp.end()) {
                            const auto& folder = it->second;
                            std::string folder_base = folder.baseURL;
                            if (folder_base.empty()) {
                                folder_base = base_url;
                            }
                            
                            for (const auto& chunk : folder.chunks) {
                                ChunkInfo final_chunk = chunk;
                                if (final_chunk.base_url.empty()) {
                                    final_chunk.base_url = folder_base;
                                }
                                all_chunks.push_back(final_chunk);
                            }
                        }
                    }
                    
                    // Filter overlapping chunks
                    std::vector<ChunkInfo> selected_chunks;
                    for (const auto& chunk : all_chunks) {
                        if (chunk.start + chunk.length > static_cast<double>(start_time) && chunk.start < static_cast<double>(end_time)) {
                            selected_chunks.push_back(chunk);
                        }
                    }
                    
                    if (selected_chunks.empty()) {
                        throw std::runtime_error("No audio chunks found for this cut");
                    }
                    
                    // Calculate start offset inside first chunk
                    double first_chunk_start = selected_chunks[0].start;
                    offset = static_cast<double>(start_time) - first_chunk_start;
                    if (offset < 0.0) offset = 0.0;
                    
                    // Construct HLS M3U8 playlist file content
                    int max_chunk_len = 10;
                    for (const auto& chunk : selected_chunks) {
                        if (chunk.length > max_chunk_len) {
                            max_chunk_len = static_cast<int>(std::ceil(chunk.length));
                        }
                    }
                    std::stringstream m3u;
                    m3u << "#EXTM3U\n";
                    m3u << "#EXT-X-VERSION:3\n";
                    m3u << "#EXT-X-TARGETDURATION:" << max_chunk_len << "\n";
                    m3u << "#EXT-X-MEDIA-SEQUENCE:0\n";
                    for (const auto& chunk : selected_chunks) {
                        std::string url = chunk.base_url;
                        if (url.find("http:") == 0) {
                            if (url.find("http://cdn-gs.radiocut.fm") == std::string::npos) {
                                url.replace(0, 5, "https:");
                            }
                        }
                        m3u << "#EXTINF:" << static_cast<int>(chunk.length) << "," << chunk.filename << "\n";
                        m3u << url << "/" << chunk.filename << "\n";
                    }
                    m3u << "#EXT-X-ENDLIST\n";
                    
                    // Write M3U8 playlist file to /tmp
                    std::string playlist_path = "/tmp/radiocut_" + cut_id + ".m3u8";
                    std::ofstream playlist_file(playlist_path, std::ios::out | std::ios::trunc);
                    if (!playlist_file) {
                        throw std::runtime_error("Failed to write temporary M3U8 playlist file");
                    }
                    playlist_file << m3u.str();
                    playlist_file.close();
                    
                    playlist_url = "file://" + playlist_path;
                    success = true;
                } catch (const std::exception& e) {
                    error = e.what();
                    success = false;
                }
                
                // Apply update under mutex lock
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto& c) { return c.id == cut_id; });
                    if (it != cuts.end()) {
                        it->resolving = false;
                        if (success) {
                            it->playlist_url = playlist_url;
                            it->start_offset = offset;
                            it->resolved = true;
                        } else {
                            it->error_message = error;
                            it->resolved = false;
                            it->should_autoplay = false;
                        }
                    }
                }
            }).detach();
        }
    };

} // namespace rouen::cards
