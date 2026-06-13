#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../registrar.hpp"
#include "../../helpers/fetch.hpp"
#include <glaze/glaze.hpp>
#include "../../helpers/flag_renderer.hpp"

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

    worldcup_dashboard() {
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

        // Start Background Data Fetch
        fetch_real_data();
    }

    ~worldcup_dashboard() override = default;

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
            // Header decoration
            draw_header_banner();
            
            ImGui::Spacing();
            
            if (ImGui::BeginTabBar("WorldCupTabs", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem(ICON_MD_SPORTS_SOCCER " Match Center")) {
                    render_match_center();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_FORMAT_LIST_BULLETED " Standings")) {
                    render_standings();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_TRACK_CHANGES " Team Tracker")) {
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

    void draw_header_banner() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[0]);
        ImGui::BeginChild("HeaderBanner", ImVec2(0, 50), true, ImGuiWindowFlags_NoScrollbar);
        
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colors[2], "  %s", ICON_MD_EMOJI_EVENTS);
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use large monospaced/header font
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "FIFA WORLD CUP 2026");
        ImGui::PopFont();
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 200);
        ImGui::TextColored(colors[5], "USA • CANADA • MEXICO");
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
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
            ImGui::Spacing();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2 - 120);
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
            bool has_scorers = is_live && (!featured->home_scorers.empty() || !featured->away_scorers.empty());
            float feat_height = has_scorers ? 220.0f : (is_live ? 165.0f : 145.0f);

            ImGui::TextColored(colors[2], "Featured Match");
            ImGui::Separator();
            
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
            ImGui::BeginChild("FeaturedMatchBox", ImVec2(0, feat_height), true);
            
            ImGui::Columns(3, "featMatchCols", false);
            ImGui::SetColumnWidth(0, 180);
            ImGui::SetColumnWidth(1, 160);
            ImGui::SetColumnWidth(2, 180);

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
            ImGui::Indent(15);
            if (is_live) {
                if (home_factor > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, home_factor), "%s GOAL!", ICON_MD_SPORTS_SOCCER);
                } else {
                    ImGui::Text(""); // Fixed space to prevent layout shifts
                }
            }
            ImVec2 flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36, 24));
            flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36, 24), featured->home_code);
            ImGui::Spacing();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("%s", featured->home_team.c_str());
            ImGui::PopFont();
            ImGui::TextColored(colors[5], "%s", featured->home_code.c_str());
            ImGui::Unindent(15);

            // Score/Status
            ImGui::NextColumn();
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            if (featured->status == "LIVE") {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 35);
                ImGui::TextColored(colors[4], "%s LIVE", ICON_MD_FIBER_MANUAL_RECORD);
                
                ImGui::Spacing();
                std::string score_str = std::format("{} - {}", featured->home_score, featured->away_score);
                ImVec2 text_size = ImGui::CalcTextSize(score_str.c_str());
                ImVec2 cur_pos = ImGui::GetCursorScreenPos();
                float offset_x = (160.0f - text_size.x) / 2.0f;
                ImVec2 score_min = ImVec2(cur_pos.x + offset_x - 12.0f, cur_pos.y - 4.0f);
                ImVec2 score_max = ImVec2(cur_pos.x + offset_x + text_size.x + 12.0f, cur_pos.y + text_size.y + 4.0f);
                
                if (max_factor > 0.0f) {
                    ImVec4 glow_color = ImVec4(0.2f, 0.8f, 0.4f, 0.35f * max_factor);
                    ImGui::GetWindowDrawList()->AddRectFilled(score_min, score_max, ImGui::GetColorU32(glow_color), 8.0f);
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
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (160.0f - text_width) / 2.0f);
                ImGui::TextColored(colors[3], "%s", live_time.c_str());
            } else if (featured->status == "COMPLETED") {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);
                ImGui::TextColored(colors[5], "COMPLETED");
                ImGui::Spacing();
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 30);
                ImGui::Text("%d - %d", featured->home_score, featured->away_score);
                ImGui::PopFont();
                
                ImGui::Spacing();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15);
                ImGui::TextColored(colors[5], "%s", featured->time_str.c_str());
            } else {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 35);
                ImGui::TextColored(colors[2], "UPCOMING");
                ImGui::Spacing();
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 45);
                ImGui::Text("VS");
                ImGui::PopFont();
                
                ImGui::Spacing();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15);
                std::string feat_time = featured->time_str;
                time_t utc = get_match_utc_time(featured->date_str, featured->stadium_id);
                if (utc != 0) {
                    feat_time = format_local_hour_minute(utc);
                }
                ImGui::TextColored(colors[5], "%s", feat_time.c_str());
            }

            // Away Team
            ImGui::NextColumn();
            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Indent(15);
            if (is_live) {
                if (away_factor > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, away_factor), "%s GOAL!", ICON_MD_SPORTS_SOCCER);
                } else {
                    ImGui::Text("");
                }
            }
            flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36, 24));
            flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36, 24), featured->away_code);
            ImGui::Spacing();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("%s", featured->away_team.c_str());
            ImGui::PopFont();
            ImGui::TextColored(colors[5], "%s", featured->away_code.c_str());
            ImGui::Unindent(15);

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Spacing();
            
            if (has_scorers) {
                ImGui::TextColored(colors[2], "⚽ Match Events / Scorers:");
                ImGui::Indent(10.0f);
                if (!featured->home_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s: %s", featured->home_team.c_str(), featured->home_scorers.c_str());
                }
                if (!featured->away_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s: %s", featured->away_team.c_str(), featured->away_scorers.c_str());
                }
                ImGui::Unindent(10.0f);
                ImGui::Separator();
                ImGui::Spacing();
            }
            
            ImGui::TextColored(colors[6], "  %s  %s", ICON_MD_PLACE, featured->venue.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 150);
            ImGui::TextColored(colors[5], "%s", featured->group.c_str());

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::TextColored(colors[3], "%s Connected to Live FIFA API", ICON_MD_SIGNAL_CELLULAR_4_BAR);

        ImGui::Spacing();
        ImGui::TextColored(colors[2], "Fixture List & Scores:");
        ImGui::Separator();

        // Render completed and other matches in a scrollable list
        ImGui::BeginChild("MatchScrollBox", ImVec2(0, 0), false);
        for (const auto& m : display_matches) {
            // Skip the featured match since it's pinned to the top
            if (featured && m.home_code == featured->home_code && m.away_code == featured->away_code && m.date_str == featured->date_str) {
                continue;
            }
            
            bool has_scorers = (m.status == "COMPLETED" || m.status == "LIVE") && (!m.home_scorers.empty() || !m.away_scorers.empty());
            float height = has_scorers ? 85.0f : 60.0f;
            
            std::string m_flash_key = m.home_code + "_" + m.away_code + "_" + m.date_str;
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

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg_color);
            ImGui::BeginChild(std::format("match_{}_{}_{}", m.home_code, m.away_code, m.date_str).c_str(), ImVec2(0, height), true);
            
            // Drawing flag and home team
            ImGui::AlignTextToFramePadding();
            ImVec2 fpos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24, 16));
            flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(24, 16), m.home_code);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::Text("%s", m.home_team.c_str());
            
            // Score / Status
            ImGui::SameLine(ImGui::GetWindowWidth() - 250);
            if (m.status == "COMPLETED") {
                ImGui::TextColored(colors[3], "%d - %d", m.home_score, m.away_score);
                ImGui::SameLine(ImGui::GetWindowWidth() - 170);
                ImGui::TextColored(colors[5], "%s", m.time_str.c_str());
            } else if (m.status == "LIVE") {
                ImGui::TextColored(colors[4], "%d - %d", m.home_score, m.away_score);
                if (row_factor > 0.0f) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, row_factor), "%s", ICON_MD_SPORTS_SOCCER);
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - 170);
                std::string display_time = get_match_time_display(m);
                ImGui::TextColored(colors[4], "%s LIVE", display_time.c_str());
            } else {
                ImGui::TextColored(colors[6], "VS");
                ImGui::SameLine(ImGui::GetWindowWidth() - 170);
                std::string display_time = m.time_str;
                time_t utc = get_match_utc_time(m.date_str, m.stadium_id);
                if (utc != 0) {
                    display_time = format_local_hour_minute(utc);
                }
                ImGui::TextColored(colors[2], "%s", display_time.c_str());
            }
            
            // Away team and flag
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            fpos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24, 16));
            flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(24, 16), m.away_code);
            ImGui::SameLine();
            ImGui::Text("%s", m.away_team.c_str());

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
                ImGui::Indent(32.0f);
                if (!m.home_scorers.empty()) {
                    ImGui::TextColored(colors[5], "%s %s", ICON_MD_SPORTS_SOCCER, m.home_scorers.c_str());
                } else {
                    ImGui::Text("");
                }
                ImGui::Unindent(32.0f);

                if (!m.away_scorers.empty()) {
                    ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
                    ImGui::TextColored(colors[5], "%s %s", ICON_MD_SPORTS_SOCCER, m.away_scorers.c_str());
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    void render_standings() {
        bool loaded = false;
        std::unordered_map<std::string, std::vector<GroupTeam>> active_groups;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            active_groups = api_groups;
            loaded = data_loaded;
        }

        if (!loaded) {
            ImGui::Spacing();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2 - 120);
            ImGui::TextColored(colors[6], "Loading group standings...");
            return;
        }

        static int selected_group_idx = 3; // Defaults to Group D (USA)
        static const char* group_dropdown_names[] = {
            "Group A", "Group B", "Group C", "Group D", "Group E", "Group F",
            "Group G", "Group H", "Group I", "Group J", "Group K", "Group L"
        };
        
        ImGui::SetNextItemWidth(150);
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
                ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed, 25.0f);
                ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 25.0f);
                ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 25.0f);
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 25.0f);
                ImGui::TableSetupColumn("GD", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("PTS", ImGuiTableColumnFlags_WidthFixed, 35.0f);
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
                    ImGui::Dummy(ImVec2(20, 13));
                    flags::draw_flag(ImGui::GetWindowDrawList(), fpos, ImVec2(20, 13), t.code);
                    ImGui::SameLine();
                    ImGui::TextColored(text_color, "%s", t.name.c_str());
                    
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
        for (const auto& s : stadiums) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
            ImGui::BeginChild(s.name.c_str(), ImVec2(0, 100.0f), true);
            
            ImGui::TextColored(colors[2], "%s", s.name.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 180);
            ImGui::TextColored(colors[5], "Capacity: %d", s.capacity);
            
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
            ImGui::Spacing();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2 - 120);
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

        static int selected_idx = 0;
        if (selected_idx >= static_cast<int>(sorted_teams.size())) {
            selected_idx = 0;
        }

        ImGui::TextColored(colors[2], "Select a Team to Track:");
        ImGui::SetNextItemWidth(250.0f);
        
        // Build array of char* for Combo
        std::vector<std::string> combo_items;
        std::vector<const char*> combo_items_cstr;
        for (const auto& item : sorted_teams) {
            combo_items.push_back(std::format("{} ({})", item.second, item.first));
        }
        for (const auto& s : combo_items) {
            combo_items_cstr.push_back(s.c_str());
        }

        ImGui::Combo("##TeamCombo", &selected_idx, combo_items_cstr.data(), static_cast<int>(combo_items_cstr.size()));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        size_t sel_u_idx = static_cast<size_t>(selected_idx);
        std::string sel_code = sorted_teams[sel_u_idx].first;
        std::string sel_name = sorted_teams[sel_u_idx].second;

        // Draw selected team flag next to name
        ImVec2 flag_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(36, 24));
        flags::draw_flag(ImGui::GetWindowDrawList(), flag_pos, ImVec2(36, 24), sel_code);
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::Text("%s", sel_name.c_str());
        ImGui::PopFont();

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

        if (!upcoming_matches.empty()) {
            const Match* next_match = upcoming_matches[0];
            bool is_live = (next_match->status == "LIVE");
            float feat_height = is_live ? 200.0f : 145.0f;
            
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

            ImGui::PushStyleColor(ImGuiCol_ChildBg, next_bg_color);
            ImGui::BeginChild("NextMatchBox", ImVec2(0, feat_height), true);
            
            // Match detail header
            ImGui::TextColored(colors[5], "%s - %s", next_match->group.c_str(), next_match->venue.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Render vs. Opponent
            bool is_home = (next_match->home_code == sel_code);
            std::string opponent_name = is_home ? next_match->away_team : next_match->home_team;
            std::string opponent_code = is_home ? next_match->away_code : next_match->home_code;
            
            ImGui::AlignTextToFramePadding();
            ImVec2 op_flag_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(24, 16));
            flags::draw_flag(ImGui::GetWindowDrawList(), op_flag_pos, ImVec2(24, 16), opponent_code);
            ImGui::SameLine();
            ImGui::Text("Vs. %s (%s)", opponent_name.c_str(), opponent_code.c_str());
            
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
                
                if (!next_match->home_scorers.empty() || !next_match->away_scorers.empty()) {
                    ImGui::Spacing();
                    ImGui::TextColored(colors[2], "⚽ Scorers:");
                    ImGui::Indent(10.0f);
                    if (!next_match->home_scorers.empty()) {
                        ImGui::TextColored(colors[5], "%s: %s", next_match->home_team.c_str(), next_match->home_scorers.c_str());
                    }
                    if (!next_match->away_scorers.empty()) {
                        ImGui::TextColored(colors[5], "%s: %s", next_match->away_team.c_str(), next_match->away_scorers.c_str());
                    }
                    ImGui::Unindent(10.0f);
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
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
            
            // Render subsequent matches
            if (upcoming_matches.size() > 1) {
                ImGui::Spacing();
                ImGui::TextColored(colors[2], "Other Upcoming Matches:");
                ImGui::Separator();
                
                ImGui::BeginChild("SubsequentMatchesScroll", ImVec2(0, 0), false);
                for (size_t i = 1; i < upcoming_matches.size(); ++i) {
                    const Match* m = upcoming_matches[i];
                    
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[7]);
                    ImGui::BeginChild(std::format("sub_match_{}_{}", sel_code, i).c_str(), ImVec2(0, 68.0f), true);
                    
                    bool is_m_home = (m->home_code == sel_code);
                    std::string opp_name = is_m_home ? m->away_team : m->home_team;
                    std::string opp_code = is_m_home ? m->away_code : m->home_code;
                    
                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, 260.0f);
                    
                    // Column 0: Opponent
                    ImGui::AlignTextToFramePadding();
                    ImVec2 sub_flag_pos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(24, 16));
                    flags::draw_flag(ImGui::GetWindowDrawList(), sub_flag_pos, ImVec2(24, 16), opp_code);
                    ImGui::SameLine();
                    ImGui::Text("Vs. %s (%s)", opp_name.c_str(), opp_code.c_str());
                    
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
                    
                    ImGui::Columns(1);
                    
                    // Hover tooltip
                    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                        ImGui::SetTooltip("Venue: %s\nGroup/Stage: %s\nStadium Local: %s\nYour Local: %s", 
                                          m->venue.c_str(), m->group.c_str(), m->date_str.c_str(), sub_localized_time.c_str());
                    }
                    
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
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
