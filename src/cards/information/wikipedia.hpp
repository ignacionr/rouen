#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <string>
#include <vector>
#include <memory>
#include <format>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class wikipedia : public card {
public:
    struct wikipedia_result {
        std::string title;
        int pageid{0};
        std::string snippet;
        std::string image_url;
    };

    struct shared_state {
        std::mutex mutex;
        std::vector<wikipedia_result> search_results;
        bool is_searching{false};
        std::string error_message;
        
        // Article details
        std::string article_title;
        std::string article_summary;
        std::string article_full_text;
        std::string article_url;
        std::string article_thumbnail_url;
        bool is_loading_article{false};
        bool article_loaded{false};
        
        bool card_alive{true};
    };

    struct LoadedTexture {
        SDL_Texture* texture{nullptr};
        int width{0};
        int height{0};
    };

    static std::string trim(std::string_view s) {
        auto start = s.begin();
        while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
            start++;
        }
        auto end = s.end();
        while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
            end--;
        }
        return std::string(start, end);
    }

    wikipedia(std::string_view initial_query = "") {
        // Vibrant slate blue theme
        colors[0] = {0.12f, 0.35f, 0.62f, 1.0f}; // Primary Wikipedia Blue
        colors[1] = {0.18f, 0.45f, 0.75f, 0.7f}; // Lighter blue accent
        
        get_color(2, ImVec4(0.95f, 0.95f, 0.95f, 1.0f)); // Bright text for article titles
        get_color(3, ImVec4(0.75f, 0.75f, 0.75f, 1.0f)); // Secondary body text
        get_color(4, ImVec4(0.15f, 0.22f, 0.33f, 0.6f)); // Highlighted block background
        get_color(5, ImVec4(0.12f, 0.12f, 0.16f, 0.8f)); // Dark search box background
        get_color(6, ImVec4(0.35f, 0.65f, 0.95f, 1.0f)); // Hover / link text
        
        name("Wikipedia");
        requested_fps = 10;
        width = 600.0f; // Standard comfortable reading and list width
        
        state = std::make_shared<shared_state>();
        
        // Resolve paths for ImageCache
        std::string db_path = rouen::platform::get_user_data_path("wikipedia_images.db").string();
        std::string cache_dir = (rouen::platform::get_user_data_path() / "cache" / "wikipedia").string();
        image_cache_ = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
        
        std::string decoded = ::helpers::StringHelper::url_decode(initial_query);
        if (!decoded.empty()) {
            strncpy(search_buffer, decoded.c_str(), sizeof(search_buffer) - 1);
            current_query = decoded;
            if (decoded.starts_with("title:")) {
                trigger_load_article(decoded.substr(6));
            } else {
                trigger_search(decoded);
            }
        }
    }
    
    ~wikipedia() override {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->card_alive = false;
        }
        clear_loaded_textures();
    }

    void set_renderer(SDL_Renderer* r) {
        renderer_ = r;
    }

    void clear_loaded_textures() {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (auto& [url, lt] : loaded_textures_) {
            if (lt.texture) {
                SDL_DestroyTexture(lt.texture);
            }
        }
        loaded_textures_.clear();
    }

    std::string get_uri() const override {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->article_loaded && !state->article_title.empty()) {
            return "wikipedia:title:" + ::helpers::StringHelper::url_encode(state->article_title);
        }
        if (current_query.empty()) {
            return "wikipedia";
        }
        return "wikipedia:" + ::helpers::StringHelper::url_encode(current_query);
    }
    
    bool matches_uri(std::string_view uri) const override {
        return uri == "wikipedia" || uri.starts_with("wikipedia:");
    }
    
    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("wikipedia:")) {
            std::string query = std::string(uri.substr(10));
            std::string decoded_query = ::helpers::StringHelper::url_decode(query);
            
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                strncpy(search_buffer, decoded_query.c_str(), sizeof(search_buffer) - 1);
                current_query = decoded_query;
            }
            
            if (decoded_query.starts_with("title:")) {
                trigger_load_article(decoded_query.substr(6));
            } else {
                trigger_search(decoded_query);
            }
        }
    }

    void trigger_search(const std::string& query) {
        if (query.empty()) return;
        
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->is_searching = true;
            state->error_message.clear();
            state->article_loaded = false;
        }
        
        std::thread([shared_state_ptr = this->state, query]() {
            try {
                std::string encoded_query = ::helpers::StringHelper::url_encode(query);
                // Use generator=search to fetch page images and extracts at the same time
                std::string url = "https://en.wikipedia.org/w/api.php?action=query&generator=search&gsrsearch=" + encoded_query + 
                                  "&prop=pageimages|extracts&piprop=thumbnail&pithumbsize=120&exintro=1&explaintext=1&exsentences=2&format=json&utf8=";
                
                std::vector<std::string> headers = {
                    "User-Agent: RouenWikipediaCard/1.0 (ignacionr@github.com; ignacionr) libcurl/8.x",
                    "Accept: application/json"
                };
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t resp;
                auto ec = glz::read_json(resp, response);
                std::vector<wikipedia_result> temp_results;
                
                if (!ec && resp.contains("query") && resp["query"].contains("pages") && resp["query"]["pages"].is_object()) {
                    auto& pages = resp["query"]["pages"].get<glz::json_t::object_t>();
                    struct raw_item {
                        std::string title;
                        int pageid{0};
                        std::string snippet;
                        std::string image_url;
                        int index{0};
                    };
                    std::vector<raw_item> raw_items;
                    for (auto& [page_id, page_data] : pages) {
                        raw_item item;
                        if (page_data.contains("title") && page_data["title"].is_string()) {
                            item.title = page_data["title"].get<std::string>();
                        }
                        if (page_data.contains("pageid") && page_data["pageid"].is_number()) {
                            item.pageid = static_cast<int>(page_data["pageid"].get<double>());
                        }
                        if (page_data.contains("index") && page_data["index"].is_number()) {
                            item.index = static_cast<int>(page_data["index"].get<double>());
                        }
                        if (page_data.contains("extract") && page_data["extract"].is_string()) {
                            item.snippet = page_data["extract"].get<std::string>();
                        }
                        if (page_data.contains("thumbnail") && page_data["thumbnail"].contains("source") && page_data["thumbnail"]["source"].is_string()) {
                            item.image_url = page_data["thumbnail"]["source"].get<std::string>();
                        }
                        raw_items.push_back(std::move(item));
                    }
                    
                    // Sort by search result index (relevance)
                    std::sort(raw_items.begin(), raw_items.end(), [](const raw_item& a, const raw_item& b) {
                        return a.index < b.index;
                    });
                    
                    for (auto& raw : raw_items) {
                        wikipedia_result res;
                        res.title = raw.title;
                        res.pageid = raw.pageid;
                        res.snippet = raw.snippet;
                        res.image_url = raw.image_url;
                        temp_results.push_back(std::move(res));
                    }
                }
                
                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->search_results = std::move(temp_results);
                    shared_state_ptr->is_searching = false;
                    if (shared_state_ptr->search_results.empty()) {
                        shared_state_ptr->error_message = "No Wikipedia results found.";
                    }
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->is_searching = false;
                    shared_state_ptr->error_message = std::string("Search error: ") + e.what();
                }
            }
        }).detach();
    }

    void trigger_load_article(const std::string& title) {
        if (title.empty()) return;
        
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->is_loading_article = true;
            state->error_message.clear();
            state->article_loaded = false;
            state->article_title = title;
            state->article_summary.clear();
            state->article_full_text.clear();
            state->article_url.clear();
            state->article_thumbnail_url.clear();
        }
        
        std::thread([shared_state_ptr = this->state, title]() {
            try {
                std::string encoded_title = ::helpers::StringHelper::url_encode(title);
                std::string title_under = title;
                std::replace(title_under.begin(), title_under.end(), ' ', '_');
                std::string encoded_title_rest = ::helpers::StringHelper::url_encode(title_under);
                
                std::string summary_url = "https://en.wikipedia.org/api/rest_v1/page/summary/" + encoded_title_rest;
                std::string extract_url = "https://en.wikipedia.org/w/api.php?action=query&prop=extracts&explaintext=1&titles=" + encoded_title + "&format=json";
                
                std::vector<std::string> headers = {
                    "User-Agent: RouenWikipediaCard/1.0 (ignacionr@github.com; ignacionr) libcurl/8.x",
                    "Accept: application/json"
                };
                
                std::string summary_body;
                std::string extract_body;
                std::string temp_summary;
                std::string temp_full_text;
                std::string temp_url;
                std::string temp_thumbnail_url;
                std::string temp_title = title;
                
                // Fetch summary
                try {
                    summary_body = http::fetch()(summary_url, headers);
                    glz::json_t resp;
                    auto ec = glz::read_json(resp, summary_body);
                    if (!ec) {
                        if (resp.contains("extract") && resp["extract"].is_string()) {
                            temp_summary = resp["extract"].get<std::string>();
                        }
                        if (resp.contains("title") && resp["title"].is_string()) {
                            temp_title = resp["title"].get<std::string>();
                        }
                        if (resp.contains("content_urls") && resp["content_urls"].contains("desktop") && 
                            resp["content_urls"]["desktop"].contains("page") && resp["content_urls"]["desktop"]["page"].is_string()) {
                            temp_url = resp["content_urls"]["desktop"]["page"].get<std::string>();
                        }
                        if (resp.contains("thumbnail") && resp["thumbnail"].contains("source") && resp["thumbnail"]["source"].is_string()) {
                            temp_thumbnail_url = resp["thumbnail"]["source"].get<std::string>();
                        }
                    }
                } catch (...) {
                    // Fallback handles this
                }
                
                // Fetch full text
                try {
                    extract_body = http::fetch()(extract_url, headers);
                    glz::json_t resp;
                    auto ec = glz::read_json(resp, extract_body);
                    if (!ec && resp.contains("query") && resp["query"].contains("pages") && resp["query"]["pages"].is_object()) {
                        auto& pages = resp["query"]["pages"].get<glz::json_t::object_t>();
                        for (auto& [page_id, page_data] : pages) {
                            if (page_data.contains("extract") && page_data["extract"].is_string()) {
                                temp_full_text = page_data["extract"].get<std::string>();
                            }
                            if (temp_title.empty() && page_data.contains("title") && page_data["title"].is_string()) {
                                temp_title = page_data["title"].get<std::string>();
                            }
                        }
                    }
                } catch (...) {
                    // Fallback handles this
                }
                
                if (temp_url.empty()) {
                    std::string t_under = temp_title;
                    std::replace(t_under.begin(), t_under.end(), ' ', '_');
                    temp_url = "https://en.wikipedia.org/wiki/" + ::helpers::StringHelper::url_encode(t_under);
                }
                
                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->article_title = temp_title;
                    shared_state_ptr->article_summary = temp_summary;
                    shared_state_ptr->article_full_text = temp_full_text;
                    shared_state_ptr->article_url = temp_url;
                    shared_state_ptr->article_thumbnail_url = temp_thumbnail_url;
                    shared_state_ptr->is_loading_article = false;
                    shared_state_ptr->article_loaded = true;
                    
                    if (temp_full_text.empty() && temp_summary.empty()) {
                        shared_state_ptr->error_message = "Failed to load article content.";
                        shared_state_ptr->article_loaded = false;
                    }
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->is_loading_article = false;
                    shared_state_ptr->error_message = std::string("Load error: ") + e.what();
                }
            }
        }).detach();
    }

    void request_image_download(const std::string& url) {
        std::lock_guard<std::mutex> lock(downloading_mutex_);
        if (downloading_urls_.find(url) == downloading_urls_.end()) {
            downloading_urls_.insert(url);
            std::thread([this, url]() {
                try {
                    image_cache_->downloadAndCache(url);
                } catch (...) {}
                std::lock_guard<std::mutex> lock2(downloading_mutex_);
                downloading_urls_.erase(url);
            }).detach();
        }
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

    SDL_Texture* get_item_texture(const std::string& url, ::helpers::ImageCache::Variant variant, int& texture_width, int& texture_height) {
        texture_width = 0;
        texture_height = 0;

        if (!renderer_ || !image_cache_ || url.empty()) {
            return nullptr;
        }

        const auto cache_key = variant == ::helpers::ImageCache::Variant::Grayscale ? url + "#grayscale" : url;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto it = loaded_textures_.find(cache_key);
            if (it != loaded_textures_.end()) {
                texture_width = it->second.width;
                texture_height = it->second.height;
                return it->second.texture;
            }
        }

        SDL_Texture* texture = image_cache_->getTexture(renderer_, url, texture_width, texture_height, false, variant);
        if (texture) {
            std::lock_guard<std::mutex> lock(state->mutex);
            loaded_textures_[cache_key] = {texture, texture_width, texture_height};
        }
        return texture;
    }

    bool render() override {
        return render_window([this]() {
            bool is_searching = false;
            bool is_loading_article = false;
            bool article_loaded = false;
            std::string error_message;
            std::string article_title;
            std::string article_summary;
            std::string article_full_text;
            std::string article_url;
            std::string article_thumbnail_url;
            std::vector<wikipedia_result> results;
            
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                is_searching = state->is_searching;
                is_loading_article = state->is_loading_article;
                article_loaded = state->article_loaded;
                error_message = state->error_message;
                article_title = state->article_title;
                article_summary = state->article_summary;
                article_full_text = state->article_full_text;
                article_url = state->article_url;
                article_thumbnail_url = state->article_thumbnail_url;
                results = state->search_results;
            }
            
            if (is_searching || is_loading_article) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", is_searching ? "Searching Wikipedia..." : "Loading Article...");
                ImGui::Separator();
                
                static float dot_timer = 0.0f;
                dot_timer += ImGui::GetIO().DeltaTime;
                size_t dot_count = static_cast<size_t>(dot_timer * 2.0f) % 4;
                std::string dots(dot_count, '.');
                ImGui::Text("Please wait%s", dots.c_str());
                return;
            }
            
            if (!error_message.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("Error: %s", error_message.c_str());
                ImGui::PopStyleColor();
                if (ImGui::Button("Clear Error")) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->error_message.clear();
                }
                ImGui::Separator();
            }
            
            if (article_loaded) {
                // RENDER ARTICLE VIEW
                if (ImGui::Button("< Back to Search")) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->article_loaded = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Open in Browser")) {
                    rouen::platform::open_url(article_url);
                }
                
                ImGui::Separator();
                
                // Draw thumbnail next to title if available
                if (!article_thumbnail_url.empty()) {
                    int thumb_w = 0, thumb_h = 0;
                    SDL_Texture* thumb_tex = get_item_texture(article_thumbnail_url, ::helpers::ImageCache::Variant::Color, thumb_w, thumb_h);
                    if (thumb_tex) {
                        float aspect = static_cast<float>(thumb_w) / static_cast<float>(thumb_h);
                        float draw_h = 100.0f;
                        float draw_w = draw_h * aspect;
                        
                        ImGui::Image(rouen::helpers::texture_id_cast(thumb_tex), ImVec2(draw_w, draw_h));
                        ImGui::SameLine();
                    } else {
                        int cached_w = 0, cached_h = 0;
                        if (!image_cache_->isCached(article_thumbnail_url, cached_w, cached_h, ::helpers::ImageCache::Variant::Color)) {
                            request_image_download(article_thumbnail_url);
                        }
                    }
                }

                ImGui::BeginGroup();
                ImGui::TextColored(colors[2], "%s", article_title.c_str());
                ImGui::EndGroup();
                
                ImGui::Separator();
                
                ImGui::BeginChild("ArticleContent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                
                if (!article_summary.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[4]);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                    ImGui::BeginChild("SummaryBox", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4.0f + 16.0f), true);
                    ImGui::TextWrapped("%s", article_summary.c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                }
                
                if (!article_full_text.empty()) {
                    std::stringstream ss(article_full_text);
                    std::string line;
                    while (std::getline(ss, line)) {
                        if (line.empty()) {
                            ImGui::Spacing();
                            continue;
                        }
                        
                        if (line.starts_with("==") && line.ends_with("==")) {
                            size_t level = 0;
                            while (level < line.size() && line[level] == '=') level++;
                            std::string header_title = line.substr(level, line.size() - level * 2);
                            header_title = trim(header_title);
                            
                            if (level == 2) {
                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::TextColored(colors[0], "%s", header_title.c_str());
                                ImGui::Spacing();
                            } else {
                                ImGui::Spacing();
                                ImGui::TextColored(colors[1], "%s", header_title.c_str());
                            }
                        } else {
                            ImGui::TextWrapped("%s", line.c_str());
                        }
                    }
                } else {
                    ImGui::TextWrapped("No text content available.");
                }
                
                ImGui::EndChild();
            } else {
                // RENDER SEARCH VIEW
                float button_w = 80.0f;
                ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[5]);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - button_w - ImGui::GetStyle().ItemSpacing.x);
                bool search_submitted = ImGui::InputTextWithHint("##wiki_search_input", "Search Wikipedia...", search_buffer, sizeof(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::PopItemWidth();
                ImGui::PopStyleColor();
                
                ImGui::SameLine();
                if (ImGui::Button("Search", ImVec2(button_w, 0)) || search_submitted) {
                    current_query = search_buffer;
                    trigger_search(current_query);
                }
                
                ImGui::Separator();
                
                ImGui::BeginChild("SearchResultsList", ImVec2(0, 0), true);
                if (results.empty()) {
                    ImGui::TextColored(colors[3], "Enter a search term to find articles.");
                } else {
                    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
                    float avail_width = ImGui::GetContentRegionAvail().x;
                    
                    for (size_t i = 0; i < results.size(); ++i) {
                        const auto& res = results[i];
                        
                        ImGui::PushID(std::format("{}##{}", res.title, i).c_str());
                        
                        const ImVec2 row_start = ImGui::GetCursorScreenPos();
                        
                        // Try to load item thumbnail
                        SDL_Texture* item_tex = nullptr;
                        int item_tex_w = 0, item_tex_h = 0;
                        if (renderer_ && image_cache_ && !res.image_url.empty()) {
                            item_tex = get_item_texture(res.image_url, ::helpers::ImageCache::Variant::Color, item_tex_w, item_tex_h);
                            if (!item_tex) {
                                int cached_w = 0, cached_h = 0;
                                if (!image_cache_->isCached(res.image_url, cached_w, cached_h, ::helpers::ImageCache::Variant::Color)) {
                                    request_image_download(res.image_url);
                                }
                            }
                        }
                        
                        bool has_item_image = (item_tex != nullptr);
                        
                        try {
                            ImGui::BeginGroup();
                            
                            if (has_item_image) {
                                ImGui::BeginGroup();
                                ImGui::PushTextWrapPos(avail_width - 130.0f * dpi_scale);
                            } else {
                                ImGui::PushTextWrapPos(avail_width);
                            }
                            
                            // Title (selectable to open item)
                            if (ImGui::Selectable(res.title.c_str(), false, 0, ImVec2(has_item_image ? avail_width - 130.0f * dpi_scale : avail_width, 0))) {
                                trigger_load_article(res.title);
                            }
                            
                            if (!res.snippet.empty()) {
                                ImGui::TextWrapped("%s", res.snippet.c_str());
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
                                    if (SDL_Texture* grayscale_tex = get_item_texture(res.image_url, ::helpers::ImageCache::Variant::Grayscale, grayscale_w, grayscale_h)) {
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
                                
                                // Set dummy height spacing to ensure layout advances below the image if it's taller than group text
                                ImGui::SetCursorScreenPos(ImVec2(row_start.x, row_bottom));
                            }
                            
                            ImGui::EndGroup();
                        } catch (...) {}
                        
                        ImGui::PopID();
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
            }
        });
    }

private:
    std::shared_ptr<shared_state> state;
    char search_buffer[256]{};
    std::string current_query;

    SDL_Renderer* renderer_{nullptr};
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    std::unordered_map<std::string, LoadedTexture> loaded_textures_;
    std::unordered_set<std::string> downloading_urls_;
    std::mutex downloading_mutex_;
};

} // namespace rouen::cards
