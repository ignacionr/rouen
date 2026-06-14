#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../registrar.hpp"
#include "../../helpers/fetch.hpp"
#include <glaze/glaze.hpp>
#include "../../helpers/flag_renderer.hpp"
#include "../../helpers/image_cache.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <format>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <ctime>
#include <atomic>
#include <cctype>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include "../../helpers/llm_config.hpp"
#include "../../helpers/platform_utils.hpp"

namespace rouen::cards {

class worldcup_dashboard : public card {
public:
    struct Match {
        std::string group;
        std::string home_team;
        std::string home_code;
        std::string away_team;
        std::string away_code;
        int home_score;
        int away_score;
        std::string status; // "COMPLETED", "LIVE", "UPCOMING"
        std::string date_str;
        std::string venue;
        std::string time_str;
        std::string home_scorers;
        std::string away_scorers;
        std::string stadium_id;
    };

    struct GroupTeam {
        std::string name;
        std::string code;
        int played;
        int won;
        int drawn;
        int lost;
        int gd;
        int points;
    };

    struct TeamInfo {
        std::string name;
        std::string code;
        std::string group;
    };

    struct CommentaryEvent {
        int minute;
        std::string type; // "GOAL", "CARD", "CHANCE", "INFO", "START", "END"
        std::string text;
    };

    struct CommentaryCache {
        std::string text;
        int64_t last_updated_epoch = 0;
        int last_home_score = -1;
        int last_away_score = -1;
        std::string status;
    };

    struct PlayerInfo {
        std::string name;
        std::string photo_url;
        std::string position;
        int jersey_number = 0;
        std::string comment;
    };

    struct LineupPlayer {
        std::string name;
        std::string position;
        int jersey_number = 0;
    };

    struct QAPair {
        std::string question;
        std::string answer;
    };

    struct TeamPlayersCache {
        std::vector<PlayerInfo> players;
        std::vector<LineupPlayer> lineup;
        std::vector<QAPair> qa;
        int64_t last_updated_epoch = 0;
    };

    worldcup_dashboard(std::string_view locator = "") {
        // Set World Cup theme colors: vibrant pitch green and gold accents
        colors[0] = {0.09f, 0.45f, 0.27f, 1.0f}; // Pitch Green
        colors[1] = {0.85f, 0.68f, 0.21f, 0.7f}; // Gold Accent
        
        get_color(2, ImVec4(1.0f, 0.84f, 0.0f, 1.0f));  // Vibrant Gold
        get_color(3, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));   // Success Green
        get_color(4, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));   // Alert Red
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Gray secondary text
        get_color(6, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));   // Info Blue
        get_color(7, ImVec4(0.12f, 0.12f, 0.14f, 0.9f)); // Dark background card

        name("FIFA World Cup 2026");
        width = 560.0f;
        requested_fps = 1; // 1 FPS since simulation has been removed

        // Load persistent commentary cache from disk
        load_commentary_cache_from_disk();
        load_team_players_cache_from_disk();

        if (!locator.empty()) {
            handle_uri("worldcup:" + std::string(locator));
        }

        // Start Background Data Fetch
        fetch_real_data();
    }

    ~worldcup_dashboard() override {
        stop_speaking();
        clear_player_textures();
    }

    bool render() override {
        // Periodic background fetch every 30 seconds
        auto now = std::chrono::steady_clock::now();
        bool loaded = false;
        std::vector<Match> display_matches;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            loaded = data_loaded;
            display_matches = api_matches;
        }
        if (!loaded || std::chrono::duration_cast<std::chrono::seconds>(now - last_fetch_time).count() >= 30) {
            last_fetch_time = now;
            fetch_real_data();
        }

        // Periodic commentary refresh check for expanded live matches
        std::string current_expanded_key;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            current_expanded_key = expanded_match_key_;
        }

        if (!current_expanded_key.empty()) {
            const Match* expanded_match = nullptr;
            for (const auto& m : display_matches) {
                std::string key = m.home_code + "_" + m.away_code + "_" + m.date_str;
                if (key == current_expanded_key) {
                    expanded_match = &m;
                    break;
                }
            }

            if (expanded_match && expanded_match->status == "LIVE") {
                bool need_refresh = false;
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    if (commentary_cache_.contains(current_expanded_key)) {
                        auto cache = commentary_cache_[current_expanded_key];
                        auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count();
                        auto elapsed = now_epoch - cache.last_updated_epoch;
                        if (elapsed >= 300) {
                            need_refresh = true;
                        }
                    } else {
                        need_refresh = true;
                    }
                }
                if (need_refresh) {
                    fetch_commentary_async(current_expanded_key, *expanded_match);
                }
            }
        }
        
        // Detect score changes to trigger goals flash animations
        for (const auto& m : display_matches) {
            std::string match_key = m.home_code + "_" + m.away_code + "_" + m.date_str;
            if (last_known_scores.contains(match_key)) {
                auto prev = last_known_scores[match_key];
                if (m.home_score > prev.first) {
                    match_flashes[match_key].home_flash_timer = 5.0f;
                    last_known_scores[match_key].first = m.home_score;
                }
                if (m.away_score > prev.second) {
                    match_flashes[match_key].away_flash_timer = 5.0f;
                    last_known_scores[match_key].second = m.away_score;
                }
                if (m.home_score < prev.first) {
                    last_known_scores[match_key].first = m.home_score;
                }
                if (m.away_score < prev.second) {
                    last_known_scores[match_key].second = m.away_score;
                }
            } else {
                last_known_scores[match_key] = {m.home_score, m.away_score};
                match_flashes[match_key] = {m.home_score, m.away_score, 0.0f, 0.0f};
            }
        }
        
        // Update flash timers using DeltaTime
        float dt = ImGui::GetIO().DeltaTime;
        bool any_flash_active = false;
        for (auto& [key, flash] : match_flashes) {
            if (flash.home_flash_timer > 0.0f) {
                flash.home_flash_timer -= dt;
                if (flash.home_flash_timer < 0.0f) flash.home_flash_timer = 0.0f;
                any_flash_active = true;
            }
            if (flash.away_flash_timer > 0.0f) {
                flash.away_flash_timer -= dt;
                if (flash.away_flash_timer < 0.0f) flash.away_flash_timer = 0.0f;
                any_flash_active = true;
            }
        }
        
        // Temporarily boost framerate to 60 FPS for smooth fades, else stay at power-saving 1 FPS
        requested_fps = any_flash_active ? 60 : 1;
        
        return render_window([this]() {
            ImGui::Spacing();
            
            if (ImGui::BeginTabBar("WorldCupTabs", ImGuiTabBarFlags_None)) {
                ImGuiTabItemFlags mc_flags = ImGuiTabItemFlags_None;
                if (set_match_center_selected) {
                    mc_flags |= ImGuiTabItemFlags_SetSelected;
                    set_match_center_selected = false; // Reset so user can switch normally
                }
                if (ImGui::BeginTabItem(ICON_MD_SPORTS_SOCCER " Match Center", nullptr, mc_flags)) {
                    render_match_center();
                    ImGui::EndTabItem();
                }
                ImGuiTabItemFlags std_flags = ImGuiTabItemFlags_None;
                if (set_standings_selected) {
                    std_flags |= ImGuiTabItemFlags_SetSelected;
                    set_standings_selected = false; // Reset
                }
                if (ImGui::BeginTabItem(ICON_MD_FORMAT_LIST_BULLETED " Standings", nullptr, std_flags)) {
                    render_standings();
                    ImGui::EndTabItem();
                }
                ImGuiTabItemFlags tracker_flags = ImGuiTabItemFlags_None;
                if (set_team_tracker_selected) {
                    tracker_flags |= ImGuiTabItemFlags_SetSelected;
                    set_team_tracker_selected = false; // Reset
                }
                if (ImGui::BeginTabItem(ICON_MD_TRACK_CHANGES " Team Tracker", nullptr, tracker_flags)) {
                    render_team_tracker();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_MAP " Stadiums")) {
                    render_stadiums();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        });
    }

    std::string get_uri() const override {
        return "worldcup";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "worldcup" || uri.starts_with("worldcup:");
    }

    void handle_uri(std::string_view uri) override {
        std::string_view locator = uri;
        auto colon_pos = uri.find(':');
        if (colon_pos != std::string_view::npos) {
            locator = uri.substr(colon_pos + 1);
        }
        if (locator.starts_with("match:")) {
            locator = locator.substr(6);
        }
        if (!locator.empty()) {
            std::lock_guard<std::mutex> lock(data_mutex);
            expanded_match_key_ = std::string(locator);
            set_match_center_selected = true;
            need_scroll_to_match = true;
        }
    }

    SDL_Renderer* renderer = nullptr;
    std::shared_ptr<::helpers::ImageCache> image_cache;

    void set_renderer(SDL_Renderer* r) {
        renderer = r;
        if (renderer) {
            auto db_path = rouen::platform::get_user_data_path("worldcup_images.db").string();
            auto cache_dir = rouen::platform::get_user_data_path("cache/worldcup_images").string();
            image_cache = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
        }
    }

