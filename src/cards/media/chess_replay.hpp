#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <array>
#include <unordered_map>
#include <filesystem>
#include <future>
#include <optional>
#include <algorithm>
#include <map>
#include <glaze/json.hpp>

#include "imgui.h"
#include <SDL.h>
#include <SDL_image.h>

#include "../interface/card.hpp"
#include "../../models/chess/chess.hpp"
#include "../../helpers/debug.hpp"  // Include debug system for better logging
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/fetch.hpp"  // For Chess.com API requests
#include "../../registrar.hpp"
#include "../../fonts.hpp"  // For font utilities
#include "../../../external/IconsMaterialDesign.h"

// Chess-specific debug macros are already defined in debug.hpp
// No need to redefine them here

namespace rouen::cards {
    
// Chess.com API response structures
struct ChessGameArchive {
    std::string url;
    std::string pgn;
    std::string time_class;
    int64_t end_time;
    std::string white_username;
    std::string black_username;
    std::string result;
};

// JSON structure for Chess.com API game response
struct ChessComGame {
    std::string url;
    std::string pgn;
    std::string time_class;
    int64_t end_time;
    
    // Add missing fields that might be in the actual response
    struct Players {
        struct Player {
            std::string username;
            int result; // 'win', 'lose', 'draw' encoded as integers
            
            // Define additional fields as needed
            std::string rating;
        };
        
        Player white;
        Player black;
    };
    
    Players players;
    std::string result; // The game result string (e.g., "1-0")
    
    // Additional fields can be added here as needed
};

// JSON structure for Chess.com API archives response
struct ChessComArchives {
    std::vector<std::string> archives;
};

// JSON structure for Chess.com API games response
struct ChessComGamesResponse {
    std::vector<ChessComGame> games;
};

// Glaze metadata for Chess.com API structures
} // namespace rouen::cards

