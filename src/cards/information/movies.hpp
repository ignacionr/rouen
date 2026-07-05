#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <optional>

#include "../interface/card.hpp"
#include "../../helpers/sqlite.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class movies : public card {
public:
    struct MovieItem {
        long long id{0};
        std::string title;
        std::string poster_path;
        std::string release_date;
        double vote_average{0.0};
        std::string overview;
        std::string list_name;
    };

    movies() {
        // Setup card colors (violet primary/secondary for movie theater feel)
        colors[0] = ImVec4(0.5f, 0.3f, 0.7f, 1.0f); // Violet primary
        colors[1] = ImVec4(0.6f, 0.4f, 0.8f, 0.7f); // Lighter secondary
        
        get_color(2, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // White for titles
        get_color(3, ImVec4(0.9f, 0.8f, 0.2f, 1.0f)); // Yellow for ratings
        get_color(4, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Gray for overview text
        get_color(5, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); // Red for delete button

        name("My Movies & Watchlists");
        width = 650.0f;
        requested_fps = 30; // High frame rate for animations/scrolling

        // Resolve paths for ImageCache
        std::string db_path = rouen::platform::get_user_data_path("movies_images.db").string();
        std::string cache_dir = (rouen::platform::get_user_data_path() / "cache" / "movies").string();
        image_cache_ = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);

        init_db();
        fetch_trending();
    }

    ~movies() override = default;

    std::string get_uri() const override {
        return "movies";
    }

    bool render() override {
        return render_window([this]() {
            auto config = rouen::helpers::ConfigService::instance();
            std::string api_key = config->get_env("TMDB_API_KEY");
            std::string token = config->get_env("TMDB_TOKEN");

            if (api_key.empty() && token.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("TMDB API Key or Token is not configured!");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::TextWrapped("Please edit your environment configuration and add either:");
                ImGui::BulletText("TMDB_API_KEY (v3 API Key)");
                ImGui::BulletText("TMDB_TOKEN (v4 Read Access Bearer Token)");
                ImGui::Spacing();
                ImGui::TextWrapped("You can create an account and generate these keys for free at: https://www.themoviedb.org/");
                return;
            }

            // Left Side: Sidebar navigation (List selector)
            ImGui::BeginGroup();
            float sidebar_w = 140.0f;
            ImGui::BeginChild("SidebarList", ImVec2(sidebar_w, -1), true);
            
            std::vector<std::string> list_names = {"To Watch", "Watched", "Favorites", "Trending", "Search Movies"};
            for (const auto& list_name : list_names) {
                bool is_selected = (selected_list_ == list_name);
                
                // Show items counts for user lists
                std::string label = list_name;
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    if (list_name == "To Watch") {
                        label += std::format(" ({})", lists_["To Watch"].size());
                    } else if (list_name == "Watched") {
                        label += std::format(" ({})", lists_["Watched"].size());
                    } else if (list_name == "Favorites") {
                        label += std::format(" ({})", lists_["Favorites"].size());
                    }
                }

                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    selected_list_ = list_name;
                }
            }
            ImGui::EndChild();
            ImGui::EndGroup();

            ImGui::SameLine();

            // Right Side: List Content View
            ImGui::BeginGroup();
            float content_w = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild("ListContent", ImVec2(content_w, -1), false);
            
            if (selected_list_ == "Search Movies") {
                render_search_view();
            } else if (selected_list_ == "Trending") {
                render_trending_view();
            } else {
                render_user_list_view(selected_list_);
            }
            
            ImGui::EndChild();
            ImGui::EndGroup();

            // Render Movie Details Popup Modal if active
            render_details_modal();
        });
    }

