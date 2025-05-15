#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <chrono>
#include <optional>
#include <utility>
#include <glaze/json.hpp>

#include "debug.hpp"
#include "fetch.hpp"

// Chess API debug macros
#define CHESS_API_ERROR(message) LOG_COMPONENT("CHESS_API", LOG_LEVEL_ERROR, message)
#define CHESS_API_ERROR_FMT(fmt, ...) CHESS_API_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_API_WARN(message) LOG_COMPONENT("CHESS_API", LOG_LEVEL_WARN, message)
#define CHESS_API_WARN_FMT(fmt, ...) CHESS_API_WARN(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_API_INFO(message) LOG_COMPONENT("CHESS_API", LOG_LEVEL_INFO, message)
#define CHESS_API_INFO_FMT(fmt, ...) CHESS_API_INFO(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_API_DEBUG(message) LOG_COMPONENT("CHESS_API", LOG_LEVEL_DEBUG, message)
#define CHESS_API_DEBUG_FMT(fmt, ...) CHESS_API_DEBUG(debug::format_log(fmt, __VA_ARGS__))

namespace rouen {
namespace chess {

// ---- JSON Structures for Chess.com API ----

// JSON structure for Chess.com API game
struct ChessComGame {
    std::string url;
    std::string pgn;
    std::string time_class;
    int64_t end_time;
    
    struct Players {
        struct Player {
            std::string username;
            int result; // 'win', 'lose', 'draw' encoded as integers
            std::string rating;
        };
        
        Player white;
        Player black;
    };
    
    Players players;
    std::string result; // The game result string (e.g., "1-0")
};

// Simplified game archive structure for internal use
struct ChessGameArchive {
    std::string url;
    std::string pgn;
    std::string time_class;
    int64_t end_time;
    std::string white_username;
    std::string black_username;
    std::string result;
};

// JSON structure for Chess.com API archives response
struct ChessComArchives {
    std::vector<std::string> archives;
};

// JSON structure for Chess.com API games response
struct ChessComGamesResponse {
    std::vector<ChessComGame> games;
};

// ---- Main Chess.com API Client Class ----

class ChessComApiClient {
public:
    ChessComApiClient() = default;
    ~ChessComApiClient() = default;
    