private:
    // Real API Data Variables
    std::vector<Match> api_matches;
    std::unordered_map<std::string, std::vector<GroupTeam>> api_groups;
    std::unordered_map<std::string, TeamInfo> api_teams;
    bool data_loaded = false;
    std::mutex data_mutex;
    std::atomic<bool> is_fetching{false};
    std::chrono::steady_clock::time_point last_fetch_time = std::chrono::steady_clock::now();
    
    std::unordered_map<std::string, std::pair<int, int>> last_known_scores;
    struct MatchFlash {
        int last_home_score = -1;
        int last_away_score = -1;
        float home_flash_timer = 0.0f;
        float away_flash_timer = 0.0f;
    };
    std::unordered_map<std::string, MatchFlash> match_flashes;

    std::unordered_map<std::string, CommentaryCache> commentary_cache_;
    std::unordered_set<std::string> fetching_matches_;
    std::string expanded_match_key_;
    bool set_match_center_selected = false;
    bool need_scroll_to_match = false;
    bool set_standings_selected = false;
    int selected_group_idx = 3; // Defaults to Group D (USA)
    bool set_team_tracker_selected = false;
    std::string tracker_selected_team_code = "";
    int tracker_selected_idx = 0;
    int tracker_last_selected_idx = -1;

    std::unordered_map<std::string, TeamPlayersCache> team_players_cache_;
    std::unordered_set<std::string> fetching_players_;
    struct LoadedTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, LoadedTexture> player_textures_;
    std::unordered_set<std::string> fetching_player_photos_;

    void fetch_commentary_async(const std::string& match_key, const Match& m) {
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (fetching_matches_.contains(match_key)) {
                return;
            }
            fetching_matches_.insert(match_key);
        }

        std::jthread([this, match_key, m]() {
            try {
                auto llm_instance = helpers::LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    throw std::runtime_error("LLM not configured");
                }

                auto settings = helpers::LLMConfig::get_current_config();
                auto fetcher = std::make_shared<http::fetch>();

                std::string prompt;
                if (m.status == "LIVE") {
                    prompt = std::format(
                        "You are a legendary, extremely passionate Spanish-style football commentator broadcasting live. "
                        "Commentate on the match between {} and {}.\n"
                        "Current Score: {} {} - {} {}.\n"
                        "Match Status: {} ({}).\n"
                        "Home Scorers: {}. Away Scorers: {}.\n"
                        "Venue: {}.\n\n"
                        "CRITICAL: Use your web search capabilities to find real-time/recent information about this match from news, live text commentaries, stats sites, or fan hubs. "
                        "Find events and stats beyond just who scored the goals: look for yellow/red cards, shots on target, possession, fouls, outstanding player performances, tactical changes, or key saves. "
                        "Commentate dynamically on these actual match events, focusing specifically on the events of the last 5 minutes of action! "
                        "Make it sound incredibly exciting, dramatic, and full of energy (feel free to use words like 'GOOOOOAL', 'Incredible', 'What a match!'). "
                        "Provide a short, intense commentary of about 3-4 paragraphs.",
                        m.home_team, m.away_team,
                        m.home_team, m.home_score, m.away_score, m.away_team,
                        m.status, m.time_str,
                        m.home_scorers.empty() ? "None" : m.home_scorers,
                        m.away_scorers.empty() ? "None" : m.away_scorers,
                        m.venue
                    );
                } else if (m.status == "COMPLETED") {
                    prompt = std::format(
                        "You are a legendary, extremely passionate football commentator.\n"
                        "Write a dramatic, highly enthusiastic summary commentary of the completed match between {} and {}.\n"
                        "Final Score: {} {} - {} {}.\n"
                        "Home Scorers: {}. Away Scorers: {}.\n"
                        "Venue: {}.\n\n"
                        "CRITICAL:\n"
                        "1. Use your web search capabilities to discover specific match events (yellow/red cards, dramatic saves, referee decisions, stats like possession/shots, player injuries, or post-match comments/reactions). "
                        "Include these real match occurrences to enrich the commentary beyond just the goal scorers.\n"
                        "2. Search the web to find an official or high-quality YouTube highlights/summary video link of this specific match ('{} vs {}' played on or around {}). "
                        "Include the YouTube link clearly at the end of the commentary so the user can watch the summary.\n\n"
                        "Write with immense energy and passion! Provide a commentary of about 3-4 paragraphs followed by the YouTube link section.",
                        m.home_team, m.away_team,
                        m.home_team, m.home_score, m.away_score, m.away_team,
                        m.home_scorers.empty() ? "None" : m.home_scorers,
                        m.away_scorers.empty() ? "None" : m.away_scorers,
                        m.venue,
                        m.home_team, m.away_team, m.date_str
                    );
                } else {
                    prompt = std::format(
                        "You are a legendary, extremely passionate football commentator.\n"
                        "Provide an exciting, highly enthusiastic preview commentary for the upcoming match between {} and {} "
                        "at the venue {}.\n\n"
                        "CRITICAL: Use your web search capabilities to find recent news, injuries, historical head-to-head records, expected lineups, or press conference quotes for this matchup. "
                        "Incorporate these details to build up real anticipation and hype!\n\n"
                        "Build up the hype, talk about key players and the drama of this World Cup fixture in a passionate way! "
                        "Provide a commentary of about 3-4 paragraphs.",
                        m.home_team, m.away_team, m.venue
                    );
                }

                std::string search_mode_str = "on"; // Enable web search/grounding for all compatible models (Grok, Gemini, etc.)

                auto response = llm_instance->sendMessage(
                    prompt,
                    [fetcher](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher->post(url, data, header_client);
                    },
                    "user",
                    settings.model_name,
                    search_mode_str,
                    0.85f
                );

                std::string result_text;
                if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                    result_text = response.choices[0].message.content;
                } else {
                    result_text = "The commentator is temporarily speechless! (AI returned an empty response)";
                }

                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    CommentaryCache cache;
                    cache.text = result_text;
                    cache.last_updated_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                    cache.last_home_score = m.home_score;
                    cache.last_away_score = m.away_score;
                    cache.status = m.status;
                    commentary_cache_[match_key] = cache;
                    fetching_matches_.erase(match_key);
                }
                save_commentary_cache_to_disk();
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error in AI commentary request: {}", e.what());
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    CommentaryCache cache;
                    cache.text = std::format("Error fetching commentary: {}", e.what());
                    cache.last_updated_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                    cache.last_home_score = m.home_score;
                    cache.last_away_score = m.away_score;
                    cache.status = m.status;
                    commentary_cache_[match_key] = cache;
                    fetching_matches_.erase(match_key);
                }
                save_commentary_cache_to_disk();
            }
        }).detach();
    }

    void handle_commentary_click(const std::string& match_key, const Match& m) {
        bool need_fetch = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (expanded_match_key_ == match_key) {
                expanded_match_key_.clear();
            } else {
                expanded_match_key_ = match_key;
                if (!commentary_cache_.contains(match_key)) {
                    need_fetch = true;
                } else {
                    auto cache = commentary_cache_[match_key];
                    if (m.status == "LIVE") {
                        auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count();
                        auto elapsed = now_epoch - cache.last_updated_epoch;
                        if (elapsed >= 300) {
                            need_fetch = true;
                        }
                    }
                }
            }
        }
        if (need_fetch) {
            fetch_commentary_async(match_key, m);
        }
    }

    std::vector<std::string> extract_urls(const std::string& text) {
        std::vector<std::string> urls;
        size_t pos = 0;
        while (true) {
            pos = text.find("http", pos);
            if (pos == std::string::npos) {
                break;
            }
            if (pos + 4 < text.size() && (text.substr(pos, 7) == "http://" || text.substr(pos, 8) == "https://")) {
                size_t end_pos = pos;
                while (end_pos < text.size()) {
                    char c = text[end_pos];
                    if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == '`' || c == '<' || c == '>' || c == '[' || c == ']' || c == '(' || c == ')') {
                        break;
                    }
                    end_pos++;
                }
                std::string url = text.substr(pos, end_pos - pos);
                while (!url.empty() && (url.back() == '.' || url.back() == ',' || url.back() == '?' || url.back() == '!')) {
                    url.pop_back();
                }
                if (!url.empty()) {
                    if (std::find(urls.begin(), urls.end(), url) == urls.end()) {
                        urls.push_back(url);
                    }
                }
                pos = end_pos;
            } else {
                pos += 4;
            }
        }
        return urls;
    }

    std::string speaking_match_key_;

    void stop_speaking() {
        rouen::platform::stop_speech();
        speaking_match_key_.clear();
    }

    void say_commentary_async(const std::string& match_key, const std::string& text) {
        speaking_match_key_ = match_key;
        rouen::platform::speak_text_async(text, [this, match_key]() {
            if (speaking_match_key_ == match_key) {
                speaking_match_key_.clear();
            }
        });
    }

    void save_commentary_cache_to_disk() {
        try {
            std::string filepath = rouen::platform::get_user_data_path("worldcup_commentary_cache.json").string();
            std::string json_str;
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                [[maybe_unused]] auto ec = glz::write_json(commentary_cache_, json_str);
            }
            std::ofstream out(filepath, std::ios::out | std::ios::trunc);
            if (out.is_open()) {
                out << json_str;
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Failed to save commentary cache: {}", e.what());
        }
    }

    void load_commentary_cache_from_disk() {
        try {
            std::string filepath = rouen::platform::get_user_data_path("worldcup_commentary_cache.json").string();
            if (!std::filesystem::exists(filepath)) {
                return;
            }
            std::ifstream in(filepath);
            if (!in.is_open()) {
                return;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string json_str = ss.str();
            std::unordered_map<std::string, CommentaryCache> temp_cache;
            auto ec = glz::read_json(temp_cache, json_str);
            if (!ec) {
                std::lock_guard<std::mutex> lock(data_mutex);
                commentary_cache_ = std::move(temp_cache);
                DB_INFO("Loaded World Cup AI commentary cache from disk");
            } else {
                DB_WARN_FMT("Failed to parse commentary cache JSON: {}", glz::format_error(ec, json_str));
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Failed to load commentary cache: {}", e.what());
        }
    }

    void save_team_players_cache_to_disk() {
        try {
            std::string filepath = rouen::platform::get_user_data_path("worldcup_team_players_cache.json").string();
            std::string json_str;
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                [[maybe_unused]] auto ec = glz::write_json(team_players_cache_, json_str);
            }
            std::ofstream out(filepath, std::ios::out | std::ios::trunc);
            if (out.is_open()) {
                out << json_str;
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Failed to save team players cache: {}", e.what());
        }
    }

    void load_team_players_cache_from_disk() {
        try {
            std::string filepath = rouen::platform::get_user_data_path("worldcup_team_players_cache.json").string();
            if (!std::filesystem::exists(filepath)) {
                return;
            }
            std::ifstream in(filepath);
            if (!in.is_open()) {
                return;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string json_str = ss.str();
            std::unordered_map<std::string, TeamPlayersCache> temp_cache;
            auto ec = glz::read_json(temp_cache, json_str);
            if (!ec) {
                std::lock_guard<std::mutex> lock(data_mutex);
                team_players_cache_ = std::move(temp_cache);
                DB_INFO("Loaded World Cup team players cache from disk");
            } else {
                DB_WARN_FMT("Failed to parse team players cache JSON: {}", glz::format_error(ec, json_str));
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Failed to load team players cache: {}", e.what());
        }
    }

    std::string strip_json_markdown(const std::string& input) {
        std::string result = input;
        
        // Remove leading/trailing spaces
        size_t start = result.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            result = result.substr(start);
        }
        
        // Check for markdown code block starts
        if (result.starts_with("```json")) {
            result = result.substr(7);
        } else if (result.starts_with("```")) {
            result = result.substr(3);
        }
        
        // Find the last ``` and remove it
        size_t end_code = result.rfind("```");
        if (end_code != std::string::npos) {
            result = result.substr(0, end_code);
        }
        
        // Trim again
        start = result.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            result = result.substr(start);
        }
        size_t end_val = result.find_last_not_of(" \t\n\r");
        if (end_val != std::string::npos) {
            result = result.substr(0, end_val + 1);
        }
        
        return result;
    }

    void clear_player_textures() {
        for (auto& [url, lt] : player_textures_) {
            if (lt.texture) {
                SDL_DestroyTexture(lt.texture);
            }
        }
        player_textures_.clear();
    }

    void fetch_team_players_async(const std::string& team_code, const std::string& team_name) {
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (fetching_players_.contains(team_code)) {
                return;
            }
            fetching_players_.insert(team_code);
        }

        std::jthread([this, team_code, team_name]() {
            try {
                auto llm_instance = helpers::LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    throw std::runtime_error("LLM not configured");
                }

                auto settings = helpers::LLMConfig::get_current_config();
                auto fetcher = std::make_shared<http::fetch>();

                std::string prompt = std::format(
                    "You are an expert football analyst.\n"
                    "Provide detailed information for the national football team: {}.\n\n"
                    "You must return ONLY a raw JSON object matching this schema. Do not include any commentary, intro, or markdown formatting outside the JSON code block.\n\n"
                    "CRITICAL REQUIREMENTS:\n"
                    "1. Use your web search capabilities to find the latest active national team roster (as of 2026/current year).\n"
                    "2. Key Players ('players'): Provide the 5 most important/famous players in the current roster. "
                    "(Note: Leave the 'photo_url' field as an empty string \"\").\n"
                    "3. Full Lineup ('lineup'): Provide the expected starting XI (11 players) representing their latest lineup in a standard formation (e.g. 4-3-3 or 4-4-2). For each, include name, position (Goalkeeper/Defender/Midfielder/Forward), and jersey number.\n"
                    "4. Q&A ('qa'): Provide 3 interesting questions and answers regarding the team's current form, history, or keys to success for the 2026 World Cup campaign.\n\n"
                    "JSON Schema:\n"
                    "{{\n"
                    "  \"players\": [\n"
                    "    {{\n"
                    "      \"name\": \"Player Name\",\n"
                    "      \"photo_url\": \"URL to player photo\",\n"
                    "      \"position\": \"Goalkeeper/Defender/Midfielder/Forward\",\n"
                    "      \"jersey_number\": 10,\n"
                    "      \"comment\": \"Short description of their role/importance in the team today\"\n"
                    "    }}\n"
                    "  ],\n"
                    "  \"lineup\": [\n"
                    "    {{\n"
                    "      \"name\": \"Player Name\",\n"
                    "      \"position\": \"Goalkeeper/Defender/Midfielder/Forward\",\n"
                    "      \"jersey_number\": 1\n"
                    "    }}\n"
                    "  ],\n"
                    "  \"qa\": [\n"
                    "    {{\n"
                    "      \"question\": \"Question about the team?\",\n"
                    "      \"answer\": \"Detailed answer about the team.\"\n"
                    "    }}\n"
                    "  ]\n"
                    "}}\n",
                    team_name
                );

                std::string search_mode_str = "on"; // Enable web search to get real photos/details

                auto response = llm_instance->sendMessage(
                    prompt,
                    [fetcher](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher->post(url, data, header_client);
                    },
                    "user",
                    settings.model_name,
                    search_mode_str,
                    0.45f
                );

                std::string result_text;
                if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                    result_text = response.choices[0].message.content;
                } else {
                    throw std::runtime_error("AI returned an empty response");
                }

                // Strip markdown code block wrappers
                std::string clean_json = strip_json_markdown(result_text);

                TeamPlayersCache cache;
                auto ec = glz::read_json(cache, clean_json);
                if (ec) {
                    throw std::runtime_error(std::format("JSON parse error: {}", glz::format_error(ec, clean_json)));
                }

                cache.last_updated_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    team_players_cache_[team_code] = cache;
                    fetching_players_.erase(team_code);
                }
                save_team_players_cache_to_disk();
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error in AI team players request: {}", e.what());
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    fetching_players_.erase(team_code);
                    
                    // Store error as a comment in a single player entry so the UI can show the error state
                    TeamPlayersCache cache;
                    PlayerInfo err_player;
                    err_player.name = "Error Fetching Squad";
                    err_player.comment = std::format("Failed to load: {}. Please try again.", e.what());
                    err_player.position = "System Error";
                    cache.players.push_back(err_player);
                    cache.last_updated_epoch = 0; // Trigger reload
                    team_players_cache_[team_code] = cache;
                }
            }
        }).detach();
    }

    void fetch_player_photo_async(const std::string& team_code, const std::string& team_name, size_t player_idx, const std::string& player_name) {
        std::string search_key = team_code + "_" + player_name;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (fetching_player_photos_.contains(search_key)) {
                return;
            }
            fetching_player_photos_.insert(search_key);
        }

        std::jthread([this, team_code, team_name, player_idx, player_name, search_key]() {
            try {
                auto llm_instance = helpers::LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    throw std::runtime_error("LLM not configured");
                }

                auto settings = helpers::LLMConfig::get_current_config();
                auto fetcher = std::make_shared<http::fetch>();

                std::string prompt = std::format(
                    "You are a football data assistant.\n"
                    "Provide exactly 5 different candidate direct image URLs of the football player '{}' (who plays for the national team '{}').\n"
                    "The URLs must be direct links to images (ending in .jpg, .jpeg, .png, etc.) from different open sources such as Wikimedia Commons, Wikipedia, FIFA/UEFA websites, official club websites, or sports news portals.\n"
                    "Return ONLY a raw JSON object matching this schema. Do not include any commentary, intro, or markdown formatting outside the JSON code block.\n\n"
                    "JSON Schema:\n"
                    "{{\n"
                    "  \"urls\": [\n"
                    "    \"https://example.com/image1.jpg\",\n"
                    "    \"https://example.com/image2.png\"\n"
                    "  ]\n"
                    "}}\n",
                    player_name, team_name
                );

                std::string search_mode_str = "on"; // Enable web search/grounding

                auto response = llm_instance->sendMessage(
                    prompt,
                    [fetcher](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher->post(url, data, header_client);
                    },
                    "user",
                    settings.model_name,
                    search_mode_str,
                    0.2f
                );

                std::string result_text;
                if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                    result_text = response.choices[0].message.content;
                } else {
                    throw std::runtime_error("AI returned an empty response");
                }

                std::string clean_json = strip_json_markdown(result_text);
                
                glz::json_t doc;
                auto ec = glz::read_json(doc, clean_json);
                std::vector<std::string> candidate_urls;
                if (!ec && doc.contains("urls") && doc["urls"].is_array()) {
                    for (const auto& item : doc["urls"].get<std::vector<glz::json_t>>()) {
                        if (item.is_string()) {
                            candidate_urls.push_back(item.get<std::string>());
                        }
                    }
                }

                bool found_working = false;
                std::string working_url = "";

                if (image_cache) {
                    for (const auto& url : candidate_urls) {
                        if (url.empty() || !url.starts_with("http")) {
                            continue;
                        }
                        if (image_cache->downloadAndCache(url)) {
                            working_url = url;
                            found_working = true;
                            break;
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    if (team_players_cache_.contains(team_code)) {
                        auto& cache = team_players_cache_[team_code];
                        if (player_idx < cache.players.size() && cache.players[player_idx].name == player_name) {
                            if (found_working) {
                                cache.players[player_idx].photo_url = working_url;
                            } else {
                                cache.players[player_idx].photo_url = "failed";
                            }
                        }
                    }
                    fetching_player_photos_.erase(search_key);
                }

                if (found_working) {
                    save_team_players_cache_to_disk();
                }

            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error fetching photo for {}: {}", player_name, e.what());
                std::lock_guard<std::mutex> lock(data_mutex);
                fetching_player_photos_.erase(search_key);
            }
        }).detach();
    }

    void render_expanded_commentary_box(const std::string& match_key, const Match& m) {
        bool is_fetching_comm = false;
        bool has_cache = false;
        CommentaryCache cache;

        {
            std::lock_guard<std::mutex> lock(data_mutex);
            is_fetching_comm = fetching_matches_.contains(match_key);
            if (commentary_cache_.contains(match_key)) {
                has_cache = true;
                cache = commentary_cache_[match_key];
            }
        }

        // Draw dynamically sized card box with ChannelsSplit to prevent scroll hijacking
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->ChannelsSplit(2);
        draw_list->ChannelsSetCurrent(1); // Set current channel to 1 (foreground / text)

        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 4.0f)); // Inner vertical padding
        ImGui::Indent(8.0f); // Inner horizontal padding

        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::TextColored(colors[2], "%s Passionate AI Commentary", ICON_MD_AUTO_AWESOME);
        ImGui::PopFont();

        std::string speak_label = (speaking_match_key_ == match_key) ? ICON_MD_VOLUME_OFF " Stop" : (has_cache && !is_fetching_comm ? ICON_MD_VOLUME_UP " Listen" : "");
        std::string ref_label = is_fetching_comm ? ICON_MD_AUTO_AWESOME " Generating..." : ICON_MD_REFRESH " Refresh";
        
        float speak_w = speak_label.empty() ? 0.0f : (ImGui::CalcTextSize(speak_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f);
        float ref_w = is_fetching_comm ? ImGui::CalcTextSize(ref_label.c_str()).x : (ImGui::CalcTextSize(ref_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f);
        float total_buttons_w = speak_w + (speak_w > 0.0f ? ImGui::GetStyle().ItemSpacing.x : 0.0f) + ref_w;
        
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > total_buttons_w) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - total_buttons_w - ImGui::GetStyle().WindowPadding.x);
        } else {
            ImGui::SameLine();
        }
        
        if (!speak_label.empty()) {
            if (speaking_match_key_ == match_key) {
                if (ImGui::SmallButton(std::format("{}##stop_{}", speak_label, match_key).c_str())) {
                    stop_speaking();
                }
            } else {
                if (ImGui::SmallButton(std::format("{}##listen_{}", speak_label, match_key).c_str())) {
                    say_commentary_async(match_key, cache.text);
                }
            }
            ImGui::SameLine();
        }
        
        if (is_fetching_comm) {
            ImGui::TextColored(colors[3], "%s", ref_label.c_str());
            requested_fps = 60;
        } else {
            if (ImGui::SmallButton(std::format("{}##ref_{}", ref_label, match_key).c_str())) {
                fetch_commentary_async(match_key, m);
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (is_fetching_comm && !has_cache) {
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionMax().x - 160.0f * dpi_scale) / 2.0f);
            
            const float RADIUS = 12.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 center = ImVec2(pos.x + RADIUS, pos.y + RADIUS);
            auto time = ImGui::GetTime() * 5.0;
            const int NUM_SEGMENTS = 8;
            for (int i = 0; i < NUM_SEGMENTS; i++) {
                auto t = time + i * 0.25;
                auto a = (t * M_PI * 2.0) / NUM_SEGMENTS;
                auto b = ((t + 0.25) * M_PI * 2.0) / NUM_SEGMENTS;
                float alpha = 0.1f + 0.9f * (1.0f - static_cast<float>(i) / static_cast<float>(NUM_SEGMENTS));
                ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors[1].x, colors[1].y, colors[1].z, alpha));
                draw_list->PathArcTo(center, RADIUS, static_cast<float>(a), static_cast<float>(b), 12);
                draw_list->PathStroke(color, false, 2.0f);
            }
            ImGui::Dummy(ImVec2(RADIUS * 2 + 10, RADIUS * 2 + 10));
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "Connecting to stadium booth...");
        } else {
            if (has_cache) {
                if (is_fetching_comm) {
                    ImGui::TextColored(colors[3], "(Updating commentary in background...)");
                    ImGui::Spacing();
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
                ImGui::TextWrapped("%s", cache.text.c_str());
                ImGui::PopStyleColor();

                // Parse and render link buttons
                auto urls = extract_urls(cache.text);
                if (!urls.empty()) {
                    ImGui::Spacing();
                    ImGui::TextColored(colors[2], "%s Quick Links found in commentary:", ICON_MD_LINK);
                    ImGui::Indent(10.0f);
                    for (size_t idx = 0; idx < urls.size(); ++idx) {
                        const auto& url = urls[idx];
                        std::string label = url;
                        if (url.find("youtube.com") != std::string::npos || url.find("youtu.be") != std::string::npos) {
                            label = ICON_MD_PLAY_CIRCLE_FILLED " Watch YouTube Highlights";
                        } else {
                            label = ICON_MD_OPEN_IN_NEW " Open Link";
                        }
                        
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.35f, 0.22f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.45f, 0.28f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.09f, 0.27f, 0.17f, 1.0f));
                        
                        if (ImGui::Button(std::format("{}##btn_lnk_{}_{}", label, match_key, idx).c_str())) {
                            rouen::platform::open_url(url);
                        }
                        ImGui::PopStyleColor(3);
                        
                        if (idx + 1 < urls.size()) {
                            ImGui::SameLine();
                        }
                    }
                    ImGui::Unindent(10.0f);
                }

                ImGui::Spacing();
                ImGui::Separator();
                
                auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                auto elapsed_seconds = now_epoch - cache.last_updated_epoch;
                
                std::string age_str;
                if (elapsed_seconds < 60) {
                    age_str = "just now";
                } else {
                    age_str = std::format("{}m ago", elapsed_seconds / 60);
                }

                if (m.status == "LIVE") {
                    static float dot_alpha = 0.0f;
                    static bool dot_fade_up = true;
                    if (dot_fade_up) {
                        dot_alpha += ImGui::GetIO().DeltaTime * 2.0f;
                        if (dot_alpha >= 1.0f) {
                            dot_alpha = 1.0f;
                            dot_fade_up = false;
                        }
                    } else {
                        dot_alpha -= ImGui::GetIO().DeltaTime * 2.0f;
                        if (dot_alpha <= 0.2f) {
                            dot_alpha = 0.2f;
                            dot_fade_up = true;
                        }
                    }
                    requested_fps = 60;

                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, dot_alpha), "%s", ICON_MD_FIBER_MANUAL_RECORD);
                    ImGui::SameLine();
                    int next_refresh = 300 - static_cast<int>(elapsed_seconds);
                    if (next_refresh < 0) next_refresh = 0;
                    ImGui::TextColored(colors[5], "Live Commentary (Last 5 mins focus) • Auto-refresh in %dm %ds", next_refresh / 60, next_refresh % 60);
                } else {
                    ImGui::TextColored(colors[5], "Match status: %s • Cached %s", cache.status.c_str(), age_str.c_str());
                }
            } else {
                ImGui::TextColored(colors[4], "No commentary available yet.");
            }
        }

        ImGui::Unindent(8.0f);
        ImGui::Dummy(ImVec2(0, 4.0f)); // Bottom vertical padding
        ImGui::EndGroup();

        // Get actual size of the commentary group
        ImVec2 min_pos = ImGui::GetItemRectMin();
        ImVec2 max_pos = ImGui::GetItemRectMax();
        
        // Add padding around the group for visual framing
        min_pos.x -= 4.0f;
        min_pos.y -= 4.0f;
        max_pos.x += 4.0f;
        max_pos.y += 4.0f;

        draw_list->ChannelsSetCurrent(0); // Set current channel to 0 (background)
        draw_list->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(colors[7]), 6.0f);
        draw_list->AddRect(min_pos, max_pos, ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f);

        draw_list->ChannelsMerge();
    }

    void fetch_real_data() {
        if (is_fetching.exchange(true)) {
            return;
        }
        std::jthread([this]() {
            try {
                auto get_safe_string = [](const glz::json_t& g, const std::string& key, const std::string& default_val = "") -> std::string {
                    if (g.contains(key)) {
                        const auto& val = g[key];
                        if (val.is_string()) {
                            return val.get<std::string>();
                        }
                    }
                    return default_val;
                };

                auto clean_scorers = [](const std::string& raw) -> std::string {
                    if (raw == "null" || raw.empty()) return "";
                    std::string result;
                    for (char c : raw) {
                        if (c == '{' || c == '}' || c == '"' || c == '\\') continue;
                        result.push_back(c);
                    }
                    std::string clean;
                    size_t i = 0;
                    while (i < result.size()) {
                        if (i + 2 < result.size() && 
                            static_cast<unsigned char>(result[i]) == 0xE2 && 
                            static_cast<unsigned char>(result[i+1]) == 0x80 && 
                            (static_cast<unsigned char>(result[i+2]) == 0x9C || static_cast<unsigned char>(result[i+2]) == 0x9D)) {
                            i += 3;
                        } else {
                            clean.push_back(result[i]);
                            i++;
                        }
                    }
                    std::string final_str;
                    for (size_t k = 0; k < clean.size(); ++k) {
                        if (clean[k] == ',') {
                            final_str += ", ";
                        } else {
                            final_str += clean[k];
                        }
                    }
                    return final_str;
                };

                DB_INFO("World Cup Card: Fetching teams from live API...");
                http::fetch fetcher{15};
                
                // 1. Fetch teams
                std::string teams_url = "https://worldcup26.ir/get/teams";
                std::string teams_response = fetcher(teams_url, [](auto h) {
                    h("User-Agent: Rouen/1.0");
                });
                
                glz::json_t teams_json;
                auto ec_teams = glz::read_json(teams_json, teams_response);
                if (ec_teams) {
                    DB_ERROR_FMT("Failed to parse teams JSON: {}", glz::format_error(ec_teams, teams_response));
                    return;
                }
                
                std::unordered_map<std::string, TeamInfo> temp_teams;
                for (const auto& t : teams_json["teams"].get<std::vector<glz::json_t>>()) {
                    std::string tid = get_safe_string(t, "id");
                    TeamInfo info;
                    info.name = get_safe_string(t, "name_en");
                    info.code = get_safe_string(t, "fifa_code");
                    info.group = get_safe_string(t, "groups");
                    temp_teams[tid] = info;
                }
                
                // Fetch stadiums
                DB_INFO("World Cup Card: Fetching stadiums from live API...");
                std::string stadiums_url = "https://worldcup26.ir/get/stadiums";
                std::string stadiums_response = fetcher(stadiums_url, [](auto h) {
                    h("User-Agent: Rouen/1.0");
                });
                
                glz::json_t stadiums_json;
                auto ec_stadiums = glz::read_json(stadiums_json, stadiums_response);
                std::unordered_map<std::string, std::pair<std::string, std::string>> temp_stadiums;
                if (!ec_stadiums) {
                    for (const auto& s : stadiums_json["stadiums"].get<std::vector<glz::json_t>>()) {
                        std::string sid = get_safe_string(s, "id");
                        std::string sname = get_safe_string(s, "name_en");
                        std::string scity = get_safe_string(s, "city_en");
                        temp_stadiums[sid] = {sname, scity};
                    }
                }
                
                DB_INFO("World Cup Card: Fetching games from live API...");
                // 2. Fetch games
                std::string games_url = "https://worldcup26.ir/get/games";
                std::string games_response = fetcher(games_url, [](auto h) {
                    h("User-Agent: Rouen/1.0");
                });
                
                glz::json_t games_json;
                auto ec_games = glz::read_json(games_json, games_response);
                if (ec_games) {
                    DB_ERROR_FMT("Failed to parse games JSON: {}", glz::format_error(ec_games, games_response));
                    return;
                }
                
                std::vector<Match> temp_matches;
                for (const auto& g : games_json["games"].get<std::vector<glz::json_t>>()) {
                    Match m;
                    m.group = get_safe_string(g, "group");
                    std::string finished_str = get_safe_string(g, "finished", "FALSE");
                    std::string time_el = get_safe_string(g, "time_elapsed");
                    
                    std::string hid = get_safe_string(g, "home_team_id");
                    std::string aid = get_safe_string(g, "away_team_id");
                    
                    if (temp_teams.contains(hid)) {
                        m.home_team = temp_teams[hid].name;
                        m.home_code = temp_teams[hid].code;
                    } else {
                        std::string label = get_safe_string(g, "home_team_label");
                        if (!label.empty()) {
                            m.home_team = label;
                        } else {
                            m.home_team = get_safe_string(g, "home_team_name_en", "TBD");
                        }
                        m.home_code = "";
                    }
                    
                    if (temp_teams.contains(aid)) {
                        m.away_team = temp_teams[aid].name;
                        m.away_code = temp_teams[aid].code;
                    } else {
                        std::string label = get_safe_string(g, "away_team_label");
                        if (!label.empty()) {
                            m.away_team = label;
                        } else {
                            m.away_team = get_safe_string(g, "away_team_name_en", "TBD");
                        }
                        m.away_code = "";
                    }
                    
                    std::string hscore_str = get_safe_string(g, "home_score", "0");
                    std::string ascore_str = get_safe_string(g, "away_score", "0");
                    m.home_score = hscore_str.empty() ? 0 : std::stoi(hscore_str);
                    m.away_score = ascore_str.empty() ? 0 : std::stoi(ascore_str);
                    
                    m.date_str = get_safe_string(g, "local_date");
                    std::string sid = get_safe_string(g, "stadium_id");
                    m.stadium_id = sid;
                    
                    time_t utc_kickoff = get_match_utc_time(m.date_str, m.stadium_id);
                    auto now = std::chrono::system_clock::now();
                    time_t now_time = std::chrono::system_clock::to_time_t(now);
                    
                    if (finished_str == "TRUE" || time_el == "finished") {
                        m.status = "COMPLETED";
                    } else if (utc_kickoff != 0 && now_time >= utc_kickoff) {
                        m.status = "LIVE";
                    } else if (time_el == "notstarted" || time_el.empty()) {
                        m.status = "UPCOMING";
                    } else {
                        m.status = "LIVE";
                    }
                    
                    if (m.status == "COMPLETED") {
                        m.time_str = "FT";
                    } else {
                        m.time_str = time_el;
                        if (m.time_str == "notstarted" && m.date_str.size() >= 16) {
                            m.time_str = m.date_str.substr(11);
                        }
                    }
                    
                    if (temp_stadiums.contains(sid)) {
                        m.venue = temp_stadiums[sid].first + ", " + temp_stadiums[sid].second;
                    } else {
                        m.venue = "World Cup Stadium";
                    }
                    
                    m.home_scorers = clean_scorers(get_safe_string(g, "home_scorers"));
                    m.away_scorers = clean_scorers(get_safe_string(g, "away_scorers"));
                    
                    temp_matches.push_back(m);
                }

                // Fetch and parse ESPN live scoreboard to override stats/scores for real-time accuracy
                glz::json_t espn_json;
                bool espn_loaded = false;
                try {
                    DB_INFO("World Cup Card: Fetching live scores from ESPN...");
                    std::string espn_url = "https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/scoreboard";
                    std::string espn_response = fetcher(espn_url, [](auto h) {
                        h("User-Agent: Rouen/1.0");
                    });
                    auto ec_espn = glz::read_json(espn_json, espn_response);
                    if (!ec_espn) {
                        espn_loaded = true;
                        DB_INFO("World Cup Card: ESPN fetch completed successfully!");
                    } else {
                        DB_WARN_FMT("Failed to parse ESPN JSON: {}", glz::format_error(ec_espn, espn_response));
                    }
                } catch (const std::exception& e) {
                    DB_WARN_FMT("Exception in ESPN fetcher: {}", e.what());
                }

                struct EspnMatch {
                    std::string home_team;
                    std::string away_team;
                    int home_score = 0;
                    int away_score = 0;
                    std::string status;
                    std::string time_str;
                    std::string home_scorers;
                    std::string away_scorers;
                };

                std::vector<EspnMatch> espn_matches;
                if (espn_loaded && espn_json.contains("events")) {
                    for (const auto& ev : espn_json["events"].get<std::vector<glz::json_t>>()) {
                        if (!ev.contains("competitions")) continue;
                        const auto& comps = ev["competitions"].get<std::vector<glz::json_t>>();
                        if (comps.empty()) continue;
                        const auto& comp = comps[0];
                        if (!comp.contains("competitors")) continue;

                        EspnMatch em;

                        std::string state = "pre";
                        if (ev.contains("status") && ev["status"].contains("type")) {
                            state = get_safe_string(ev["status"]["type"], "state", "pre");
                            em.time_str = get_safe_string(ev["status"]["type"], "detail", "");
                        }

                        if (state == "in") {
                            em.status = "LIVE";
                        } else if (state == "post") {
                            em.status = "COMPLETED";
                            em.time_str = "FT";
                        } else {
                            em.status = "UPCOMING";
                        }

                        for (const auto& team : comp["competitors"].get<std::vector<glz::json_t>>()) {
                            std::string ha = get_safe_string(team, "homeAway");
                            std::string tname = "";
                            if (team.contains("team")) {
                                tname = get_safe_string(team["team"], "displayName");
                            }
                            std::string score_str = get_safe_string(team, "score", "0");
                            int score = score_str.empty() ? 0 : std::stoi(score_str);

                            if (ha == "home") {
                                em.home_team = tname;
                                em.home_score = score;
                            } else {
                                em.away_team = tname;
                                em.away_score = score;
                            }
                        }

                        if (comp.contains("details")) {
                            std::vector<std::string> home_goals;
                            std::vector<std::string> away_goals;
                            for (const auto& det : comp["details"].get<std::vector<glz::json_t>>()) {
                                std::string type_text = "";
                                if (det.contains("type")) {
                                    type_text = get_safe_string(det["type"], "text");
                                }
                                bool is_goal = (type_text.find("Goal") != std::string::npos || type_text == "Penalty");
                                if (!is_goal) continue;

                                std::string clock_val = "";
                                if (det.contains("clock")) {
                                    clock_val = get_safe_string(det["clock"], "displayValue");
                                }

                                std::string scorer_name = "";
                                if (det.contains("athletesInvolved")) {
                                    const auto& aths = det["athletesInvolved"].get<std::vector<glz::json_t>>();
                                    if (!aths.empty()) {
                                        scorer_name = get_safe_string(aths[0], "displayName");
                                    }
                                }

                                if (scorer_name.empty()) continue;

                                std::string team_id_str = "";
                                if (det.contains("team")) {
                                    team_id_str = get_safe_string(det["team"], "id");
                                }

                                bool is_own_goal = (type_text.find("Own Goal") != std::string::npos);

                                std::string goal_entry = scorer_name;
                                if (!clock_val.empty()) {
                                    goal_entry += " " + clock_val;
                                }
                                if (is_own_goal) {
                                    goal_entry += " (OG)";
                                }

                                std::string home_team_id = "";
                                std::string away_team_id = "";
                                for (const auto& competitor : comp["competitors"].get<std::vector<glz::json_t>>()) {
                                    std::string ha = get_safe_string(competitor, "homeAway");
                                    std::string tid = get_safe_string(competitor, "id");
                                    if (ha == "home") home_team_id = tid;
                                    else away_team_id = tid;
                                }

                                if (team_id_str == home_team_id) {
                                    home_goals.push_back(goal_entry);
                                } else {
                                    away_goals.push_back(goal_entry);
                                }
                            }

                            for (size_t i = 0; i < home_goals.size(); ++i) {
                                if (i > 0) em.home_scorers += ", ";
                                em.home_scorers += home_goals[i];
                            }
                            for (size_t i = 0; i < away_goals.size(); ++i) {
                                if (i > 0) em.away_scorers += ", ";
                                em.away_scorers += away_goals[i];
                            }
                        }

                        espn_matches.push_back(em);
                    }
                }

                if (!espn_matches.empty()) {
                    for (auto& m : temp_matches) {
                        std::string norm_m_home = normalize_team_name(m.home_team);
                        std::string norm_m_away = normalize_team_name(m.away_team);

                        for (const auto& em : espn_matches) {
                            std::string norm_em_home = normalize_team_name(em.home_team);
                            std::string norm_em_away = normalize_team_name(em.away_team);

                            if ((norm_m_home == norm_em_home && norm_m_away == norm_em_away) ||
                                (norm_m_home == norm_em_away && norm_m_away == norm_em_home)) {

                                m.home_score = (norm_m_home == norm_em_home) ? em.home_score : em.away_score;
                                m.away_score = (norm_m_home == norm_em_home) ? em.away_score : em.home_score;
                                m.status = em.status;
                                m.time_str = em.time_str;

                                if (norm_m_home == norm_em_home) {
                                    m.home_scorers = em.home_scorers;
                                    m.away_scorers = em.away_scorers;
                                } else {
                                    m.home_scorers = em.away_scorers;
                                    m.away_scorers = em.home_scorers;
                                }
                                break;
                            }
                        }
                    }
                }

                // 3. Fetch groups and standings from live API /get/groups
                DB_INFO("World Cup Card: Fetching groups from live API...");
                std::string groups_url = "https://worldcup26.ir/get/groups";
                std::string groups_response = fetcher(groups_url, [](auto h) {
                    h("User-Agent: Rouen/1.0");
                });
                
                glz::json_t groups_json;
                auto ec_groups = glz::read_json(groups_json, groups_response);
                if (ec_groups) {
                    DB_ERROR_FMT("Failed to parse groups JSON: {}", glz::format_error(ec_groups, groups_response));
                    return;
                }

                std::unordered_map<std::string, std::vector<GroupTeam>> temp_groups;
                for (const auto& g : groups_json["groups"].get<std::vector<glz::json_t>>()) {
                    std::string gname = get_safe_string(g, "name");
                    if (gname.empty()) continue;
                    
                    std::vector<GroupTeam> gteams;
                    if (g.contains("teams")) {
                        for (const auto& gt : g["teams"].get<std::vector<glz::json_t>>()) {
                            std::string tid = get_safe_string(gt, "team_id");
                            if (temp_teams.contains(tid)) {
                                GroupTeam team;
                                team.name = temp_teams[tid].name;
                                team.code = temp_teams[tid].code;
                                
                                std::string mp_str = get_safe_string(gt, "mp", "0");
                                std::string w_str = get_safe_string(gt, "w", "0");
                                std::string d_str = get_safe_string(gt, "d", "0");
                                std::string l_str = get_safe_string(gt, "l", "0");
                                std::string gd_str = get_safe_string(gt, "gd", "0");
                                std::string pts_str = get_safe_string(gt, "pts", "0");
                                
                                team.played = mp_str.empty() ? 0 : std::stoi(mp_str);
                                team.won = w_str.empty() ? 0 : std::stoi(w_str);
                                team.drawn = d_str.empty() ? 0 : std::stoi(d_str);
                                team.lost = l_str.empty() ? 0 : std::stoi(l_str);
                                team.gd = gd_str.empty() ? 0 : std::stoi(gd_str);
                                team.points = pts_str.empty() ? 0 : std::stoi(pts_str);
                                
                                gteams.push_back(team);
                            }
                        }
                    }
                    
                    std::sort(gteams.begin(), gteams.end(), [](const auto& a, const auto& b) {
                        if (a.points != b.points) return a.points > b.points;
                        return a.gd > b.gd;
                    });
                    
                    temp_groups[gname] = gteams;
                }
                
                // Save to card state atomically
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    api_matches = std::move(temp_matches);
                    api_groups = std::move(temp_groups);
                    api_teams = std::move(temp_teams);
                    data_loaded = true;
                }
                
                DB_INFO("World Cup Card: Fetch completed successfully!");
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Exception in World Cup background fetcher: {}", e.what());
            }
            is_fetching = false;
        }).detach();
    }


    void render_match_center() {
        // Grab current matches list thread-safely
        std::vector<Match> display_matches;
        bool loaded = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            display_matches = api_matches;
            loaded = data_loaded;
        }

        if (!loaded) {
            float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionMax().x - 240.0f * dpi_scale) / 2.0f);
            ImGui::TextColored(colors[6], "Loading live match schedule...");
            return;
        }

        // 1. Featured Match Box at the top
        const Match* featured = nullptr;
        for (const auto& m : display_matches) {
            if (m.status == "LIVE") {
                featured = &m;
                break;
            }
        }
        if (!featured) {
            for (const auto& m : display_matches) {
                if (m.status == "UPCOMING") {
                    featured = &m;
                    break;
                }
            }
        }
        if (!featured && !display_matches.empty()) {
            featured = &display_matches.front();
        }

        if (featured) {
            bool is_live = featured->status == "LIVE";
            bool has_scorers = (is_live || featured->status == "COMPLETED") && (!featured->home_scorers.empty() || !featured->away_scorers.empty());
            float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;

            ImGui::TextColored(colors[2], "Featured Match");
            ImGui::Separator();
            
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            float card_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

            draw_list->ChannelsSplit(2);
            draw_list->ChannelsSetCurrent(1); // Foreground/text

            // Render contents in a group
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * dpi_scale);
            ImGui::Indent(8.0f * dpi_scale);
            ImGui::BeginGroup();
            
            ImGui::Columns(3, "featMatchCols", false);
            ImGui::SetColumnWidth(0, card_width * 0.38f);
            ImGui::SetColumnWidth(1, card_width * 0.24f);
            ImGui::SetColumnWidth(2, card_width * 0.38f);

            std::string feat_key = featured->home_code + "_" + featured->away_code + "_" + featured->date_str;
            float home_factor = 0.0f;
            float away_factor = 0.0f;
            if (match_flashes.contains(feat_key)) {
                home_factor = match_flashes[feat_key].home_flash_timer / 5.0f;
                away_factor = match_flashes[feat_key].away_flash_timer / 5.0f;
            }
            float max_factor = std::max(home_factor, away_factor);

            // Home Team
            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Indent(15 * dpi_scale);
            if (is_live) {
                if (home_factor > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, home_factor), "%s GOAL!", ICON_MD_SPORTS_SOCCER);
                } else {
                    ImGui::TextUnformatted(""); // Fixed space to prevent layout shifts
                }
            }
            ImVec2 flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36 * dpi_scale, 24 * dpi_scale));
            flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36 * dpi_scale, 24 * dpi_scale), featured->home_code);
            ImGui::Spacing();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            if (ImGui::Selectable(std::format("{}##feat_home", featured->home_team).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                tracker_selected_team_code = featured->home_code;
                set_team_tracker_selected = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Go to Team Tracker for %s", featured->home_team.c_str());
            }
            ImGui::PopFont();
            ImGui::TextColored(colors[5], "%s", featured->home_code.c_str());
            ImGui::Unindent(15 * dpi_scale);

            // Score/Status
            ImGui::NextColumn();
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            float col_width = ImGui::GetColumnWidth(1);
            if (featured->status == "LIVE") {
                std::string live_label = ICON_MD_FIBER_MANUAL_RECORD " LIVE";
                float text_w = ImGui::CalcTextSize(live_label.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::TextColored(colors[4], "%s", live_label.c_str());
                
                ImGui::Spacing();
                std::string score_str = std::format("{} - {}", featured->home_score, featured->away_score);
                ImVec2 text_size = ImGui::CalcTextSize(score_str.c_str());
                ImVec2 cur_pos = ImGui::GetCursorScreenPos();
                float offset_x = (col_width - text_size.x) / 2.0f;
                ImVec2 score_min = ImVec2(cur_pos.x + offset_x - 12.0f * dpi_scale, cur_pos.y - 4.0f * dpi_scale);
                ImVec2 score_max = ImVec2(cur_pos.x + offset_x + text_size.x + 12.0f * dpi_scale, cur_pos.y + text_size.y + 4.0f * dpi_scale);
                
                if (max_factor > 0.0f) {
                    ImVec4 glow_color = ImVec4(0.2f, 0.8f, 0.4f, 0.35f * max_factor);
                    ImGui::GetWindowDrawList()->AddRectFilled(score_min, score_max, ImGui::GetColorU32(glow_color), 8.0f * dpi_scale);
                }
                
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                if (max_factor > 0.0f) {
                    ImVec4 text_col = ImVec4(1.0f, 0.8f + 0.2f * max_factor, 0.0f, 1.0f);
                    ImGui::TextColored(text_col, "%s", score_str.c_str());
                } else {
                    ImGui::Text("%s", score_str.c_str());
                }
                ImGui::PopFont();
                
                ImGui::Spacing();
                std::string live_time = get_match_time_display(*featured);
                float text_width = ImGui::CalcTextSize(live_time.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_width) / 2.0f);
                ImGui::TextColored(colors[3], "%s", live_time.c_str());
            } else if (featured->status == "COMPLETED") {
                std::string status_str = "COMPLETED";
                float text_w = ImGui::CalcTextSize(status_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::TextColored(colors[5], "%s", status_str.c_str());
                ImGui::Spacing();
                
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                std::string score_str = std::format("{} - {}", featured->home_score, featured->away_score);
                text_w = ImGui::CalcTextSize(score_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::Text("%s", score_str.c_str());
                ImGui::PopFont();
                
                ImGui::Spacing();
                text_w = ImGui::CalcTextSize(featured->time_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::TextColored(colors[5], "%s", featured->time_str.c_str());
            } else {
                std::string status_str = "UPCOMING";
                float text_w = ImGui::CalcTextSize(status_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::TextColored(colors[2], "%s", status_str.c_str());
                ImGui::Spacing();
                
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                std::string vs_str = "VS";
                text_w = ImGui::CalcTextSize(vs_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::Text("%s", vs_str.c_str());
                ImGui::PopFont();
                
                ImGui::Spacing();
                std::string feat_time = featured->time_str;
                time_t utc = get_match_utc_time(featured->date_str, featured->stadium_id);
                if (utc != 0) {
                    feat_time = format_local_hour_minute(utc);
                }
                text_w = ImGui::CalcTextSize(feat_time.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_w) / 2.0f);
                ImGui::TextColored(colors[5], "%s", feat_time.c_str());
            }

            // Away Team
            ImGui::NextColumn();
            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Indent(15 * dpi_scale);
            if (is_live) {
                if (away_factor > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, away_factor), "%s GOAL!", ICON_MD_SPORTS_SOCCER);
                } else {
                    ImGui::TextUnformatted("");
                }
            }
            flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36 * dpi_scale, 24 * dpi_scale));
            flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36 * dpi_scale, 24 * dpi_scale), featured->away_code);
            ImGui::Spacing();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            if (ImGui::Selectable(std::format("{}##feat_away", featured->away_team).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                tracker_selected_team_code = featured->away_code;
                set_team_tracker_selected = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Go to Team Tracker for %s", featured->away_team.c_str());
            }
            ImGui::PopFont();
            ImGui::TextColored(colors[5], "%s", featured->away_code.c_str());
            ImGui::Unindent(15 * dpi_scale);

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Spacing();
            
            if (has_scorers) {
                ImGui::TextColored(colors[2], "%s Match Events / Scorers:", ICON_MD_SPORTS_SOCCER);
                ImGui::Indent(10.0f * dpi_scale);
                if (!featured->home_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s: %s", featured->home_team.c_str(), featured->home_scorers.c_str());
                }
                if (!featured->away_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s: %s", featured->away_team.c_str(), featured->away_scorers.c_str());
                }
                ImGui::Unindent(10.0f * dpi_scale);
                ImGui::Separator();
                ImGui::Spacing();
            }
            
            ImGui::TextColored(colors[6], "  %s  %s", ICON_MD_PLACE, featured->venue.c_str());
            std::string grp_str = featured->group;
            float txt_width = ImGui::CalcTextSize(grp_str.c_str()).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > txt_width + ImGui::GetStyle().ItemSpacing.x) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - txt_width - ImGui::GetStyle().WindowPadding.x);
            } else {
                ImGui::SameLine();
            }
            ImGui::TextColored(colors[5], "%s", grp_str.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            std::string feat_btn_label = (expanded_match_key_ == feat_key) ? ICON_MD_AUTO_AWESOME " Collapse Commentary" : ICON_MD_AUTO_AWESOME " Match Commentary";
            float button_width = ImGui::CalcTextSize(feat_btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float avail_w = ImGui::GetContentRegionAvail().x;
            if (avail_w > button_width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - button_width - ImGui::GetStyle().WindowPadding.x);
            }
            if (ImGui::Button(std::format("{}##btn_feat", feat_btn_label).c_str(), ImVec2(button_width, 0.0f))) {
                handle_commentary_click(feat_key, *featured);
            }

            ImGui::EndGroup();
            ImGui::Unindent(8.0f * dpi_scale);

            ImVec2 group_max = ImGui::GetItemRectMax();
            float card_bottom = group_max.y + 6.0f * dpi_scale;

            draw_list->ChannelsSetCurrent(0); // Background
            draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(colors[7]), 6.0f * dpi_scale);
            draw_list->AddRect(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

            draw_list->ChannelsMerge();

            // Advance cursor past the card box
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, card_bottom + 8.0f * dpi_scale));

            if (expanded_match_key_ == feat_key) {
                render_expanded_commentary_box(feat_key, *featured);
                ImGui::Spacing();
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(colors[3], "%s Connected to Live FIFA API", ICON_MD_SIGNAL_CELLULAR_4_BAR);

        ImGui::Spacing();
        ImGui::TextColored(colors[2], "Fixture List & Scores:");
        ImGui::Separator();

        // Render completed and other matches in a scrollable list
        ImGui::BeginChild("MatchScrollBox", ImVec2(0, 0), false);
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        for (const auto& m : display_matches) {
            // Skip the featured match since it's pinned to the top
            if (featured && m.home_code == featured->home_code && m.away_code == featured->away_code && m.date_str == featured->date_str) {
                continue;
            }
            
            bool has_scorers = (m.status == "COMPLETED" || m.status == "LIVE") && (!m.home_scorers.empty() || !m.away_scorers.empty());
            
            std::string m_flash_key = m.home_code + "_" + m.away_code + "_" + m.date_str;
            if (need_scroll_to_match && m_flash_key == expanded_match_key_) {
                ImGui::SetScrollHereY(0.0f);
                need_scroll_to_match = false; // Reset so user can scroll freely afterwards
            }
            float row_factor = 0.0f;
            if (match_flashes.contains(m_flash_key)) {
                row_factor = std::max(match_flashes[m_flash_key].home_flash_timer, match_flashes[m_flash_key].away_flash_timer) / 5.0f;
            }

            ImVec4 bg_color = ImVec4(0.15f, 0.15f, 0.17f, 0.5f);
            if (row_factor > 0.0f) {
                bg_color.x = bg_color.x * (1.0f - row_factor) + 0.2f * row_factor;
                bg_color.y = bg_color.y * (1.0f - row_factor) + 0.8f * row_factor;
                bg_color.z = bg_color.z * (1.0f - row_factor) + 0.4f * row_factor;
                bg_color.w = bg_color.w * (1.0f - row_factor) + 0.35f * row_factor;
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            float card_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

            draw_list->ChannelsSplit(2);
            draw_list->ChannelsSetCurrent(1); // Foreground/text

            // Render contents in a group
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * dpi_scale);
            ImGui::Indent(8.0f * dpi_scale);
            ImGui::BeginGroup();
            
            ImGui::Columns(3, nullptr, false);
            ImGui::SetColumnWidth(0, card_width * 0.38f);
            ImGui::SetColumnWidth(1, card_width * 0.24f);
            ImGui::SetColumnWidth(2, card_width * 0.38f);

            // Drawing flag and home team
            ImGui::AlignTextToFramePadding();
            ImVec2 fpos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24 * dpi_scale, 16 * dpi_scale));
            flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(24 * dpi_scale, 16 * dpi_scale), m.home_code);
            ImGui::SameLine();
            if (ImGui::Selectable(std::format("{}##home_{}", m.home_team, m.home_code).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                tracker_selected_team_code = m.home_code;
                set_team_tracker_selected = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Go to Team Tracker for %s", m.home_team.c_str());
            }
            
            // Score / Status
            ImGui::NextColumn();
            ImGui::AlignTextToFramePadding();
            float col_width = ImGui::GetColumnWidth(1);
            if (m.status == "COMPLETED") {
                std::string score_str = std::format("{} - {}", m.home_score, m.away_score);
                float score_w = ImGui::CalcTextSize(score_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - score_w) / 2.0f);
                ImGui::TextColored(colors[3], "%s", score_str.c_str());
                
                std::string time_str = m.time_str;
                float time_w = ImGui::CalcTextSize(time_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - time_w) / 2.0f);
                ImGui::TextColored(colors[5], "%s", time_str.c_str());
            } else if (m.status == "LIVE") {
                std::string score_str = std::format("{} - {}", m.home_score, m.away_score);
                float score_w = ImGui::CalcTextSize(score_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - score_w) / 2.0f);
                ImGui::TextColored(colors[4], "%s", score_str.c_str());
                
                std::string display_time = get_match_time_display(m) + " LIVE";
                float time_w = ImGui::CalcTextSize(display_time.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - time_w) / 2.0f);
                ImGui::TextColored(colors[4], "%s", display_time.c_str());
            } else {
                std::string vs_str = "VS";
                float vs_w = ImGui::CalcTextSize(vs_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - vs_w) / 2.0f);
                ImGui::TextColored(colors[6], "%s", vs_str.c_str());
                
                std::string display_time = m.time_str;
                time_t utc = get_match_utc_time(m.date_str, m.stadium_id);
                if (utc != 0) {
                    display_time = format_local_hour_minute(utc);
                }
                float time_w = ImGui::CalcTextSize(display_time.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - time_w) / 2.0f);
                ImGui::TextColored(colors[2], "%s", display_time.c_str());
            }
            
            // Away team and flag
            ImGui::NextColumn();
            ImGui::AlignTextToFramePadding();
            fpos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24 * dpi_scale, 16 * dpi_scale));
            flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(24 * dpi_scale, 16 * dpi_scale), m.away_code);
            ImGui::SameLine();
            if (ImGui::Selectable(std::format("{}##away_{}", m.away_team, m.away_code).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                tracker_selected_team_code = m.away_code;
                set_team_tracker_selected = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Go to Team Tracker for %s", m.away_team.c_str());
            }

            ImGui::Columns(1);

            // Add tooltip with stadium info and group details
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                std::string loc_str = m.date_str;
                time_t utc = get_match_utc_time(m.date_str, m.stadium_id);
                if (utc != 0) {
                    loc_str = format_local_time(utc);
                }
                ImGui::SetTooltip("Venue: %s\nGroup/Stage: %s\nStadium Local: %s\nYour Local: %s", 
                                  m.venue.c_str(), m.group.c_str(), m.date_str.c_str(), loc_str.c_str());
            }

            // Render scorers underneath if completed
            if (has_scorers) {
                ImGui::Spacing();
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, card_width * 0.5f);
                ImGui::SetColumnWidth(1, card_width * 0.5f);
                
                if (!m.home_scorers.empty()) {
                    ImGui::Indent(12.0f * dpi_scale);
                    ImGui::TextColored(colors[5], "%s %s", ICON_MD_SPORTS_SOCCER, m.home_scorers.c_str());
                    ImGui::Unindent(12.0f * dpi_scale);
                }
                ImGui::NextColumn();
                if (!m.away_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s %s", ICON_MD_SPORTS_SOCCER, m.away_scorers.c_str());
                }
                ImGui::Columns(1);
            }

            // Commentary Button inside the match card
            ImGui::Spacing();
            std::string btn_label = (expanded_match_key_ == m_flash_key) ? ICON_MD_AUTO_AWESOME " Collapse" : ICON_MD_AUTO_AWESOME " Commentary";
            float button_width = ImGui::CalcTextSize(btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float avail_w = ImGui::GetContentRegionAvail().x;
            if (avail_w > button_width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - button_width - ImGui::GetStyle().WindowPadding.x);
            }
            if (ImGui::Button(std::format("{}##btn_{}_{}", btn_label, m.home_code, m.away_code).c_str(), ImVec2(button_width, 0.0f))) {
                handle_commentary_click(m_flash_key, m);
            }

            ImGui::EndGroup();
            ImGui::Unindent(8.0f * dpi_scale);

            ImVec2 group_max = ImGui::GetItemRectMax();
            float card_bottom = group_max.y + 6.0f * dpi_scale;

            draw_list->ChannelsSetCurrent(0); // Background
            draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(bg_color), 6.0f * dpi_scale);
            draw_list->AddRect(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

            draw_list->ChannelsMerge();

            // Advance cursor past the card box
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, card_bottom + 8.0f * dpi_scale));

            if (expanded_match_key_ == m_flash_key) {
                render_expanded_commentary_box(m_flash_key, m);
                ImGui::Spacing();
            }
        }
        ImGui::EndChild();
    }

    void render_standings() {
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        bool loaded = false;
        std::unordered_map<std::string, std::vector<GroupTeam>> active_groups;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            active_groups = api_groups;
            loaded = data_loaded;
        }

        if (!loaded) {
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionMax().x - 240.0f * dpi_scale) / 2.0f);
            ImGui::TextColored(colors[6], "Loading group standings...");
            return;
        }

        const char* group_dropdown_names[] = {
            "Group A", "Group B", "Group C", "Group D", "Group E", "Group F",
            "Group G", "Group H", "Group I", "Group J", "Group K", "Group L"
        };
        
        ImGui::SetNextItemWidth(150.0f * dpi_scale);
        ImGui::Combo("Select Group", &selected_group_idx, group_dropdown_names, IM_ARRAYSIZE(group_dropdown_names));
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_REFRESH " Refresh")) {
            fetch_real_data();
        }
        
        std::string selected_group_letter = std::string(1, static_cast<char>('A' + selected_group_idx));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (active_groups.contains(selected_group_letter)) {
            const auto& teams = active_groups[selected_group_letter];
            
            if (ImGui::BeginTable("table_standings", 8, 
                                  ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                // Table Headers
                ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 30.0f * dpi_scale);
                ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed, 25.0f * dpi_scale);
                ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 25.0f * dpi_scale);
                ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 25.0f * dpi_scale);
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 25.0f * dpi_scale);
                ImGui::TableSetupColumn("GD", ImGuiTableColumnFlags_WidthFixed, 30.0f * dpi_scale);
                ImGui::TableSetupColumn("PTS", ImGuiTableColumnFlags_WidthFixed, 35.0f * dpi_scale);
                ImGui::TableHeadersRow();

                int pos = 1;
                for (const auto& t : teams) {
                    ImGui::TableNextRow();
                    
                    // Highlight top 2 positions (qualification)
                    ImVec4 text_color = (pos <= 2) ? colors[3] : ImVec4(1.0f,1.0f,1.0f,1.0f);
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", pos++);
                    
                    ImGui::TableSetColumnIndex(1);
                    ImVec2 fpos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(20 * dpi_scale, 13 * dpi_scale));
                    flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(20 * dpi_scale, 13 * dpi_scale), t.code);
                    ImGui::SameLine();
                    ImGui::TextColored(text_color, "%s", t.name.c_str());
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.3f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.5f));
                    if (ImGui::SmallButton(std::format(ICON_MD_LAUNCH "##track_{}", t.code).c_str())) {
                        tracker_selected_team_code = t.code;
                        set_team_tracker_selected = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Go to Team Tracker for %s", t.name.c_str());
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", t.played);
                    
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", t.won);
                    
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", t.drawn);
                    
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%d", t.lost);
                    
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%+d", t.gd);
                    
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextColored(colors[2], "%d", t.points);
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextColored(colors[4], "Group standings data currently unavailable.");
        }
        
        ImGui::Spacing();
        ImGui::TextColored(colors[5], "Note: Top 2 teams of each group, and the 8 best 3rd-place teams");
        ImGui::TextColored(colors[5], "advance to the Round of 32 knockout stage.");
    }

    void render_stadiums() {
        ImGui::TextColored(colors[2], "Official Host Stadiums Info");
        ImGui::Separator();
        
        struct StadiumInfo {
            std::string name;
            std::string city;
            std::string country;
            int capacity;
            std::string highlight;
        };

        static std::vector<StadiumInfo> stadiums = {
            {"SoFi Stadium", "Inglewood (LA)", "United States", 70240, "Hosting the USA Opening match & Knockout stages."},
            {"Estadio Azteca", "Mexico City", "Mexico", 87523, "Hosted the World Cup Opening match. Historic 3rd WC venue!"},
            {"BC Place", "Vancouver", "Canada", 54500, "Canada's primary Western host venue with retractable roof."},
            {"MetLife Stadium", "East Rutherford (NY)", "United States", 82500, "Selected to host the grand FIFA World Cup Final!"},
            {"BMO Field", "Toronto", "Canada", 45736, "Canada's eastern hub, expanded for World Cup matches."},
            {"Estadio Akron", "Guadalajara", "Mexico", 48071, "Hosted the Group A thriller South Korea vs Czechia."}
        };

        ImGui::BeginChild("StadiumsScroll", ImVec2(0, 0), false);
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        for (const auto& s : stadiums) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
            ImGui::BeginChild(s.name.c_str(), ImVec2(0, 100.0f * dpi_scale), true);
            
            ImGui::TextColored(colors[2], "%s", s.name.c_str());
            std::string cap_str = std::format("Capacity: {}", s.capacity);
            float text_width = ImGui::CalcTextSize(cap_str.c_str()).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > text_width + ImGui::GetStyle().ItemSpacing.x) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - text_width - ImGui::GetStyle().WindowPadding.x);
            } else {
                ImGui::SameLine();
            }
            ImGui::TextColored(colors[5], "%s", cap_str.c_str());
            
            ImGui::Text("%s, %s", s.city.c_str(), s.country.c_str());
            ImGui::TextColored(colors[6], "%s", s.highlight.c_str());
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        ImGui::EndChild();
    }

    void render_team_tracker() {
        bool loaded = false;
        std::vector<Match> display_matches;
        std::unordered_map<std::string, TeamInfo> display_teams;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            display_matches = api_matches;
            display_teams = api_teams;
            loaded = data_loaded;
        }

        if (!loaded) {
            float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionMax().x - 240.0f * dpi_scale) / 2.0f);
            ImGui::TextColored(colors[6], "Loading team tracker...");
            return;
        }

        // 1. Sort teams alphabetically for the dropdown selection list
        std::vector<std::pair<std::string, std::string>> sorted_teams; // code, name
        for (const auto& [tid, t] : display_teams) {
            sorted_teams.push_back({t.code, t.name});
        }
        std::sort(sorted_teams.begin(), sorted_teams.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        if (sorted_teams.empty()) {
            ImGui::TextColored(colors[4], "No teams data available.");
            return;
        }

        if (!tracker_selected_team_code.empty()) {
            for (size_t i = 0; i < sorted_teams.size(); ++i) {
                if (sorted_teams[i].first == tracker_selected_team_code) {
                    tracker_selected_idx = static_cast<int>(i);
                    break;
                }
            }
            tracker_selected_team_code = ""; // Reset
        }

        if (tracker_selected_idx >= static_cast<int>(sorted_teams.size())) {
            tracker_selected_idx = 0;
        }

        if (tracker_selected_idx != tracker_last_selected_idx) {
            tracker_last_selected_idx = tracker_selected_idx;
            clear_player_textures();
        }

        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        ImGui::TextColored(colors[2], "Select a Team to Track:");
        ImGui::SetNextItemWidth(250.0f * dpi_scale);
        
        // Build array of char* for Combo
        std::vector<std::string> combo_items;
        std::vector<const char*> combo_items_cstr;
        for (const auto& item : sorted_teams) {
            combo_items.push_back(std::format("{} ({})", item.second, item.first));
        }
        for (const auto& s : combo_items) {
            combo_items_cstr.push_back(s.c_str());
        }

        ImGui::Combo("##TeamCombo", &tracker_selected_idx, combo_items_cstr.data(), static_cast<int>(combo_items_cstr.size()));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        size_t sel_u_idx = static_cast<size_t>(tracker_selected_idx);
        std::string sel_code = sorted_teams[sel_u_idx].first;
        std::string sel_name = sorted_teams[sel_u_idx].second;

        // Draw selected team flag next to name
        ImVec2 flag_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(36 * dpi_scale, 24 * dpi_scale));
        flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36 * dpi_scale, 24 * dpi_scale), sel_code);
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::Text("%s", sel_name.c_str());
        ImGui::PopFont();
        
        // Find selected team's group
        std::string sel_group = "";
        for (const auto& [tid, t] : display_teams) {
            if (t.code == sel_code) {
                sel_group = t.group;
                break;
            }
        }
        if (!sel_group.empty()) {
            std::string btn_label = std::format(ICON_MD_LAUNCH " Group {} Standings", sel_group);
            float btn_width = ImGui::CalcTextSize(btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > btn_width + ImGui::GetStyle().ItemSpacing.x) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - btn_width - ImGui::GetStyle().WindowPadding.x);
            } else {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(btn_label.c_str())) {
                int idx = std::toupper(static_cast<unsigned char>(sel_group[0])) - 'A';
                if (idx >= 0 && idx < 12) {
                    selected_group_idx = idx;
                    set_standings_selected = true;
                }
            }
        }

        ImGui::Spacing();

        // 2. Find all upcoming matches for this team
        std::vector<const Match*> upcoming_matches;
        for (const auto& m : display_matches) {
            if (m.status == "UPCOMING" || m.status == "LIVE") {
                if (m.home_code == sel_code || m.away_code == sel_code) {
                    upcoming_matches.push_back(&m);
                }
            }
        }

        // Sort upcoming matches chronologically by kickoff time
        std::sort(upcoming_matches.begin(), upcoming_matches.end(), [this](const Match* a, const Match* b) {
            return get_match_utc_time(a->date_str, a->stadium_id) < get_match_utc_time(b->date_str, b->stadium_id);
        });

        // Sub-tabs for Schedule vs Key Players
        if (ImGui::BeginTabBar("TeamTrackerSubTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem(ICON_MD_EVENT " Matches & Schedule")) {
                render_team_matches_section(upcoming_matches, sel_code, sel_name);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_MD_PEOPLE " Key Squad Players")) {
                render_team_players_section(sel_code, sel_name);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void render_team_matches_section(const std::vector<const Match*>& upcoming_matches, const std::string& sel_code, const std::string& sel_name) {
        if (!upcoming_matches.empty()) {
            float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const Match* next_match = upcoming_matches[0];
            bool has_scorers = (next_match->status == "LIVE") && (!next_match->home_scorers.empty() || !next_match->away_scorers.empty());
            
            ImGui::Spacing();
            ImGui::TextColored(colors[2], "Next Scheduled Match:");
            
            std::string next_key = next_match->home_code + "_" + next_match->away_code + "_" + next_match->date_str;
            float next_row_factor = 0.0f;
            if (match_flashes.contains(next_key)) {
                next_row_factor = std::max(match_flashes[next_key].home_flash_timer, match_flashes[next_key].away_flash_timer) / 5.0f;
            }

            ImVec4 next_bg_color = colors[7];
            if (next_row_factor > 0.0f) {
                next_bg_color.x = next_bg_color.x * (1.0f - next_row_factor) + 0.2f * next_row_factor;
                next_bg_color.y = next_bg_color.y * (1.0f - next_row_factor) + 0.8f * next_row_factor;
                next_bg_color.z = next_bg_color.z * (1.0f - next_row_factor) + 0.4f * next_row_factor;
                next_bg_color.w = next_bg_color.w * (1.0f - next_row_factor) + 0.35f * next_row_factor;
            }

            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            float card_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

            draw_list->ChannelsSplit(2);
            draw_list->ChannelsSetCurrent(1); // Foreground/text

            // Render contents in a group with padding
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * dpi_scale);
            ImGui::Indent(8.0f * dpi_scale);
            ImGui::BeginGroup();
            
            // Match detail header
            ImGui::TextColored(colors[5], "%s - %s", next_match->group.c_str(), next_match->venue.c_str());
            std::string view_btn_label = std::format(ICON_MD_LAUNCH " View Match##btn_next_{}", next_key);
            float btn_width = ImGui::CalcTextSize(view_btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > btn_width + ImGui::GetStyle().ItemSpacing.x) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - btn_width - ImGui::GetStyle().WindowPadding.x);
            } else {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(view_btn_label.c_str())) {
                "create_card"_sfn(std::format("worldcup:{}", next_key));
            }
            ImGui::Separator();
            ImGui::Spacing();

            // Render vs. Opponent
            bool is_home = (next_match->home_code == sel_code);
            std::string opponent_name = is_home ? next_match->away_team : next_match->home_team;
            std::string opponent_code = is_home ? next_match->away_code : next_match->home_code;
            
            ImGui::AlignTextToFramePadding();
            ImVec2 op_flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24 * dpi_scale, 16 * dpi_scale));
            flags::draw_flag(draw_list, op_flag_pos, ImVec2(24 * dpi_scale, 16 * dpi_scale), opponent_code);
            ImGui::SameLine();
            if (ImGui::Selectable(std::format("Vs. {} ({})", opponent_name, opponent_code).c_str(), false, ImGuiSelectableFlags_None, ImVec2(250 * dpi_scale, 0))) {
                tracker_selected_team_code = opponent_code;
                set_team_tracker_selected = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Switch tracker to %s", opponent_name.c_str());
            }
            
            ImGui::Spacing();
            
            // Local date from stadium
            ImGui::TextColored(colors[5], "Stadium Local Time: %s", next_match->date_str.c_str());

            // Convert and localize
            time_t utc_time = get_match_utc_time(next_match->date_str, next_match->stadium_id);
            std::string localized_time = format_local_time(utc_time);
            
            ImGui::TextColored(colors[3], "Your Local Time:    %s", localized_time.c_str());
            
            ImGui::Spacing();
            
            // Live Countdown
            auto now = std::chrono::system_clock::now();
            auto diff = std::chrono::system_clock::to_time_t(now);
            double diff_sec = std::difftime(utc_time, diff);
            
            if (next_match->status == "LIVE") {
                ImGui::TextColored(colors[4], "%s MATCH IS LIVE NOW!", ICON_MD_FIBER_MANUAL_RECORD);
                ImGui::SameLine();
                std::string match_time = get_match_time_display(*next_match);
                ImGui::TextColored(colors[3], "(%s)", match_time.c_str());
                
                if (next_row_factor > 0.0f) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, next_row_factor), "%s GOAL!!!", ICON_MD_SPORTS_SOCCER);
                }
                
                ImGui::Spacing();
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::TextColored(colors[2], "Score: %s %d - %d %s", 
                    next_match->home_team.c_str(), next_match->home_score, 
                    next_match->away_score, next_match->away_team.c_str());
                ImGui::PopFont();
                
                if (has_scorers) {
                    ImGui::Spacing();
                    ImGui::TextColored(colors[2], "%s Scorers:", ICON_MD_SPORTS_SOCCER);
                    ImGui::Indent(10.0f * dpi_scale);
                    if (!next_match->home_scorers.empty()) {
                        ImGui::TextColored(colors[5], "%s: %s", next_match->home_team.c_str(), next_match->home_scorers.c_str());
                    }
                    if (!next_match->away_scorers.empty()) {
                        ImGui::TextColored(colors[5], "%s: %s", next_match->away_team.c_str(), next_match->away_scorers.c_str());
                    }
                    ImGui::Unindent(10.0f * dpi_scale);
                }
            } else if (diff_sec > 0) {
                int days = static_cast<int>(diff_sec) / 86400;
                int hours = (static_cast<int>(diff_sec) % 86400) / 3600;
                int minutes = (static_cast<int>(diff_sec) % 3600) / 60;
                
                if (days > 0) {
                    ImGui::TextColored(colors[2], "Kickoff in: %d days, %d hours, %d minutes", days, hours, minutes);
                } else if (hours > 0) {
                    ImGui::TextColored(colors[2], "Kickoff in: %d hours, %d minutes", hours, minutes);
                } else {
                    ImGui::TextColored(colors[2], "Kickoff in: %d minutes", minutes);
                }
            } else {
                ImGui::TextColored(colors[5], "Match has started!");
            }
            
            ImGui::EndGroup();
            ImGui::Unindent(8.0f * dpi_scale);

            ImVec2 next_group_max = ImGui::GetItemRectMax();
            float next_bottom = next_group_max.y + 6.0f * dpi_scale;

            draw_list->ChannelsSetCurrent(0); // Background
            draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + card_width, next_bottom), ImGui::GetColorU32(next_bg_color), 6.0f * dpi_scale);
            draw_list->AddRect(start_pos, ImVec2(start_pos.x + card_width, next_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

            draw_list->ChannelsMerge();
            
            // Advance cursor past the card box
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, next_bottom + 8.0f * dpi_scale));
            
            // Render subsequent matches
            if (upcoming_matches.size() > 1) {
                ImGui::Spacing();
                ImGui::TextColored(colors[2], "Other Upcoming Matches:");
                ImGui::Separator();
                
                ImGui::BeginChild("SubsequentMatchesScroll", ImVec2(0, 0), false);
                for (size_t i = 1; i < upcoming_matches.size(); ++i) {
                    const Match* m = upcoming_matches[i];
                    
                    ImVec2 item_start_pos = ImGui::GetCursorScreenPos();
                    float item_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

                    draw_list->ChannelsSplit(2);
                    draw_list->ChannelsSetCurrent(1); // Foreground

                    // Render contents in a group with padding
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * dpi_scale);
                    ImGui::Indent(8.0f * dpi_scale);
                    ImGui::BeginGroup();
                    
                    bool is_m_home = (m->home_code == sel_code);
                    std::string opp_name = is_m_home ? m->away_team : m->home_team;
                    std::string opp_code = is_m_home ? m->away_code : m->home_code;
                    
                    ImGui::Columns(3, nullptr, false);
                    ImGui::SetColumnWidth(0, item_width * 0.45f);
                    ImGui::SetColumnWidth(1, item_width * 0.40f);
                    ImGui::SetColumnWidth(2, item_width * 0.15f);
                    
                    // Column 0: Opponent
                    ImGui::AlignTextToFramePadding();
                    ImVec2 sub_flag_pos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(24 * dpi_scale, 16 * dpi_scale));
                    flags::draw_flag(draw_list, sub_flag_pos, ImVec2(24 * dpi_scale, 16 * dpi_scale), opp_code);
                    ImGui::SameLine();
                    if (ImGui::Selectable(std::format("Vs. {} ({})##sub_sel_{}", opp_name, opp_code, i).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                        tracker_selected_team_code = opp_code;
                        set_team_tracker_selected = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Switch tracker to %s", opp_name.c_str());
                    }
                    
                    ImGui::TextColored(colors[5], "Stage: %s", m->group.c_str());
                    
                    // Column 1: Time & Countdown
                    ImGui::NextColumn();
                    
                    time_t sub_utc_time = get_match_utc_time(m->date_str, m->stadium_id);
                    std::string sub_localized_time = format_local_time(sub_utc_time);
                    ImGui::TextColored(colors[3], "%s", sub_localized_time.c_str());
                    
                    auto sub_now = std::chrono::system_clock::now();
                    auto sub_diff = std::chrono::system_clock::to_time_t(sub_now);
                    double sub_diff_sec = std::difftime(sub_utc_time, sub_diff);
                    if (sub_diff_sec > 0) {
                        int days = static_cast<int>(sub_diff_sec) / 86400;
                        int hours = (static_cast<int>(sub_diff_sec) % 86400) / 3600;
                        int minutes = (static_cast<int>(sub_diff_sec) % 3600) / 60;
                        if (days > 0) {
                            ImGui::TextColored(colors[2], "Kickoff in %d days, %d hrs", days, hours);
                        } else if (hours > 0) {
                            ImGui::TextColored(colors[2], "Kickoff in %d hrs, %d mins", hours, minutes);
                        } else {
                            ImGui::TextColored(colors[2], "Kickoff in %d minutes", minutes);
                        }
                    } else {
                        ImGui::TextColored(colors[4], "Kickoff has started!");
                    }

                    // Column 2: Action Button
                    ImGui::NextColumn();
                    std::string sub_key = m->home_code + "_" + m->away_code + "_" + m->date_str;
                    if (ImGui::SmallButton(std::format(ICON_MD_LAUNCH " View##btn_sub_{}", i).c_str())) {
                        "create_card"_sfn(std::format("worldcup:{}", sub_key));
                    }
                    
                    ImGui::Columns(1);
                    
                    ImGui::EndGroup();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Venue: %s\nGroup/Stage: %s\nStadium Local: %s\nYour Local: %s", 
                                          m->venue.c_str(), m->group.c_str(), m->date_str.c_str(), sub_localized_time.c_str());
                    }
                    ImGui::Unindent(8.0f * dpi_scale);

                    ImVec2 item_group_max = ImGui::GetItemRectMax();
                    float item_bottom = item_group_max.y + 6.0f * dpi_scale;

                    draw_list->ChannelsSetCurrent(0); // Background
                    draw_list->AddRectFilled(item_start_pos, ImVec2(item_start_pos.x + item_width, item_bottom), ImGui::GetColorU32(colors[7]), 6.0f * dpi_scale);
                    draw_list->AddRect(item_start_pos, ImVec2(item_start_pos.x + item_width, item_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

                    draw_list->ChannelsMerge();
                    
                    // Advance cursor past the card box
                    ImGui::SetCursorScreenPos(ImVec2(item_start_pos.x, item_bottom + 8.0f * dpi_scale));
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
            ImGui::BeginChild("NoNextMatchBox", ImVec2(0, 0), true);
            ImGui::TextColored(colors[5], "No upcoming matches scheduled for %s.", sel_name.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    void render_team_players_section(const std::string& team_code, const std::string& team_name) {
        float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
        bool has_cache = false;
        bool is_fetching_players = false;
        TeamPlayersCache cache;

        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (team_players_cache_.contains(team_code)) {
                has_cache = true;
                cache = team_players_cache_[team_code];
            }
            is_fetching_players = fetching_players_.contains(team_code);
        }

        // Trigger fetch if not cached and not currently fetching
        if (!has_cache && !is_fetching_players) {
            fetch_team_players_async(team_code, team_name);
            is_fetching_players = true;
        }

        if (is_fetching_players && !has_cache) {
            ImGui::Spacing();
            ImGui::Indent(10.0f * dpi_scale);
            ImGui::TextColored(colors[6], "%s Querying AI for key squad players...", ICON_MD_HOURGLASS_EMPTY);
            ImGui::Unindent(10.0f * dpi_scale);
            return;
        }

        // Refresh/Reload button
        std::string btn_label = std::format("{} Refresh", ICON_MD_REFRESH);
        float btn_width = ImGui::CalcTextSize(btn_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float avail_w = ImGui::GetContentRegionAvail().x;
        if (avail_w > btn_width) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - btn_width - ImGui::GetStyle().WindowPadding.x);
        }
        if (ImGui::Button(btn_label.c_str())) {
            fetch_team_players_async(team_code, team_name);
        }
        ImGui::Spacing();

        if (cache.players.empty()) {
            ImGui::Text("No key player information available for this team.");
            return;
        }

        // Render player cards (no scrollable child window so it expands naturally)
        for (size_t i = 0; i < cache.players.size(); ++i) {
            const auto& player = cache.players[i];
            
            // Check if this is an error node
            if (player.position == "System Error") {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 start_pos = ImGui::GetCursorScreenPos();
                float card_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

                draw_list->ChannelsSplit(2);
                draw_list->ChannelsSetCurrent(1); // Foreground/text

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * dpi_scale);
                ImGui::Indent(8.0f * dpi_scale);
                ImGui::BeginGroup();
                
                ImGui::TextColored(colors[4], "%s %s", ICON_MD_ERROR, player.name.c_str());
                ImGui::TextWrapped("%s", player.comment.c_str());

                ImGui::EndGroup();
                ImGui::Unindent(8.0f * dpi_scale);

                ImVec2 group_max = ImGui::GetItemRectMax();
                float card_bottom = group_max.y + 6.0f * dpi_scale;

                draw_list->ChannelsSetCurrent(0); // Background
                draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(colors[7]), 6.0f * dpi_scale);
                draw_list->AddRect(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

                draw_list->ChannelsMerge();
                
                ImGui::SetCursorScreenPos(ImVec2(start_pos.x, card_bottom + 8.0f * dpi_scale));
                continue;
            }

            // Trigger async photo search if photo_url is empty
            if (player.photo_url.empty()) {
                fetch_player_photo_async(team_code, team_name, i, player.name);
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            float card_width = ImGui::GetContentRegionAvail().x - 6.0f * dpi_scale;

            draw_list->ChannelsSplit(2);
            draw_list->ChannelsSetCurrent(1); // Foreground/text

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * dpi_scale);
            ImGui::Indent(8.0f * dpi_scale);
            ImGui::BeginGroup();

            // Columns layout for player image, details and description
            ImGui::Columns(2, nullptr, false);
            // Column 0: Image (fixed width)
            ImGui::SetColumnWidth(0, 90.0f * dpi_scale);

            // Render photo placeholder or actual image if loaded
            ImVec2 img_size(70.0f * dpi_scale, 70.0f * dpi_scale);
            ImVec2 img_pos = ImGui::GetCursorScreenPos();
            
            SDL_Texture* player_tex = nullptr;
            int tex_w = 0, tex_h = 0;
            if (renderer && image_cache && !player.photo_url.empty() && player.photo_url.starts_with("http")) {
                if (player_textures_.contains(player.photo_url)) {
                    auto& lt = player_textures_[player.photo_url];
                    player_tex = lt.texture;
                    tex_w = lt.width;
                    tex_h = lt.height;
                } else {
                    int w = 0, h = 0;
                    player_tex = image_cache->getTexture(renderer, player.photo_url, w, h);
                    player_textures_[player.photo_url] = {player_tex, w, h};
                    tex_w = w;
                    tex_h = h;
                }
            }

            if (player_tex) {
                // Draw the actual photo centered and cropped to fill!
                ImGui::SetCursorScreenPos(img_pos);
                ImVec2 uv0(0.0f, 0.0f);
                ImVec2 uv1(1.0f, 1.0f);
                if (tex_w > 0 && tex_h > 0) {
                    float ar_tex = static_cast<float>(tex_w) / static_cast<float>(tex_h);
                    float ar_box = 1.0f; // img_size is 70x70, so aspect ratio is 1.0
                    if (ar_tex > ar_box) {
                        float pct = ar_box / ar_tex;
                        uv0.x = 0.5f - 0.5f * pct;
                        uv1.x = 0.5f + 0.5f * pct;
                    } else if (ar_tex < ar_box) {
                        float pct = ar_tex / ar_box;
                        uv0.y = 0.5f - 0.5f * pct;
                        uv1.y = 0.5f + 0.5f * pct;
                    }
                }
                ImGui::Image(
                    rouen::helpers::texture_id_cast(player_tex),
                    img_size,
                    uv0,
                    uv1
                );
            } else {
                // Fallback placeholder with Jersey Number
                ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
                ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
                draw_list->AddRectFilled(img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), bg_col, 8.0f * dpi_scale);
                draw_list->AddRect(img_pos, ImVec2(img_pos.x + img_size.x, img_pos.y + img_size.y), border_col, 8.0f * dpi_scale);
                
                std::string jersey_str = player.jersey_number > 0 ? std::to_string(player.jersey_number) : "?";
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Bold / Large font
                ImVec2 text_size = ImGui::CalcTextSize(jersey_str.c_str());
                draw_list->AddText(ImVec2(img_pos.x + (img_size.x - text_size.x) / 2.0f, img_pos.y + (img_size.y - text_size.y) / 2.0f),
                                   ImGui::GetColorU32(colors[2]), jersey_str.c_str());
                ImGui::PopFont();
            }

            // Support opening the photo in browser when clicked
            ImGui::SetCursorScreenPos(img_pos);
            if (ImGui::InvisibleButton(std::format("##photo_btn_{}", i).c_str(), img_size)) {
                if (!player.photo_url.empty() && player.photo_url.starts_with("http")) {
                    rouen::platform::open_url(player.photo_url);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to view player photo/link");
            }

            // Column 1: Details
            ImGui::NextColumn();
            
            ImGui::TextColored(colors[2], "%s", player.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "#%d", player.jersey_number);
            
            ImGui::TextColored(colors[3], "%s", player.position.c_str());
            
            ImGui::Spacing();
            ImGui::TextWrapped("%s", player.comment.c_str());

            ImGui::Columns(1);
            ImGui::EndGroup();
            ImGui::Unindent(8.0f * dpi_scale);

            ImVec2 group_max = ImGui::GetItemRectMax();
            float card_bottom = std::max(group_max.y + 6.0f * dpi_scale, start_pos.y + 82.0f * dpi_scale);

            draw_list->ChannelsSetCurrent(0); // Background
            draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(colors[7]), 6.0f * dpi_scale);
            draw_list->AddRect(start_pos, ImVec2(start_pos.x + card_width, card_bottom), ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 0.8f)), 6.0f * dpi_scale);

            draw_list->ChannelsMerge();
            
            // Advance cursor past the card box
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, card_bottom + 8.0f * dpi_scale));
        }

        // 2. Starting XI Lineup
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(colors[2], "%s Starting XI (Lineup):", ICON_MD_FORMAT_LIST_NUMBERED);
        ImGui::Spacing();

        if (!cache.lineup.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
            float lineup_box_height = static_cast<float>(cache.lineup.size()) * 26.0f * dpi_scale + 35.0f * dpi_scale;
            ImGui::BeginChild("LineupChildBox", ImVec2(0, lineup_box_height), true);
            
            ImGui::Columns(3, "LineupColumns", false);
            ImGui::SetColumnWidth(0, 50.0f * dpi_scale);   // Jersey number
            ImGui::SetColumnWidth(1, 150.0f * dpi_scale);  // Position
            
            // Header
            ImGui::TextColored(colors[3], "#"); ImGui::NextColumn();
            ImGui::TextColored(colors[3], "Position"); ImGui::NextColumn();
            ImGui::TextColored(colors[3], "Name"); ImGui::NextColumn();
            ImGui::Separator();

            for (const auto& lp : cache.lineup) {
                ImGui::TextColored(colors[2], "%d", lp.jersey_number); ImGui::NextColumn();
                ImGui::Text("%s", lp.position.c_str()); ImGui::NextColumn();
                ImGui::Text("%s", lp.name.c_str()); ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::TextColored(colors[5], "Lineup details not available.");
        }

        // 3. Team Q&A Section
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(colors[2], "%s Team Q&A / FAQs:", ICON_MD_QUESTION_ANSWER);
        ImGui::Spacing();

        if (!cache.qa.empty()) {
            for (size_t q_idx = 0; q_idx < cache.qa.size(); ++q_idx) {
                const auto& qa = cache.qa[q_idx];
                ImGui::PushStyleColor(ImGuiCol_Header, colors[7]);
                if (ImGui::CollapsingHeader(std::format("Q: {}", qa.question).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent(15.0f);
                    ImGui::TextWrapped("%s", qa.answer.c_str());
                    ImGui::Unindent(15.0f);
                    ImGui::Spacing();
                }
                ImGui::PopStyleColor(); // Pop Header
            }
        } else {
            ImGui::TextColored(colors[5], "Q&A details not available.");
        }

    }

    time_t get_match_utc_time(const std::string& local_date_str, const std::string& stadium_id) {
        if (local_date_str.size() < 16) return 0;
        
        try {
            int month = std::stoi(local_date_str.substr(0, 2));
            int day = std::stoi(local_date_str.substr(3, 2));
            int year = std::stoi(local_date_str.substr(6, 4));
            int hour = std::stoi(local_date_str.substr(11, 2));
            int minute = std::stoi(local_date_str.substr(14, 2));
            
            int offset_hours = 0;
            if (stadium_id == "1") offset_hours = -6;      // Mexico City
            else if (stadium_id == "2") offset_hours = -6; // Guadalajara
            else if (stadium_id == "3") offset_hours = -6; // Monterrey
            else if (stadium_id == "4") offset_hours = -5; // Dallas
            else if (stadium_id == "5") offset_hours = -5; // Houston
            else if (stadium_id == "6") offset_hours = -5; // Kansas City
            else if (stadium_id == "7") offset_hours = -4; // Atlanta
            else if (stadium_id == "8") offset_hours = -4; // Miami
            else if (stadium_id == "9") offset_hours = -4; // Boston
            else if (stadium_id == "10") offset_hours = -4; // Philadelphia
            else if (stadium_id == "11") offset_hours = -4; // New York/New Jersey
            else if (stadium_id == "12") offset_hours = -4; // Toronto
            else if (stadium_id == "13") offset_hours = -7; // Vancouver
            else if (stadium_id == "14") offset_hours = -7; // Seattle
            else if (stadium_id == "15") offset_hours = -7; // San Francisco
            else if (stadium_id == "16") offset_hours = -7; // Los Angeles
            else offset_hours = -5;
            
            struct tm t = {};
            t.tm_year = year - 1900;
            t.tm_mon = month - 1;
            t.tm_mday = day;
            t.tm_hour = hour;
            t.tm_min = minute;
            t.tm_sec = 0;
            t.tm_isdst = 0;
            
#ifdef _WIN32
            time_t utc_time = _mkgmtime(&t);
#else
            time_t utc_time = timegm(&t);
#endif
            utc_time -= offset_hours * 3600;
            return utc_time;
        } catch (...) {
            return 0;
        }
    }

    std::string format_local_time(time_t utc_time) {
        if (utc_time == 0) return "Unknown Date/Time";
        struct tm* local_tm = std::localtime(&utc_time);
        if (!local_tm) return "Unknown Date/Time";
        char buf[64];
        std::strftime(buf, sizeof(buf), "%B %d, %Y - %I:%M %p", local_tm);
        return std::string(buf);
    }

    std::string format_local_hour_minute(time_t utc_time) {
        if (utc_time == 0) return "TBD";
        struct tm* local_tm = std::localtime(&utc_time);
        if (!local_tm) return "TBD";
        char buf[32];
        std::strftime(buf, sizeof(buf), "%I:%M %p", local_tm);
        return std::string(buf);
    }

    std::string normalize_team_name(const std::string& name) {
        std::string s = name;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        
        size_t pos;
        while ((pos = s.find(" and ")) != std::string::npos) {
            s.replace(pos, 5, " ");
        }
        while ((pos = s.find("-")) != std::string::npos) {
            s.replace(pos, 1, " ");
        }
        
        std::string clean;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                clean.push_back(c);
            }
        }
        if (clean == "czechia") clean = "czechrepublic";
        if (clean == "usa") clean = "unitedstates";
        return clean;
    }

    std::string get_match_time_display(const Match& m) {
        if (m.status == "COMPLETED") {
            return "FT";
        }
        if (m.status == "UPCOMING") {
            time_t utc = get_match_utc_time(m.date_str, m.stadium_id);
            if (utc != 0) {
                return format_local_hour_minute(utc);
            }
            return m.time_str;
        }
        // If status is LIVE
        std::string display_time = m.time_str;
        // If it is a raw status indicator like "live", "notstarted", or formatted time (contains ':')
        bool is_raw_indicator = (display_time == "live" || display_time == "notstarted" || display_time.find(':') != std::string::npos || display_time.empty());
        
        if (is_raw_indicator) {
            time_t utc_kickoff = get_match_utc_time(m.date_str, m.stadium_id);
            if (utc_kickoff != 0) {
                auto now = std::chrono::system_clock::now();
                time_t now_time = std::chrono::system_clock::to_time_t(now);
                double diff_sec = std::difftime(now_time, utc_kickoff);
                if (diff_sec >= 0) {
                    int elapsed_mins = static_cast<int>(diff_sec) / 60;
                    if (elapsed_mins <= 45) {
                        display_time = std::format("{}'", elapsed_mins);
                    } else if (elapsed_mins <= 60) {
                        display_time = "HT";
                    } else if (elapsed_mins <= 105) {
                        display_time = std::format("{}'", elapsed_mins - 15);
                    } else {
                        display_time = "90+'";
                    }
                } else {
                    display_time = "1'"; // Match just started
                }
            } else {
                display_time = "LIVE";
            }
        } else if (!display_time.empty() && std::all_of(display_time.begin(), display_time.end(), ::isdigit)) {
            display_time += "'";
        }
        return display_time;
    }
};

} // namespace rouen::cards

