#pragma once
#include <string>
#include <vector>
#include <future>
#include "../../helpers/chess_com_api.hpp"
#include <imgui.h>

namespace rouen::cards {

struct ChessComIntegrationState {
    char username_buffer[64] = {};
    std::vector<std::string> player_archives;
    std::vector<rouen::chess::ChessGameArchive> archive_games;
    std::string selected_archive;
    int selected_game_index = -1;
    bool is_fetching_archives = false;
    bool is_fetching_games = false;
    std::string api_error;
    bool auto_search_on_render = false;
    bool from_chess_com = false;
    std::string chess_com_username;
    std::future<std::pair<bool, std::string>> archives_future;
    std::future<std::pair<bool, std::string>> games_future;
};

class ChessComIntegration {
public:
    ChessComIntegration(ChessComIntegrationState& state, rouen::chess::ChessComApiClient& api)
        : state_(state), api_(api) {}

    void render_ui(ImVec4 info_color, ImVec4 error_color, std::function<void()> on_game_selected);
    void fetch_player_archives(const std::string& username);
    void fetch_archive_games(const std::string& archive_url);
    void process_api_responses();
    std::string get_selected_pgn() const;
    std::string get_selected_game_display() const;
    bool has_selected_game() const;
    // ... add more as needed ...
private:
    ChessComIntegrationState& state_;
    rouen::chess::ChessComApiClient& api_;
};

} // namespace rouen::cards