    // Fetch a player's game archives (available months)
    std::future<std::pair<bool, std::string>> fetch_player_archives(const std::string& username) {
        if (username.empty()) {
            return std::async(std::launch::async, []() {
                return std::make_pair(false, std::string("Username cannot be empty"));
            });
        }
        
        return std::async(std::launch::async, [username]() {
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
    
    // Fetch games from a specific archive URL
    std::future<std::pair<bool, std::string>> fetch_archive_games(const std::string& archive_url) {
        return std::async(std::launch::async, [archive_url]() {
            try {
                http::fetch fetcher;
                std::string response = fetcher(archive_url);
                
                return std::make_pair(true, response);
            } catch (const std::exception& e) {
                return std::make_pair(false, std::string(e.what()));
            }
        });
    }
    
    // Process archive response and convert to vector of archive URLs
    bool process_archives_response(const std::string& response, std::vector<std::string>& archives) {
        try {
            ChessComArchives archives_obj;
            auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(archives_obj, response);
            if (read_error) {
                CHESS_API_ERROR_FMT("Error parsing archives response: {}", glz::format_error(read_error));
                return false;
            }
            
            archives = archives_obj.archives;
            return true;
        } catch (const std::exception& e) {
            CHESS_API_ERROR_FMT("Exception parsing archives response: {}", e.what());
            return false;
        }
    }
    
    // Process games response and convert to vector of ChessGameArchive
    bool process_games_response(const std::string& response, std::vector<ChessGameArchive>& games) {
        try {
            ChessComGamesResponse games_response;
            auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(games_response, response);
            if (read_error) {
                CHESS_API_ERROR_FMT("Error parsing games response: {}", glz::format_error(read_error));
                return false;
            }
            
            games.clear();
            for (const auto& game_item : games_response.games) {
                ChessGameArchive archive_game;
                archive_game.url = game_item.url;
                archive_game.pgn = game_item.pgn;
                archive_game.time_class = game_item.time_class;
                archive_game.end_time = game_item.end_time;
                
                // Extract player information
                if (!game_item.players.white.username.empty() && !game_item.players.black.username.empty()) {
                    archive_game.white_username = game_item.players.white.username;
                    archive_game.black_username = game_item.players.black.username;
                } else {
                    // Fallback: try to extract from PGN if structured data isn't available
                    extract_player_info_from_pgn(game_item.pgn, archive_game);
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
                
                games.push_back(archive_game);
            }
            
            return true;
        } catch (const std::exception& e) {
            CHESS_API_ERROR_FMT("Exception parsing games response: {}", e.what());
            return false;
        }
    }
    
    // Extract player information from PGN when not available in the structured data
    void extract_player_info_from_pgn(const std::string& pgn, ChessGameArchive& game) {
        // Extract White player from PGN
        size_t white_pos = pgn.find("[White \"");
        if (white_pos != std::string::npos) {
            size_t start = white_pos + 8; // Length of '[White "'
            size_t end = pgn.find("\"]", start);
            if (end != std::string::npos) {
                game.white_username = pgn.substr(start, end - start);
            }
        }
        
        // Extract Black player from PGN
        size_t black_pos = pgn.find("[Black \"");
        if (black_pos != std::string::npos) {
            size_t start = black_pos + 8; // Length of '[Black "'
            size_t end = pgn.find("\"]", start);
            if (end != std::string::npos) {
                game.black_username = pgn.substr(start, end - start);
            }
        }
        
        // Extract Result from PGN
        size_t result_pos = pgn.find("[Result \"");
        if (result_pos != std::string::npos) {
            size_t start = result_pos + 9; // Length of '[Result "'
            size_t end = pgn.find("\"]", start);
            if (end != std::string::npos) {
                game.result = pgn.substr(start, end - start);
            }
        }
    }
    
    // Format a chess game for display
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
        
        CHESS_API_DEBUG_FMT("Fixed PGN from {} characters to {} characters", pgn.length(), fixed_pgn.length());
        return fixed_pgn;
    }
    
    // Properly clean and prepare a PGN from Chess.com for loading
    std::string prepare_pgn_for_loading(const std::string& pgn) {
        std::string clean_pgn;
        
        try {
            // Properly unescape the PGN string using the JSON library
            auto read_result = glz::read_json(clean_pgn, "\"" + pgn + "\"");
            if (!read_result) {
                CHESS_API_ERROR_FMT("Error unescaping PGN: {}", glz::format_error(read_result));
                // Fallback to direct PGN if Glaze unescaping fails
                clean_pgn = pgn;
            } else {
                CHESS_API_INFO("Successfully unescaped PGN JSON string");
            }
        } catch (const std::exception& e) {
            CHESS_API_ERROR_FMT("Error unescaping PGN: {}", e.what());
            
            // Fallback to direct PGN if Glaze unescaping fails
            clean_pgn = pgn;
        }
        
        // Ensure PGN starts with a valid tag
        if (!clean_pgn.empty() && clean_pgn.front() != '[') {
            CHESS_API_WARN("PGN doesn't start with a tag, adding Event tag");
            clean_pgn = "[Event \"Chess.com Game\"]\n" + clean_pgn;
        }
        
        // Fix common issues with Chess.com PGN format
        return fix_chess_com_pgn(clean_pgn);
    }
};

} // namespace chess
} // namespace rouen

// Define glaze schema for Chess.com API structures
template <>
struct glz::meta<rouen::chess::ChessComGame::Players::Player> {
    using T = rouen::chess::ChessComGame::Players::Player;
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
struct glz::meta<rouen::chess::ChessComGame::Players> {
    using T = rouen::chess::ChessComGame::Players;
    static constexpr auto value = glz::object(
        "white", &T::white,
        "black", &T::black
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::chess::ChessComGame> {
    using T = rouen::chess::ChessComGame;
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
struct glz::meta<rouen::chess::ChessComArchives> {
    using T = rouen::chess::ChessComArchives;
    static constexpr auto value = glz::object(
        "archives", &T::archives
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::chess::ChessComGamesResponse> {
    using T = rouen::chess::ChessComGamesResponse;
    static constexpr auto value = glz::object(
        "games", &T::games
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};