template <>
struct glz::meta<rouen::cards::worldcup_dashboard::CommentaryCache> {
    using T = rouen::cards::worldcup_dashboard::CommentaryCache;
    static constexpr auto value = object(
        "text", &T::text,
        "last_updated_epoch", &T::last_updated_epoch,
        "last_home_score", &T::last_home_score,
        "last_away_score", &T::last_away_score,
        "status", &T::status
    );
};

template <>
struct glz::meta<rouen::cards::worldcup_dashboard::PlayerInfo> {
    using T = rouen::cards::worldcup_dashboard::PlayerInfo;
    static constexpr auto value = object(
        "name", &T::name,
        "photo_url", &T::photo_url,
        "position", &T::position,
        "jersey_number", &T::jersey_number,
        "comment", &T::comment
    );
};

template <>
struct glz::meta<rouen::cards::worldcup_dashboard::LineupPlayer> {
    using T = rouen::cards::worldcup_dashboard::LineupPlayer;
    static constexpr auto value = object(
        "name", &T::name,
        "position", &T::position,
        "jersey_number", &T::jersey_number
    );
};

template <>
struct glz::meta<rouen::cards::worldcup_dashboard::QAPair> {
    using T = rouen::cards::worldcup_dashboard::QAPair;
    static constexpr auto value = object(
        "question", &T::question,
        "answer", &T::answer
    );
};

template <>
struct glz::meta<rouen::cards::worldcup_dashboard::TeamPlayersCache> {
    using T = rouen::cards::worldcup_dashboard::TeamPlayersCache;
    static constexpr auto value = object(
        "players", &T::players,
        "lineup", &T::lineup,
        "qa", &T::qa,
        "last_updated_epoch", &T::last_updated_epoch
    );
};