// Define glaze schema for Chess.com API structures
template <>
struct glz::meta<rouen::cards::ChessComGame::Players::Player> {
    using T = rouen::cards::ChessComGame::Players::Player;
    static constexpr auto value = glz::object(
        "username", &T::username,
        "result", &T::result,
        "rating", &T::rating
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::cards::ChessComGame::Players> {
    using T = rouen::cards::ChessComGame::Players;
    static constexpr auto value = glz::object(
        "white", &T::white,
        "black", &T::black
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::cards::ChessComGame> {
    using T = rouen::cards::ChessComGame;
    static constexpr auto value = glz::object(
        "url", &T::url,
        "pgn", &T::pgn,
        "time_class", &T::time_class,
        "end_time", &T::end_time,
        "players", &T::players,
        "result", &T::result
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::cards::ChessComArchives> {
    using T = rouen::cards::ChessComArchives;
    static constexpr auto value = glz::object(
        "archives", &T::archives
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::cards::ChessComGamesResponse> {
    using T = rouen::cards::ChessComGamesResponse;
    static constexpr auto value = glz::object(
        "games", &T::games
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

namespace rouen::cards {

class chess_replay : public card {
public:
    chess_replay(std::string_view pgn_path = "") {
        // Set custom colors for the chess card
        colors[0] = {0.4f, 0.6f, 0.3f, 1.0f}; // Green primary color
        colors[1] = {0.5f, 0.7f, 0.4f, 0.7f}; // Lighter green secondary color
        
        // Additional colors for board and pieces
        get_color(2, ImVec4(0.8f, 0.8f, 0.7f, 1.0f)); // Light squares
        get_color(3, ImVec4(0.5f, 0.6f, 0.4f, 1.0f)); // Dark squares
        get_color(4, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // White pieces (fallback)
        get_color(5, ImVec4(0.2f, 0.2f, 0.2f, 1.0f)); // Black pieces (fallback)
        get_color(6, ImVec4(0.9f, 0.5f, 0.5f, 0.7f)); // Highlighted square
        get_color(7, ImVec4(0.3f, 0.7f, 0.3f, 0.7f)); // Last move indicator
        get_color(8, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Error color
        get_color(9, ImVec4(0.3f, 0.3f, 0.8f, 1.0f)); // Info color for Chess.com
        
        name("Chess Replay");
        requested_fps = 30;  // Higher refresh rate for smoother animations
        width = 700.0f;      // Wider card to accommodate board and move list
        
        // Create chess game model
        game = std::make_unique<models::chess::Game>();
        
        // Try to get the SDL renderer from the registrar
        try {
            renderer = *registrar::get<SDL_Renderer*>("main_renderer");
            CHESS_INFO("Got main renderer for chess piece images");
        } catch (const std::exception& e) {
            CHESS_ERROR_FMT("Failed to get renderer from registrar: {}", e.what());
            renderer = nullptr;
        }
        
        // Load chess piece textures
        if (renderer) {
            load_piece_textures();
        }
        
        // If a PGN path was provided, load it
        if (!pgn_path.empty()) {
            load_pgn(std::string(pgn_path));
        }
        
        CHESS_INFO("Chess replay card initialized");
    }
    
    ~chess_replay() override {
        // Clean up textures
        for (auto& [piece, texture] : piece_textures) {
            if (texture) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }
        
        CHESS_INFO("Chess replay card destroyed");
    }
    
    std::string get_uri() const override {
        // Return chess-com URI if this is from the Chess.com integration
        if (from_chess_com && !chess_com_username.empty()) {
            return "chess-com:" + chess_com_username;
        }
        
        return "chess";
    }
    
    bool render() override {
        return render_window([this]() {
            if (!game) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Chess game model not initialized");
                return;
            }
            
            // Check if we need to auto-search for a username
            if (auto_search_on_render) {
                auto_search_on_render = false; // Only do this once
                fetch_player_archives(username_buffer);
                from_chess_com = true;
                chess_com_username = username_buffer;
            }
            
            // Calculate board size based on card width
            float board_size = std::min(width - 40.0f, 400.0f);
            float square_size = board_size / 8.0f;
            
            // Show Chess.com API UI at the top
            render_chess_com_ui();
            
            ImGui::Separator();
            
            // Create a two-column layout: board on left, controls and moves on right
            ImGui::Columns(2, "ChessLayout", false);
            ImGui::SetColumnWidth(0, board_size + 20.0f);
            
            // Left column: Chess board
            render_board(board_size, square_size);
            ImGui::NextColumn();
            
            // Right column: Game info, controls, and move list
            render_game_info();
            ImGui::Separator();
            render_controls();
            ImGui::Separator();
            render_move_list();
            
            // Reset column layout
            ImGui::Columns(1);
            
            // Process any pending API responses
            process_api_responses();
        });
    }
    
    // Load a PGN file
    bool load_pgn(const std::string& filepath) {
        if (!game) {
            CHESS_ERROR("Chess game model not initialized");
            return false;
        }
        
        bool success = game->load_from_pgn_file(filepath);
        if (success) {
            CHESS_INFO_FMT("Successfully loaded PGN file: {}", filepath);
            loaded_pgn_path = filepath;
        } else {
            CHESS_ERROR_FMT("Failed to load PGN file: {}", filepath);
        }
        
        return success;
    }
    
    // Load PGN from string content
    bool load_pgn_content(const std::string& pgn_content) {
        if (!game) {
            CHESS_ERROR("Chess game model not initialized");
            return false;
        }
        
        bool success = game->load_from_pgn(pgn_content);
        if (success) {
            CHESS_INFO("Successfully loaded PGN from content");
        } else {
            CHESS_ERROR("Failed to load PGN from content");
        }
        
        return success;
    }
    
    // Set the username to search for Chess.com games (public method for registrar)
    void set_chess_com_username(const std::string& username) {
        std::strncpy(username_buffer, username.c_str(), sizeof(username_buffer) - 1);
        username_buffer[sizeof(username_buffer) - 1] = '\0';
        chess_com_username = username;
        from_chess_com = true;
        auto_search_on_render = true;
    }
    
private:
    // Fix common issues with Chess.com PGN format
    std::string fix_chess_com_pgn(const std::string& pgn) {
        if (pgn.empty()) {
            return pgn;
        }
        
        std::string fixed_pgn = pgn;
        
        // Fix 1: Some PGNs have escaped newlines that should be real newlines
        size_t pos = 0;
        while ((pos = fixed_pgn.find("\\n", pos)) != std::string::npos) {
            fixed_pgn.replace(pos, 2, "\n");
            pos += 1; // Move past the inserted newline
        }
        
        // Fix 2: Some PGNs have incorrect tag formatting
        pos = 0;
        while ((pos = fixed_pgn.find("[", pos)) != std::string::npos) {
            // Check if there's no space after the tag name
            size_t tag_end = fixed_pgn.find(" ", pos);
            size_t quote_start = fixed_pgn.find("\"", pos);
            
            // If there's a quote before a space or no space, we need to add one
            if (quote_start != std::string::npos && (tag_end == std::string::npos || quote_start < tag_end)) {
                // Get the tag name
                std::string tag_name = fixed_pgn.substr(pos + 1, quote_start - pos - 1);
                // Replace with properly formatted tag
                fixed_pgn.replace(pos, quote_start - pos, "[" + tag_name + " ");
            }
            
            pos = fixed_pgn.find("]", pos);
            if (pos != std::string::npos) {
                pos++; // Move past the closing bracket
            } else {
                break; // Avoid infinite loop if there's no closing bracket
            }
        }
        
        // Fix 3: Ensure there's a blank line between tags and moves
        pos = fixed_pgn.rfind("]");
        if (pos != std::string::npos) {
            // Check if there are already two newlines after the last tag
            size_t next_pos = pos + 1;
            size_t newline_count = 0;
            while (next_pos < fixed_pgn.size() && fixed_pgn[next_pos] == '\n') {
                newline_count++;
                next_pos++;
            }
            
            // If not enough newlines, add them
            if (newline_count < 2) {
                fixed_pgn.insert(pos + 1, std::string(2 - newline_count, '\n'));
            }
        }
        
        // Fix 4: Some PGNs have inconsistent move number formatting
        // For example: 1. e4 e5 2...Nf6 instead of 1. e4 e5 2. Nf6
        std::regex move_number_ellipsis(R"((\d+)\.\.\.([A-Za-z0-9]))");
        fixed_pgn = std::regex_replace(fixed_pgn, move_number_ellipsis, "$1. $2");
        
        // Fix 5: Handle any other specific Chess.com formatting issues
        
        CHESS_DEBUG_FMT("Fixed PGN from {} characters to {} characters", pgn.length(), fixed_pgn.length());
        return fixed_pgn;
    }

    // Render Chess.com API integration UI
    void render_chess_com_ui() {
        ImGui::TextColored(colors[9], "Chess.com Player Games");
        
        // Username input field
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##ChessUsername", username_buffer, sizeof(username_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            fetch_player_archives(username_buffer);
            chess_com_username = username_buffer;
            from_chess_com = true;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Search")) {
            fetch_player_archives(username_buffer);
            chess_com_username = username_buffer;
            from_chess_com = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enter a Chess.com username and click Search to find their games");
        }
        
        // Show loading indicator if fetching data
        if (is_fetching_archives || is_fetching_games) {
            ImGui::SameLine();
            ImGui::Text("Loading...");
        }
        
        // Show error if one occurred
        if (!api_error.empty()) {
            ImGui::TextColored(colors[8], "Error: %s", api_error.c_str());
        }
        
        // Archive months dropdown
        if (!player_archives.empty()) {
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("##ArchiveMonths", selected_archive.empty() ? "Select Month" : selected_archive.c_str())) {
                for (const auto& archive : player_archives) {
                    // Extract month/year from URL: https://api.chess.com/pub/player/{username}/games/YYYY/MM
                    std::string month_display = archive.substr(archive.length() - 7); // Get "YYYY/MM"
                    month_display[4] = '-'; // Replace / with -
                    
                    bool is_selected = (selected_archive == month_display);
                    if (ImGui::Selectable(month_display.c_str(), is_selected)) {
                        selected_archive = month_display;
                        fetch_archive_games(archive);
                    }
                    
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            
            // Games list
            if (!archive_games.empty()) {
                ImGui::SetNextItemWidth(500.0f);
                if (ImGui::BeginCombo("##GamesList", selected_game_index < 0 ? 
                                    "Select Game" : 
                                    format_game_display(archive_games[static_cast<size_t>(selected_game_index)]).c_str())) {
                    for (size_t i = 0; i < archive_games.size(); ++i) {
                        const auto& chess_game_item = archive_games[i];
                        std::string game_display = format_game_display(chess_game_item);
                        
                        bool is_selected = (selected_game_index == static_cast<int>(i));
                        if (ImGui::Selectable(game_display.c_str(), is_selected)) {
                            selected_game_index = static_cast<int>(i);
                            load_selected_game();
                        }
                        
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }
    
    // Format a game for display in the dropdown
    std::string format_game_display(const ChessGameArchive& chess_game) {
        // Format date from timestamp
        std::time_t time = static_cast<std::time_t>(chess_game.end_time);
        std::tm* tm = std::localtime(&time);
        char date_buf[20];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm);
        
        // Format time class (e.g., blitz, rapid)
        std::string time_class = chess_game.time_class;
        if (!time_class.empty()) {
            time_class[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(time_class[0])));
        }
        
        return std::string(date_buf) + " - " + 
               chess_game.white_username + " vs " + chess_game.black_username + 
               " [" + time_class + "] " + chess_game.result;
    }
    
    // Fetch player archives from Chess.com API
    void fetch_player_archives(const std::string& username) {
        if (username.empty()) {
            api_error = "Please enter a username";
            return;
        }
        
        // Clear previous results
        player_archives.clear();
        archive_games.clear();
        selected_archive.clear();
        selected_game_index = -1;
        api_error.clear();
        
        // Set fetching flag
        is_fetching_archives = true;
        
        // Start async fetch
        archives_future = std::async(std::launch::async, [username]() {
            try {
                http::fetch fetcher;
                std::string url = "https://api.chess.com/pub/player/" + username + "/games/archives";
                std::string response = fetcher(url);
                
                return std::make_pair(true, response);
            } catch (const std::exception& e) {
                return std::make_pair(false, std::string(e.what()));
            }
        });
    }
    
    // Fetch games from a specific archive
    void fetch_archive_games(const std::string& archive_url) {
        // Clear previous results
        archive_games.clear();
        selected_game_index = -1;
        api_error.clear();
        
        // Set fetching flag
        is_fetching_games = true;
        
        // Start async fetch
        games_future = std::async(std::launch::async, [archive_url]() {
            try {
                http::fetch fetcher;
                std::string response = fetcher(archive_url);
                
                return std::make_pair(true, response);
            } catch (const std::exception& e) {
                return std::make_pair(false, std::string(e.what()));
            }
        });
    }
    
    // Process any completed API responses
    void process_api_responses() {
        // Check archives future
        if (is_fetching_archives && archives_future.valid()) {
            auto status = archives_future.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready) {
                auto [success, result] = archives_future.get();
                is_fetching_archives = false;
                
                if (success) {
                    try {
                        // Parse JSON to extract archive URLs
                        ChessComArchives archives;
                        auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(archives, result);
                        if (read_error) {
                            api_error = std::string("Error parsing response: ") + glz::format_error(read_error);
                        } else {
                            player_archives = archives.archives;
                        }
                    } catch (const std::exception& e) {
                        api_error = std::string("Error parsing response: ") + e.what();
                    }
                } else {
                    api_error = result;
                }
            }
        }
        
        // Check games future
        if (is_fetching_games && games_future.valid()) {
            auto status = games_future.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready) {
                auto [success, result] = games_future.get();
                is_fetching_games = false;
                
                if (success) {
                    try {
                        // Parse JSON to extract games
                        ChessComGamesResponse games_response;
                        auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(games_response, result);
                        if (!read_error) {
                            archive_games.clear();
                            for (const auto& game_item : games_response.games) {
                                ChessGameArchive archive_game;
                                archive_game.url = game_item.url;
                                archive_game.pgn = game_item.pgn;
                                archive_game.time_class = game_item.time_class;
                                archive_game.end_time = game_item.end_time;
                                
                                // Extract player information from the structured player data
                                if (game_item.players.white.username.empty() || game_item.players.black.username.empty()) {
                                    // If player info isn't available in the structured data, try to extract from PGN
                                    // This is a fallback in case the API response format changes
                                    extract_player_info_from_pgn(game_item.pgn, archive_game);
                                } else {
                                    // Use the properly parsed player information
                                    archive_game.white_username = game_item.players.white.username;
                                    archive_game.black_username = game_item.players.black.username;
                                }
                                
                                // Use the result field if available, otherwise derive from players' results
                                if (!game_item.result.empty()) {
                                    archive_game.result = game_item.result;
                                } else {
                                    // Determine result based on player results (win/loss/draw)
                                    if (game_item.players.white.result == 1) {
                                        archive_game.result = "1-0";
                                    } else if (game_item.players.black.result == 1) {
                                        archive_game.result = "0-1";
                                    } else {
                                        archive_game.result = "1/2-1/2";
                                    }
                                }
                                
                                archive_games.push_back(archive_game);
                            }
                        } else {
                            api_error = std::string("Error parsing response: ") + glz::format_error(read_error);
                        }
                    } catch (const std::exception& e) {
                        api_error = std::string("Error parsing response: ") + e.what();
                    }
                } else {
                    api_error = result;
                }
            }
        }
    }
    
    // Load the selected game
    void load_selected_game() {
        if (selected_game_index < 0 || static_cast<size_t>(selected_game_index) >= archive_games.size()) {
            CHESS_ERROR("Invalid selected game index");
            return;
        }
        
        const auto& selected_game = archive_games[static_cast<size_t>(selected_game_index)];
        if (selected_game.pgn.empty()) {
            CHESS_ERROR("Selected game has empty PGN");
            return;
        }
        
        // Log the first part of the PGN for debugging
        CHESS_INFO_FMT("Loading PGN: {}", selected_game.pgn.substr(0, std::min(size_t(100), selected_game.pgn.size())));
        
        // Use the Glaze JSON library to properly unescape the JSON string
        std::string clean_pgn;
        try {
            // Properly unescape the PGN string using the JSON library
            auto read_result = glz::read_json(clean_pgn, "\"" + selected_game.pgn + "\"");
            if (!read_result) {
                CHESS_ERROR_FMT("Error unescaping PGN: {}", glz::format_error(read_result));
                // Fallback to direct PGN if Glaze unescaping fails
                clean_pgn = selected_game.pgn;
            } else {
                CHESS_INFO("Successfully unescaped PGN JSON string");
            }
        } catch (const std::exception& e) {
            CHESS_ERROR_FMT("Error unescaping PGN: {}", e.what());
            
            // Fallback to direct PGN if Glaze unescaping fails
            clean_pgn = selected_game.pgn;
        }
        
        // Ensure PGN starts with a valid tag
        if (!clean_pgn.empty() && clean_pgn.front() != '[') {
            CHESS_WARN("PGN doesn't start with a tag, adding Event tag");
            clean_pgn = "[Event \"Chess.com Game\"]\n" + clean_pgn;
        }
        
        // Fix common issues with Chess.com PGN format
        clean_pgn = fix_chess_com_pgn(clean_pgn);
        
        // Try to load the PGN content
        bool success = load_pgn_content(clean_pgn);
        
        if (!success) {
            CHESS_ERROR("Failed to load selected game PGN");
            api_error = "Failed to load game. Invalid PGN format.";
        } else {
            api_error.clear();
            CHESS_INFO("Successfully loaded selected game");
        }
    }
    
    // Load chess piece textures from image files
    void load_piece_textures() {
        // Get the current application directory using std::filesystem
        std::filesystem::path app_path = std::filesystem::current_path();
        
        // File paths for the chess piece images - using relative paths
        const std::vector<std::pair<models::chess::Piece, std::string>> piece_files = {
            {models::chess::Piece::WhiteKing, "img/Chess_klt60.png"},
            {models::chess::Piece::WhiteQueen, "img/Chess_qlt60.png"},
            {models::chess::Piece::WhiteRook, "img/Chess_rlt60.png"},
            {models::chess::Piece::WhiteBishop, "img/Chess_blt60.png"},
            {models::chess::Piece::WhiteKnight, "img/Chess_nlt60.png"},
            {models::chess::Piece::WhitePawn, "img/Chess_plt60.png"},
            {models::chess::Piece::BlackKing, "img/Chess_kdt60.png"},
            {models::chess::Piece::BlackQueen, "img/Chess_qdt60.png"},
            {models::chess::Piece::BlackRook, "img/Chess_rdt60.png"},
            {models::chess::Piece::BlackBishop, "img/Chess_bdt60.png"},
            {models::chess::Piece::BlackKnight, "img/Chess_ndt60.png"},
            {models::chess::Piece::BlackPawn, "img/Chess_pdt60.png"}
        };
        
        // Load each texture
        std::size_t success_count = 0;
        for (const auto& [piece, file_path] : piece_files) {
            // Create the full path to the image file
            std::filesystem::path full_path = app_path / file_path;
            
            int width, height;
            SDL_Texture* texture = TextureHelper::loadTextureFromFile(renderer, full_path.string().c_str(), width, height);
            
            if (texture) {
                piece_textures[piece] = texture;
                piece_dimensions[piece] = std::make_pair(width, height);
                success_count++;
                CHESS_INFO_FMT("Loaded chess piece texture: {}", file_path);
            } else {
                CHESS_ERROR_FMT("Failed to load chess piece texture: {}", file_path);
            }
        }
        
        CHESS_INFO_FMT("Loaded {}/{} chess piece textures", success_count, piece_files.size());
        textures_loaded = (success_count == piece_files.size());
    }
    
    // Render the chess board
    void render_board(float board_size, float square_size) {
        if (!game) return;
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 board_pos = ImGui::GetCursorScreenPos();
        
        // Draw board background
        ImGui::Dummy(ImVec2(board_size, board_size));
        
        // Get current position from the game
        const auto& board = game->get_current_position();
        const auto& board_array = board.get_board();
        
        // Draw squares and pieces
        for (int rank = 7; rank >= 0; --rank) {
            for (int file = 0; file < 8; ++file) {
                // Calculate square position
                ImVec2 square_pos = ImVec2(
                    board_pos.x + static_cast<float>(file) * square_size,
                    board_pos.y + static_cast<float>(7 - rank) * square_size
                );
                
                // Determine square color (light or dark)
                bool is_light_square = (file + rank) % 2 == 0;
                ImVec4 square_color = is_light_square ? colors[2] : colors[3];
                
                // Draw square
                draw_list->AddRectFilled(
                    square_pos,
                    ImVec2(square_pos.x + square_size, square_pos.y + square_size),
                    ImGui::ColorConvertFloat4ToU32(square_color)
                );
                
                // Draw file and rank labels on the edges
                if (rank == 0) {
                    // Draw file labels (a-h) at the bottom
                    char file_label[2] = {static_cast<char>('a' + file), '\0'};
                    draw_list->AddText(
                        ImVec2(square_pos.x + square_size * 0.5f - 4.0f, 
                               square_pos.y + square_size + 2.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
                        file_label
                    );
                }
                
                if (file == 0) {
                    // Draw rank labels (1-8) on the left
                    draw_list->AddText(
                        ImVec2(square_pos.x - 14.0f, 
                               square_pos.y + square_size * 0.5f - 8.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
                        std::to_string(rank + 1).c_str()
                    );
                }
                
                // Get piece at this square
                models::chess::Piece piece = board_array[static_cast<std::size_t>(rank) * 8 + static_cast<std::size_t>(file)];
                if (piece != models::chess::Piece::None) {
                    // Draw piece using image texture if available
                    if (renderer && textures_loaded && piece_textures.count(piece) > 0 && piece_textures[piece] != nullptr) {
                        SDL_Texture* texture = piece_textures[piece];
                        auto [tex_width, tex_height] = piece_dimensions[piece];
                        
                        // Calculate scaling to fit the square
                        float scale = square_size / static_cast<float>(tex_width);
                        if (static_cast<float>(tex_height) * scale > square_size) {
                            scale = square_size / static_cast<float>(tex_height);
                        }
                        
                        // Scale down slightly to leave a small margin
                        scale *= 0.9f;
                        
                        // Calculate final dimensions
                        float piece_width = static_cast<float>(tex_width) * scale;
                        float piece_height = static_cast<float>(tex_height) * scale;
                        
                        // Calculate position to center the piece in the square
                        ImVec2 piece_pos = ImVec2(
                            square_pos.x + (square_size - piece_width) * 0.5f,
                            square_pos.y + (square_size - piece_height) * 0.5f
                        );
                        
                        // Draw the texture
                        draw_list->AddImage(
                            (ImTextureID)(intptr_t)texture,
                            piece_pos,
                            ImVec2(piece_pos.x + piece_width, piece_pos.y + piece_height)
                        );
                    } else {
                        // Fallback to a colored rectangle if image not available
                        ImVec4 piece_color = models::chess::Board::is_white(piece) ? colors[4] : colors[5];
                        
                        // Draw a placeholder for the piece
                        float piece_size = square_size * 0.7f;
                        float piece_margin = (square_size - piece_size) * 0.5f;
                        
                        draw_list->AddRectFilled(
                            ImVec2(square_pos.x + piece_margin, square_pos.y + piece_margin),
                            ImVec2(square_pos.x + piece_margin + piece_size, square_pos.y + piece_margin + piece_size),
                            ImGui::ColorConvertFloat4ToU32(piece_color),
                            piece_size * 0.25f  // Rounded corners
                        );
                        
                        // Add a piece identifier letter in the center
                        char piece_label = get_piece_letter(piece);
                        ImVec2 text_size = ImGui::CalcTextSize(&piece_label, &piece_label + 1);
                        
                        draw_list->AddText(
                            ImVec2(square_pos.x + (square_size - text_size.x) * 0.5f,
                                   square_pos.y + (square_size - text_size.y) * 0.5f),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)),
                            &piece_label, &piece_label + 1
                        );
                    }
                }
            }
        }
    }
    
    // Helper to get a letter representing the piece for fallback display
    char get_piece_letter(models::chess::Piece piece) {
        using Piece = models::chess::Piece;
        
        bool is_white = models::chess::Board::is_white(piece);
        
        switch (piece) {
            case Piece::WhiteKing:
            case Piece::BlackKing:
                return is_white ? 'K' : 'k';
            case Piece::WhiteQueen:
            case Piece::BlackQueen:
                return is_white ? 'Q' : 'q';
            case Piece::WhiteRook:
            case Piece::BlackRook:
                return is_white ? 'R' : 'r';
            case Piece::WhiteBishop:
            case Piece::BlackBishop:
                return is_white ? 'B' : 'b';
            case Piece::WhiteKnight:
            case Piece::BlackKnight:
                return is_white ? 'N' : 'n';
            case Piece::WhitePawn:
            case Piece::BlackPawn:
                return is_white ? 'P' : 'p';
            default:
                return ' ';
        }
    }
    
    // Render game information
    void render_game_info() {
        if (!game) return;
        
        // If a game is loaded, display its metadata
        if (game->get_move_count() > 0) {
            ImGui::TextColored(colors[0], "Game Info:");
            
            ImGui::Text("Event: %s", game->event.c_str());
            ImGui::Text("White: %s", game->white_player.c_str());
            ImGui::Text("Black: %s", game->black_player.c_str());
            
            if (!game->date.empty()) {
                ImGui::Text("Date: %s", game->date.c_str());
            }
            
            ImGui::Text("Result: %s", game->result.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No game loaded");
        }
        
        // PGN file loading button
        if (ImGui::Button("Load PGN File")) {
            // In a real application, you'd open a file dialog
            // For now, let's assume a fixed file path or use the previously loaded one
            std::string filepath = "/path/to/chess/game.pgn";
            if (!loaded_pgn_path.empty()) {
                filepath = loaded_pgn_path;
            }
            
            load_pgn(filepath);
        }
        
        ImGui::SameLine();
        
        // Reset button
        if (ImGui::Button("Reset")) {
            game->reset();
        }
    }
    
    // Render playback controls
    void render_controls() {
        if (!game) return;
        
        ImGui::TextColored(colors[0], "Playback Controls:");
        
        // Calculate current and total moves
        size_t current_move = game->get_current_move_index();
        size_t total_moves = game->get_move_count();
        
        // Display move counter
        ImGui::Text("Move: %zu / %zu", current_move, total_moves);
        
        // Navigation buttons
        if (ImGui::Button("<<")) {
            // First move
            game->first_move();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("<")) {
            // Previous move
            game->previous_move();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button(">")) {
            // Next move
            game->next_move();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button(">>")) {
            // Last move
            game->last_move();
        }
        
        // Autoplay controls
        ImGui::Checkbox("Autoplay", &autoplay);
        
        if (autoplay) {
            // Autoplay speed slider
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::SliderFloat("##Speed", &autoplay_speed, 0.5f, 3.0f, "%.1fx");
            
            // Handle autoplay logic - move to the next position after the delay
            static std::chrono::steady_clock::time_point last_move_time = std::chrono::steady_clock::now();
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_move_time).count();
            
            // Calculate delay based on speed (base delay of 2000ms divided by speed)
            int delay_ms = static_cast<int>(2000.0f / autoplay_speed);
            
            if (elapsed > delay_ms && current_move < total_moves) {
                game->next_move();
                last_move_time = current_time;
            } else if (current_move >= total_moves) {
                // Stop autoplay when we reach the end
                autoplay = false;
            }
        }
    }
    
    // Render the move list
    void render_move_list() {
        if (!game) return;
        
        ImGui::TextColored(colors[0], "Move List:");
        
        // Get all moves from the game
        const auto& moves = game->get_moves();
        if (moves.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No moves");
            return;
        }
        
        // Calculate current move
        size_t current_move_index = game->get_current_move_index();
        
        // Show moves in a scrollable region
        if (ImGui::BeginChild("MovesList", ImVec2(0, 200), true)) {
            for (size_t i = 0; i < moves.size(); ++i) {
                // Display move number for white moves (every other move)
                if (i % 2 == 0) {
                    ImGui::Text("%zu.", (i / 2) + 1);
                    ImGui::SameLine();
                }
                
                // Determine if this is the current move
                bool is_current_move = (i == current_move_index - 1);
                
                // Highlight the current move
                if (is_current_move) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[7]));
                }
                
                // Display the move in algebraic notation
                const auto& move = moves[i];
                
                // Format with indicators for check/checkmate
                std::string move_text = move.algebraic;
                if (move.is_check) move_text += "+";
                if (move.is_checkmate) move_text += "#";
                
                // Make the move clickable
                if (ImGui::Selectable(move_text.c_str(), is_current_move)) {
                    // When clicked, move to this position
                    game->set_position(i + 1);
                }
                
                // End highlighting
                if (is_current_move) {
                    ImGui::PopStyleColor();
                }
                
                // For white moves, stay on the same line for the black move to follow
                if (i % 2 == 0 && i < moves.size() - 1) {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::EndChild();
    }

    // Helper method to extract player information from PGN string
    void extract_player_info_from_pgn(const std::string& pgn, ChessGameArchive& archive_game) {
        // Try to find White and Black player tags in the PGN
        auto extract_tag_value = [](const std::string& pgn, const std::string& tag) -> std::string {
            std::string search_pattern = "[" + tag + " \"";
            size_t start_pos = pgn.find(search_pattern);
            if (start_pos != std::string::npos) {
                start_pos += search_pattern.length();
                size_t end_pos = pgn.find("\"]", start_pos);
                if (end_pos != std::string::npos) {
                    return pgn.substr(start_pos, end_pos - start_pos);
                }
            }
            return "";
        };
        
        // Extract player names
        archive_game.white_username = extract_tag_value(pgn, "White");
        archive_game.black_username = extract_tag_value(pgn, "Black");
        
        // Extract result if not already set
        if (archive_game.result.empty()) {
            archive_game.result = extract_tag_value(pgn, "Result");
        }
        
        // If we couldn't extract player names, use placeholders
        if (archive_game.white_username.empty()) archive_game.white_username = "White";
        if (archive_game.black_username.empty()) archive_game.black_username = "Black";
        if (archive_game.result.empty()) archive_game.result = "*";
    }
    
private:
    std::unique_ptr<models::chess::Game> game;
    std::string loaded_pgn_path;
    bool autoplay = false;
    float autoplay_speed = 1.0f;
    
    // SDL Renderer and textures
    SDL_Renderer* renderer = nullptr;
    std::unordered_map<models::chess::Piece, SDL_Texture*> piece_textures;
    std::unordered_map<models::chess::Piece, std::pair<int, int>> piece_dimensions;
    bool textures_loaded = false;
    
    // Chess.com API related members
    char username_buffer[64] = {};
    std::vector<std::string> player_archives;
    std::vector<ChessGameArchive> archive_games;
    std::string selected_archive;
    int selected_game_index = -1;
    bool is_fetching_archives = false;
    bool is_fetching_games = false;
    std::string api_error;
    bool auto_search_on_render = false;
    bool from_chess_com = false;
    std::string chess_com_username;
    
    // Async futures for API requests
    std::future<std::pair<bool, std::string>> archives_future;
    std::future<std::pair<bool, std::string>> games_future;
};

} // namespace rouen::cards