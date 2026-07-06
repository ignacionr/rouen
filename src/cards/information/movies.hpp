#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <optional>
#include <cstring>

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
        std::string id;
        std::string title;
        std::string poster_path; // Stored as full URL
        std::string release_date; // Stored as Year
        double vote_average{0.0}; // Stored as Rank
        std::string overview; // Stored as Notes/Overview
        std::string actors; // Stored as Cast/Actors
        std::string directors; // Stored as Director(s)
        std::string list_name;
    };

    struct LoadedMovieTexture {
        SDL_Texture* texture{nullptr};
        int width{0};
        int height{0};
    };

    movies() {
        // Setup card colors (violet primary/secondary for movie theater feel)
        colors[0] = ImVec4(0.5f, 0.3f, 0.7f, 1.0f); // Violet primary
        colors[1] = ImVec4(0.6f, 0.4f, 0.8f, 0.7f); // Lighter secondary
        
        get_color(2, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // White for titles
        get_color(3, ImVec4(0.9f, 0.8f, 0.2f, 1.0f)); // Yellow for rankings
        get_color(4, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Gray for secondary details
        get_color(5, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); // Red for delete button

        name("My Movies & Watchlists");
        width = 650.0f;
        requested_fps = 30; // Smooth UI scrolling

        // Resolve paths for ImageCache
        std::string db_path = rouen::platform::get_user_data_path("movies_images.db").string();
        std::string cache_dir = (rouen::platform::get_user_data_path() / "cache" / "movies").string();
        image_cache_ = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);

        init_db();
        fetch_trending();
    }

    ~movies() override {
        clear_loaded_textures();
    }

    void set_renderer(SDL_Renderer* r) {
        renderer_ = r;
    }

    std::string get_uri() const override {
        return "movies";
    }

    bool render() override {
        return render_window([this]() {
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

            // Render Edit Details Modal if active
            render_edit_modal();
        });
    }

private:
    void init_db() {
        try {
            db_ = std::make_unique<hosting::db::sqlite>(rouen::platform::get_user_data_path("movies.db").string());
            db_->ensure_table("movie_list_v3", 
                "id TEXT NOT NULL, "
                "title TEXT NOT NULL, "
                "poster_path TEXT, "
                "release_date TEXT, "
                "vote_average REAL, "
                "overview TEXT, "
                "actors TEXT, "
                "directors TEXT, "
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
            db_->exec("SELECT id, title, poster_path, release_date, vote_average, overview, actors, directors, list_name FROM movie_list_v3 ORDER BY added_at DESC", 
                [this](sqlite3_stmt* stmt) {
                    MovieItem movie;
                    const char* id_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    movie.id = id_val ? id_val : "";
                    
                    const char* title_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    movie.title = title_val ? title_val : "";
                    
                    const char* poster_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    movie.poster_path = poster_val ? poster_val : "";
                    
                    const char* rel_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    movie.release_date = rel_val ? rel_val : "";
                    
                    movie.vote_average = sqlite3_column_double(stmt, 4);
                    
                    const char* over_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    movie.overview = over_val ? over_val : "";

                    const char* actors_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                    movie.actors = actors_val ? actors_val : "";

                    const char* dirs_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                    movie.directors = dirs_val ? dirs_val : "";
                    
                    const char* list_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
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
            db_->exec("DELETE FROM movie_list_v3 WHERE id = ? AND list_name = ?", {}, movie.id, target_list);
            
            db_->exec("INSERT INTO movie_list_v3 (id, title, poster_path, release_date, vote_average, overview, actors, directors, list_name, added_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))", 
                {}, movie.id, movie.title, movie.poster_path, movie.release_date, movie.vote_average, movie.overview, movie.actors, movie.directors, target_list);
            
            load_lists_from_db();
        } catch (const std::exception& e) {
            try { "notify"_sfn("Failed to add movie: " + std::string(e.what())); } catch (...) {}
        }
    }

    void remove_from_list(const std::string& movie_id, const std::string& list_name) {
        try {
            db_->exec("DELETE FROM movie_list_v3 WHERE id = ? AND list_name = ?", {}, movie_id, list_name);
            load_lists_from_db();
        } catch (const std::exception& e) {
            try { "notify"_sfn("Failed to remove movie: " + std::string(e.what())); } catch (...) {}
        }
    }

    void save_movie_edits(const MovieItem& movie) {
        try {
            db_->exec("UPDATE movie_list_v3 SET title = ?, release_date = ?, vote_average = ?, directors = ?, actors = ?, overview = ? WHERE id = ? AND list_name = ?",
                {}, 
                std::string(edit_title_), 
                std::string(edit_year_), 
                edit_rating_, 
                std::string(edit_directors_), 
                std::string(edit_actors_), 
                std::string(edit_overview_), 
                movie.id, 
                movie.list_name
            );
            load_lists_from_db();
        } catch (const std::exception& e) {
            try { "notify"_sfn("Failed to save edits: " + std::string(e.what())); } catch (...) {}
        }
    }

    void trigger_search(const std::string& query) {
        if (query.empty()) return;
        
        search_in_progress_ = true;
        std::thread([this, query]() {
            try {
                std::string url = "https://imdb.iamidiotareyoutoo.com/search?q=" + ::helpers::StringHelper::url_encode(query);
                std::vector<std::string> headers = {"accept: application/json"};
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t doc;
                auto ec = glz::read_json(doc, response);
                if (!ec) {
                    std::vector<MovieItem> results;
                    if (doc.contains("description")) {
                        auto items = doc["description"].get<std::vector<glz::json_t>>();
                        for (const auto& item : items) {
                            MovieItem movie;
                            movie.id = item.contains("#IMDB_ID") ? item["#IMDB_ID"].get<std::string>() : "";
                            movie.title = item.contains("#TITLE") ? item["#TITLE"].get<std::string>() : "";
                            movie.poster_path = (item.contains("#IMG_POSTER") && item["#IMG_POSTER"].is_string()) ? item["#IMG_POSTER"].get<std::string>() : "";
                            
                            if (item.contains("#YEAR")) {
                                if (item["#YEAR"].is_number()) {
                                    movie.release_date = std::to_string(static_cast<int>(item["#YEAR"].get<double>()));
                                } else if (item["#YEAR"].is_string()) {
                                    movie.release_date = item["#YEAR"].get<std::string>();
                                }
                            }
                            
                            movie.vote_average = item.contains("#RANK") ? item["#RANK"].get<double>() : 0.0;
                            
                            // IMDb unofficial search returns actors in #ACTORS, map to actors field
                            movie.actors = (item.contains("#ACTORS") && item["#ACTORS"].is_string()) ? item["#ACTORS"].get<std::string>() : "";
                            
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
                // "the" query acts as a trending query because it fetches the most popular IMDb-ranked movies
                std::string url = "https://imdb.iamidiotareyoutoo.com/search?q=the";
                std::vector<std::string> headers = {"accept: application/json"};
                
                std::string response = http::fetch()(url, headers);
                
                glz::json_t doc;
                auto ec = glz::read_json(doc, response);
                if (!ec) {
                    std::vector<MovieItem> results;
                    if (doc.contains("description")) {
                        auto items = doc["description"].get<std::vector<glz::json_t>>();
                        for (const auto& item : items) {
                            MovieItem movie;
                            movie.id = item.contains("#IMDB_ID") ? item["#IMDB_ID"].get<std::string>() : "";
                            movie.title = item.contains("#TITLE") ? item["#TITLE"].get<std::string>() : "";
                            movie.poster_path = (item.contains("#IMG_POSTER") && item["#IMG_POSTER"].is_string()) ? item["#IMG_POSTER"].get<std::string>() : "";
                            
                            if (item.contains("#YEAR")) {
                                if (item["#YEAR"].is_number()) {
                                    movie.release_date = std::to_string(static_cast<int>(item["#YEAR"].get<double>()));
                                } else if (item["#YEAR"].is_string()) {
                                    movie.release_date = item["#YEAR"].get<std::string>();
                                }
                            }
                            
                            movie.vote_average = item.contains("#RANK") ? item["#RANK"].get<double>() : 0.0;
                            movie.actors = (item.contains("#ACTORS") && item["#ACTORS"].is_string()) ? item["#ACTORS"].get<std::string>() : "";
                            
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

    void clear_loaded_textures() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (auto& [url, lt] : loaded_textures_) {
            if (lt.texture) {
                SDL_DestroyTexture(lt.texture);
            }
        }
        loaded_textures_.clear();
    }

    SDL_Texture* get_movie_poster_texture(SDL_Renderer* renderer, const std::string& url, int& w, int& h) {
        if (url.empty()) return nullptr;
        
        // 1. Check in-memory loaded textures
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            auto it = loaded_textures_.find(url);
            if (it != loaded_textures_.end()) {
                w = it->second.width;
                h = it->second.height;
                return it->second.texture;
            }
        }
        
        // 2. Check if already downloaded/cached on disk
        int cached_w = 0, cached_h = 0;
        if (image_cache_->isCached(url, cached_w, cached_h)) {
            // Load into texture (runs on render thread but only once per image life!)
            SDL_Texture* tex = image_cache_->getTexture(renderer, url, w, h);
            if (tex) {
                std::lock_guard<std::mutex> lock(data_mutex_);
                loaded_textures_[url] = {tex, w, h};
                return tex;
            }
        } else {
            // 3. Trigger background download
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
        
        return nullptr;
    }

    void render_movie_row(const MovieItem& movie, const std::string& current_list) {
        ImGui::PushID(std::format("{}_{}", movie.id, current_list).c_str());
        
        // Horizontal Layout for Poster + Metadata
        ImGui::BeginGroup();
        
        // 1. Poster Image (Non-blocking lazy load)
        float poster_w = 50.0f;
        float poster_h = 75.0f;
        int w = 0, h = 0;
        
        SDL_Texture* poster_tex = get_movie_poster_texture(renderer_, movie.poster_path, w, h);
        
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
        
        // Year & Rank/Rating
        ImGui::TextColored(colors[4], "%s  |  ", movie.release_date.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "Rank/Rating: %.0f", movie.vote_average);

        // Director
        if (!movie.directors.empty()) {
            ImGui::TextColored(colors[4], "Dir: %s", movie.directors.c_str());
        }
        
        // Actors
        if (!movie.actors.empty()) {
            std::string short_cast = movie.actors;
            if (short_cast.length() > 60) {
                short_cast = short_cast.substr(0, 57) + "...";
            }
            ImGui::TextWrapped("Cast: %s", short_cast.c_str());
        }
        
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

            // Edit button (Manually input details)
            if (ImGui::SmallButton(std::format(" {} Edit", ICON_MD_EDIT).c_str())) {
                edit_movie_details_ = movie;
                strncpy(edit_title_, movie.title.c_str(), sizeof(edit_title_) - 1);
                strncpy(edit_year_, movie.release_date.c_str(), sizeof(edit_year_) - 1);
                edit_rating_ = movie.vote_average;
                strncpy(edit_directors_, movie.directors.c_str(), sizeof(edit_directors_) - 1);
                strncpy(edit_actors_, movie.actors.c_str(), sizeof(edit_actors_) - 1);
                strncpy(edit_overview_, movie.overview.c_str(), sizeof(edit_overview_) - 1);
                show_edit_popup_ = true;
            }
            ImGui::SameLine();
            
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
        ImGui::Text("Search IMDb Database:");
        
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
        ImGui::Text("Popular Movies (IMDb):");
        ImGui::Separator();
        
        if (trending_in_progress_) {
            ImGui::Text("Loading popular movies...");
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
            int w = 0, h = 0;
            SDL_Texture* poster_tex = get_movie_poster_texture(renderer_, selected_movie_details_.poster_path, w, h);
            
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
            
            // Link to IMDb profile page
            ImGui::Spacing();
            if (ImGui::Button("IMDb Profile", ImVec2(poster_w, 0))) {
                std::string profile_url = "https://imdb.com/title/" + selected_movie_details_.id;
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
            
            // Year & Rating/Rank
            ImGui::TextColored(colors[4], "Release Year: %s", selected_movie_details_.release_date.c_str());
            ImGui::TextColored(colors[3], "Rating/Rank: %.0f", selected_movie_details_.vote_average);
            
            ImGui::Separator();
            
            // Scrollable region for Cast, Directors, and Notes
            ImGui::BeginChild("OverviewScroll", ImVec2(ImGui::GetContentRegionAvail().x, 150.0f), false);
            
            if (!selected_movie_details_.directors.empty()) {
                ImGui::TextColored(colors[2], "Director(s):");
                ImGui::TextWrapped("%s", selected_movie_details_.directors.c_str());
                ImGui::Spacing();
            }

            if (!selected_movie_details_.actors.empty()) {
                ImGui::TextColored(colors[2], "Starring:");
                ImGui::TextWrapped("%s", selected_movie_details_.actors.c_str());
                ImGui::Spacing();
            }

            if (!selected_movie_details_.overview.empty()) {
                ImGui::TextColored(colors[2], "Notes / Description:");
                ImGui::TextWrapped("%s", selected_movie_details_.overview.c_str());
            }
            
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

    void render_edit_modal() {
        if (!show_edit_popup_) return;

        ImGui::OpenPopup("Edit Movie Details");

        ImGui::SetNextWindowSize(ImVec2(520.0f, 400.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Edit Movie Details", &show_edit_popup_, ImGuiWindowFlags_NoResize)) {
            
            ImGui::TextColored(colors[2], "Edit metadata for: %s", edit_movie_details_.title.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::InputText("Title", edit_title_, sizeof(edit_title_));
            ImGui::InputText("Release Year", edit_year_, sizeof(edit_year_));
            ImGui::InputDouble("Rank / Rating", &edit_rating_, 1.0, 10.0, "%.0f");
            ImGui::InputText("Director(s)", edit_directors_, sizeof(edit_directors_));
            
            ImGui::Text("Cast / Actors:");
            ImGui::InputTextMultiline("##EditActors", edit_actors_, sizeof(edit_actors_), ImVec2(-1, 60));

            ImGui::Text("Notes / Storyline:");
            ImGui::InputTextMultiline("##EditNotes", edit_overview_, sizeof(edit_overview_), ImVec2(-1, 80));

            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(100.0f, 0))) {
                save_movie_edits(edit_movie_details_);
                show_edit_popup_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0))) {
                show_edit_popup_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    std::unique_ptr<hosting::db::sqlite> db_;
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    SDL_Renderer* renderer_{nullptr};
    
    std::string selected_list_{"To Watch"};
    std::unordered_map<std::string, std::vector<MovieItem>> lists_;
    std::vector<MovieItem> search_results_;
    std::vector<MovieItem> trending_movies_;
    
    // In-memory texture cache to prevent disk loading on every frame
    std::unordered_map<std::string, LoadedMovieTexture> loaded_textures_;
    std::unordered_set<std::string> downloading_urls_;
    
    char search_buffer_[256]{""};
    std::string last_search_query_;
    
    bool search_in_progress_{false};
    bool trending_in_progress_{false};
    
    bool show_details_popup_{false};
    MovieItem selected_movie_details_;

    // Edit Buffer Fields
    bool show_edit_popup_{false};
    MovieItem edit_movie_details_;
    char edit_title_[256]{""};
    char edit_year_[64]{""};
    double edit_rating_{0.0};
    char edit_directors_[256]{""};
    char edit_actors_[512]{""};
    char edit_overview_[1024]{""};
    
    std::mutex data_mutex_;
    std::mutex downloading_mutex_;
};

} // namespace rouen::cards