private:
    void init_db() {
        try {
            db_ = std::make_unique<hosting::db::sqlite>(rouen::platform::get_user_data_path("movies.db").string());
            db_->ensure_table("movie_list", 
                "id INTEGER NOT NULL, "
                "title TEXT NOT NULL, "
                "poster_path TEXT, "
                "release_date TEXT, "
                "vote_average REAL, "
                "overview TEXT, "
                "list_name TEXT NOT NULL, "
                "added_at TEXT, "
                "PRIMARY KEY(id, list_name)"
            );
            load_lists_from_db();
        } catch (const std::exception& e) {
            CONFIG_ERROR_FMT("Error initializing movies DB: {}", e.what());
        }
    }

    void load_lists_from_db() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        lists_["To Watch"].clear();
        lists_["Watched"].clear();
        lists_["Favorites"].clear();
        
        try {
            db_->exec("SELECT id, title, poster_path, release_date, vote_average, overview, list_name FROM movie_list ORDER BY added_at DESC", 
                [this](sqlite3_stmt* stmt) {
                    MovieItem movie;
                    movie.id = sqlite3_column_int64(stmt, 0);
                    
                    const char* title_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    movie.title = title_val ? title_val : "";
                    
                    const char* poster_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    movie.poster_path = poster_val ? poster_val : "";
                    
                    const char* rel_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    movie.release_date = rel_val ? rel_val : "";
                    
                    movie.vote_average = sqlite3_column_double(stmt, 4);
                    
                    const char* over_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    movie.overview = over_val ? over_val : "";
                    
                    const char* list_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                    movie.list_name = list_val ? list_val : "";
                    
                    lists_[movie.list_name].push_back(movie);
                }
            );
        } catch (const std::exception& e) {
            CONFIG_ERROR_FMT("Error loading movie lists: {}", e.what());
        }
    }

    void add_to_list(const MovieItem& movie, const std::string& target_list) {
        try {
            // Delete first to prevent constraint violations
            db_->exec("DELETE FROM movie_list WHERE id = ? AND list_name = ?", {}, movie.id, target_list);
            
            db_->exec("INSERT INTO movie_list (id, title, poster_path, release_date, vote_average, overview, list_name, added_at) VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'))", 
                {}, movie.id, movie.title, movie.poster_path, movie.release_date, movie.vote_average, movie.overview, target_list);
            
            load_lists_from_db();
        } catch (const std::exception& e) {
            try { "notify"_sfn("Failed to add movie to list: " + std::string(e.what())); } catch (...) {}
        }
    }

    void remove_from_list(long long movie_id, const std::string& list_name) {
        try {
            db_->exec("DELETE FROM movie_list WHERE id = ? AND list_name = ?", {}, movie_id, list_name);
            load_lists_from_db();
        } catch (const std::exception& e) {
            try { "notify"_sfn("Failed to remove movie: " + std::string(e.what())); } catch (...) {}
        }
    }

    void trigger_search(const std::string& query) {
        if (query.empty()) return;
        
        search_in_progress_ = true;
        std::thread([this, query]() {
            try {
                auto config = rouen::helpers::ConfigService::instance();
                std::string api_key = config->get_env("TMDB_API_KEY");
                std::string token = config->get_env("TMDB_TOKEN");
                
                std::string url = "https://api.themoviedb.org/3/search/movie?query=" + ::helpers::StringHelper::url_encode(query);
                std::vector<std::string> headers;
                if (!token.empty()) {
                    headers.push_back("Authorization: Bearer " + token);
                } else if (!api_key.empty()) {
                    url += "&api_key=" + api_key;
                }
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t doc;
                auto ec = glz::read_json(doc, response);
                if (!ec) {
                    std::vector<MovieItem> results;
                    if (doc.contains("results")) {
                        auto items = doc["results"].get<std::vector<glz::json_t>>();
                        for (const auto& item : items) {
                            MovieItem movie;
                            movie.id = item.contains("id") ? static_cast<long long>(item["id"].get<double>()) : 0;
                            movie.title = item.contains("title") ? item["title"].get<std::string>() : "";
                            movie.poster_path = (item.contains("poster_path") && item["poster_path"].is_string()) ? item["poster_path"].get<std::string>() : "";
                            movie.release_date = (item.contains("release_date") && item["release_date"].is_string()) ? item["release_date"].get<std::string>() : "";
                            movie.vote_average = item.contains("vote_average") ? item["vote_average"].get<double>() : 0.0;
                            movie.overview = (item.contains("overview") && item["overview"].is_string()) ? item["overview"].get<std::string>() : "";
                            results.push_back(movie);
                        }
                    }
                    
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    search_results_ = std::move(results);
                }
            } catch (...) {}
            search_in_progress_ = false;
        }).detach();
    }

    void fetch_trending() {
        trending_in_progress_ = true;
        std::thread([this]() {
            try {
                auto config = rouen::helpers::ConfigService::instance();
                std::string api_key = config->get_env("TMDB_API_KEY");
                std::string token = config->get_env("TMDB_TOKEN");
                
                std::string url = "https://api.themoviedb.org/3/trending/movie/week";
                std::vector<std::string> headers;
                if (!token.empty()) {
                    headers.push_back("Authorization: Bearer " + token);
                } else if (!api_key.empty()) {
                    url += "?api_key=" + api_key;
                }
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t doc;
                auto ec = glz::read_json(doc, response);
                if (!ec) {
                    std::vector<MovieItem> results;
                    if (doc.contains("results")) {
                        auto items = doc["results"].get<std::vector<glz::json_t>>();
                        for (const auto& item : items) {
                            MovieItem movie;
                            movie.id = item.contains("id") ? static_cast<long long>(item["id"].get<double>()) : 0;
                            movie.title = item.contains("title") ? item["title"].get<std::string>() : "";
                            movie.poster_path = (item.contains("poster_path") && item["poster_path"].is_string()) ? item["poster_path"].get<std::string>() : "";
                            movie.release_date = (item.contains("release_date") && item["release_date"].is_string()) ? item["release_date"].get<std::string>() : "";
                            movie.vote_average = item.contains("vote_average") ? item["vote_average"].get<double>() : 0.0;
                            movie.overview = (item.contains("overview") && item["overview"].is_string()) ? item["overview"].get<std::string>() : "";
                            results.push_back(movie);
                        }
                    }
                    
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    trending_movies_ = std::move(results);
                }
            } catch (...) {}
            trending_in_progress_ = false;
        }).detach();
    }

    void render_movie_row(const MovieItem& movie, const std::string& current_list) {
        ImGui::PushID(std::format("{}_{}", movie.id, current_list).c_str());
        
        // Horizontal Layout for Poster + Metadata
        ImGui::BeginGroup();
        
        // 1. Poster Image
        float poster_w = 50.0f;
        float poster_h = 75.0f;
        SDL_Texture* poster_tex = nullptr;
        
        if (!movie.poster_path.empty()) {
            std::string img_url = "https://image.tmdb.org/t/p/w200" + movie.poster_path;
            int w = 0, h = 0;
            poster_tex = image_cache_->getTexture(ImGui::GetIO().BackendRendererUserData ? 
                static_cast<SDL_Renderer*>(ImGui::GetIO().BackendRendererUserData) : nullptr, img_url, w, h);
        }
        
        ImVec2 start_pos = ImGui::GetCursorScreenPos();
        if (poster_tex) {
            ImGui::Image(rouen::helpers::texture_id_cast(poster_tex), ImVec2(poster_w, poster_h));
        } else {
            // Draw neat fallback box
            ImGui::GetWindowDrawList()->AddRectFilled(
                start_pos,
                ImVec2(start_pos.x + poster_w, start_pos.y + poster_h),
                ImGui::GetColorU32(ImGuiCol_FrameBg),
                4.0f
            );
            ImGui::GetWindowDrawList()->AddRect(
                start_pos,
                ImVec2(start_pos.x + poster_w, start_pos.y + poster_h),
                ImGui::GetColorU32(ImGuiCol_Border),
                4.0f
            );
            ImGui::Dummy(ImVec2(poster_w, poster_h));
        }
        
        ImGui::SameLine();
        
        // 2. Metadata Columns
        ImGui::BeginGroup();
        
        // Title Button (Opens Info Details modal)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text, colors[2]); // White/Accent
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Bold or larger font
        if (ImGui::Button(movie.title.c_str())) {
            selected_movie_details_ = movie;
            show_details_popup_ = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        
        // Year & Vote Rating
        std::string year = movie.release_date.length() >= 4 ? movie.release_date.substr(0, 4) : "Unknown";
        ImGui::TextColored(colors[4], "%s  |  ", year.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "%s %.1f", ICON_MD_STAR, movie.vote_average);
        
        // Short Overview
        std::string short_overview = movie.overview;
        if (short_overview.length() > 100) {
            short_overview = short_overview.substr(0, 97) + "...";
        }
        ImGui::TextWrapped("%s", short_overview.c_str());
        
        // Action Buttons Row
        ImGui::Spacing();
        if (current_list == "Search Movies" || current_list == "Trending") {
            if (ImGui::SmallButton(std::format(" {} Watchlist", ICON_MD_ADD).c_str())) {
                add_to_list(movie, "To Watch");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(std::format(" {} Watched", ICON_MD_DONE).c_str())) {
                add_to_list(movie, "Watched");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(std::format(" {} Favorite", ICON_MD_FAVORITE).c_str())) {
                add_to_list(movie, "Favorites");
            }
        } else {
            // User List view Action Buttons
            if (current_list == "To Watch") {
                if (ImGui::SmallButton("Mark Watched")) {
                    add_to_list(movie, "Watched");
                    remove_from_list(movie.id, "To Watch");
                }
                ImGui::SameLine();
            }
            if (current_list != "Favorites") {
                if (ImGui::SmallButton("Add Favorite")) {
                    add_to_list(movie, "Favorites");
                }
                ImGui::SameLine();
            }
            
            // Delete button
            ImGui::PushStyleColor(ImGuiCol_Text, colors[5]);
            if (ImGui::SmallButton(std::format(" {} Remove", ICON_MD_DELETE).c_str())) {
                remove_from_list(movie.id, current_list);
            }
            ImGui::PopStyleColor();
        }
        
        ImGui::EndGroup();
        ImGui::EndGroup();
        
        ImGui::Separator();
        ImGui::PopID();
    }

    void render_search_view() {
        ImGui::Text("Search TMDB Database:");
        
        // Search Input
        ImGui::PushItemWidth(-80.0f);
        bool enter_pressed = ImGui::InputText("##MovieSearchInput", search_buffer_, sizeof(search_buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        
        if (ImGui::Button("Search") || enter_pressed) {
            std::string query(search_buffer_);
            if (!query.empty() && query != last_search_query_) {
                last_search_query_ = query;
                trigger_search(query);
            }
        }
        
        ImGui::Separator();
        
        if (search_in_progress_) {
            ImGui::Text("Searching...");
            return;
        }
        
        std::vector<MovieItem> temp_results;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            temp_results = search_results_;
        }
        
        if (temp_results.empty() && !last_search_query_.empty()) {
            ImGui::Text("No movies found matching '%s'.", last_search_query_.c_str());
            return;
        }
        
        ImGui::BeginChild("SearchResultsScroll", ImVec2(-1, -1));
        for (const auto& movie : temp_results) {
            render_movie_row(movie, "Search Movies");
        }
        ImGui::EndChild();
    }

    void render_trending_view() {
        ImGui::Text("Trending Movies This Week:");
        ImGui::Separator();
        
        if (trending_in_progress_) {
            ImGui::Text("Loading trending movies...");
            return;
        }
        
        std::vector<MovieItem> temp_movies;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            temp_movies = trending_movies_;
        }
        
        ImGui::BeginChild("TrendingScroll", ImVec2(-1, -1));
        for (const auto& movie : temp_movies) {
            render_movie_row(movie, "Trending");
        }
        ImGui::EndChild();
    }

    void render_user_list_view(const std::string& list_name) {
        ImGui::Text("%s:", list_name.c_str());
        ImGui::Separator();
        
        std::vector<MovieItem> temp_movies;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            auto it = lists_.find(list_name);
            if (it != lists_.end()) {
                temp_movies = it->second;
            }
        }
        
        if (temp_movies.empty()) {
            ImGui::TextColored(colors[4], "No movies in this list yet.");
            ImGui::TextColored(colors[4], "Select 'Search Movies' to find and add films!");
            return;
        }
        
        ImGui::BeginChild("UserListScroll", ImVec2(-1, -1));
        for (const auto& movie : temp_movies) {
            render_movie_row(movie, list_name);
        }
        ImGui::EndChild();
    }

    void render_details_modal() {
        if (!show_details_popup_) return;
        
        ImGui::OpenPopup("Movie Details");
        
        // Centered Window/Popup size
        ImGui::SetNextWindowSize(ImVec2(500.0f, 380.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Movie Details", &show_details_popup_, ImGuiWindowFlags_NoResize)) {
            
            // Poster (larger size)
            float poster_w = 120.0f;
            float poster_h = 180.0f;
            SDL_Texture* poster_tex = nullptr;
            
            if (!selected_movie_details_.poster_path.empty()) {
                std::string img_url = "https://image.tmdb.org/t/p/w500" + selected_movie_details_.poster_path;
                int w = 0, h = 0;
                poster_tex = image_cache_->getTexture(ImGui::GetIO().BackendRendererUserData ? 
                    static_cast<SDL_Renderer*>(ImGui::GetIO().BackendRendererUserData) : nullptr, img_url, w, h);
            }
            
            ImGui::BeginGroup();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            if (poster_tex) {
                ImGui::Image(rouen::helpers::texture_id_cast(poster_tex), ImVec2(poster_w, poster_h));
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    start_pos,
                    ImVec2(start_pos.x + poster_w, start_pos.y + poster_h),
                    ImGui::GetColorU32(ImGuiCol_FrameBg),
                    6.0f
                );
                ImGui::Dummy(ImVec2(poster_w, poster_h));
            }
            
            // Link to TMDB page
            ImGui::Spacing();
            if (ImGui::Button("TMDB Profile", ImVec2(poster_w, 0))) {
                std::string profile_url = "https://www.themoviedb.org/movie/" + std::to_string(selected_movie_details_.id);
                auto cmd = rouen::platform::open_file(profile_url, true);
                [[maybe_unused]] int res = std::system(cmd.c_str());
            }
            ImGui::EndGroup();
            
            ImGui::SameLine();
            
            // Text Details Column
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            
            // Title
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::TextColored(colors[2], "%s", selected_movie_details_.title.c_str());
            ImGui::PopFont();
            
            // Year & Rating
            std::string year = selected_movie_details_.release_date.length() >= 4 ? 
                selected_movie_details_.release_date.substr(0, 4) : "Unknown";
            ImGui::TextColored(colors[4], "Release Year: %s", year.c_str());
            ImGui::TextColored(colors[3], "Rating: %s %.1f/10", ICON_MD_STAR, selected_movie_details_.vote_average);
            
            ImGui::Separator();
            
            // Overview Scrollable region
            ImGui::BeginChild("OverviewScroll", ImVec2(ImGui::GetContentRegionAvail().x, 150.0f), false);
            ImGui::TextWrapped("%s", selected_movie_details_.overview.c_str());
            ImGui::EndChild();
            
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            
            // Footer/Close Buttons
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40.0f);
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(80.0f, 0))) {
                show_details_popup_ = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
    }

    std::unique_ptr<hosting::db::sqlite> db_;
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    
    std::string selected_list_{"To Watch"};
    std::unordered_map<std::string, std::vector<MovieItem>> lists_;
    std::vector<MovieItem> search_results_;
    std::vector<MovieItem> trending_movies_;
    
    char search_buffer_[256]{""};
    std::string last_search_query_;
    
    bool search_in_progress_{false};
    bool trending_in_progress_{false};
    
    bool show_details_popup_{false};
    MovieItem selected_movie_details_;
    
    std::mutex data_mutex_;
};

} // namespace rouen::cards
