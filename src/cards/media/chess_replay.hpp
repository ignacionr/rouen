#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <array>

#include "imgui.h"
#include "../interface/card.hpp"
#include "../../models/chess/chess.hpp"
#include "../../helpers/debug.hpp"  // Include debug system for better logging
#include "../../registrar.hpp"
#include "../../fonts.hpp"  // For font utilities

// Define chess symbols for direct use and better debugging
namespace rouen::chess_symbols {
    // Define chess piece symbols using Unicode code points (more reliable than UTF-8 byte sequences)
    // White pieces
    inline constexpr char32_t WHITE_KING_CODEPOINT = 0x2654;   // ♔ U+2654
    inline constexpr char32_t WHITE_QUEEN_CODEPOINT = 0x2655;  // ♕ U+2655
    inline constexpr char32_t WHITE_ROOK_CODEPOINT = 0x2656;   // ♖ U+2656
    inline constexpr char32_t WHITE_BISHOP_CODEPOINT = 0x2657; // ♗ U+2657
    inline constexpr char32_t WHITE_KNIGHT_CODEPOINT = 0x2658; // ♘ U+2658
    inline constexpr char32_t WHITE_PAWN_CODEPOINT = 0x2659;   // ♙ U+2659
    
    // Black pieces
    inline constexpr char32_t BLACK_KING_CODEPOINT = 0x265A;   // ♚ U+265A
    inline constexpr char32_t BLACK_QUEEN_CODEPOINT = 0x265B;  // ♛ U+265B
    inline constexpr char32_t BLACK_ROOK_CODEPOINT = 0x265C;   // ♜ U+265C
    inline constexpr char32_t BLACK_BISHOP_CODEPOINT = 0x265D; // ♝ U+265D
    inline constexpr char32_t BLACK_KNIGHT_CODEPOINT = 0x265E; // ♞ U+265E
    inline constexpr char32_t BLACK_PAWN_CODEPOINT = 0x265F;   // ♟ U+265F
    
    // UTF-8 encoded strings for display and debugging
    // Note: Using UTF-8 literal strings in a C++23 compatible way
    inline constexpr const char WHITE_KING[] = "\xE2\x99\x94";   // ♔ U+2654
    inline constexpr const char WHITE_QUEEN[] = "\xE2\x99\x95";  // ♕ U+2655
    inline constexpr const char WHITE_ROOK[] = "\xE2\x99\x96";   // ♖ U+2656
    inline constexpr const char WHITE_BISHOP[] = "\xE2\x99\x97"; // ♗ U+2657
    inline constexpr const char WHITE_KNIGHT[] = "\xE2\x99\x98"; // ♘ U+2658
    inline constexpr const char WHITE_PAWN[] = "\xE2\x99\x99";   // ♙ U+2659
    
    inline constexpr const char BLACK_KING[] = "\xE2\x99\x9A";   // ♚ U+265A
    inline constexpr const char BLACK_QUEEN[] = "\xE2\x99\x9B";  // ♛ U+265B
    inline constexpr const char BLACK_ROOK[] = "\xE2\x99\x9C";   // ♜ U+265C
    inline constexpr const char BLACK_BISHOP[] = "\xE2\x99\x9D"; // ♝ U+265D
    inline constexpr const char BLACK_KNIGHT[] = "\xE2\x99\x9E"; // ♞ U+265E
    inline constexpr const char BLACK_PAWN[] = "\xE2\x99\x9F";   // ♟ U+265F

    // Helper to get a unicode code point from the piece enum
    inline char32_t get_unicode_codepoint(rouen::models::chess::Piece piece) {
        using Piece = rouen::models::chess::Piece;
        
        switch (piece) {
            case Piece::WhiteKing: return WHITE_KING_CODEPOINT;
            case Piece::WhiteQueen: return WHITE_QUEEN_CODEPOINT;
            case Piece::WhiteRook: return WHITE_ROOK_CODEPOINT;
            case Piece::WhiteBishop: return WHITE_BISHOP_CODEPOINT;
            case Piece::WhiteKnight: return WHITE_KNIGHT_CODEPOINT;
            case Piece::WhitePawn: return WHITE_PAWN_CODEPOINT;
            case Piece::BlackKing: return BLACK_KING_CODEPOINT;
            case Piece::BlackQueen: return BLACK_QUEEN_CODEPOINT;
            case Piece::BlackRook: return BLACK_ROOK_CODEPOINT;
            case Piece::BlackBishop: return BLACK_BISHOP_CODEPOINT;
            case Piece::BlackKnight: return BLACK_KNIGHT_CODEPOINT;
            case Piece::BlackPawn: return BLACK_PAWN_CODEPOINT;
            default: return ' ';
        }
    }

    // Helper to get a unicode chess piece from the piece enum
    inline const char* get_unicode_piece(rouen::models::chess::Piece piece) {
        using Piece = rouen::models::chess::Piece;
        
        switch (piece) {
            case Piece::WhiteKing: return WHITE_KING;
            case Piece::WhiteQueen: return WHITE_QUEEN;
            case Piece::WhiteRook: return WHITE_ROOK;
            case Piece::WhiteBishop: return WHITE_BISHOP;
            case Piece::WhiteKnight: return WHITE_KNIGHT;
            case Piece::WhitePawn: return WHITE_PAWN;
            case Piece::BlackKing: return BLACK_KING;
            case Piece::BlackQueen: return BLACK_QUEEN;
            case Piece::BlackRook: return BLACK_ROOK;
            case Piece::BlackBishop: return BLACK_BISHOP;
            case Piece::BlackKnight: return BLACK_KNIGHT;
            case Piece::BlackPawn: return BLACK_PAWN;
            default: return " ";
        }
    }
    
    // Check if chess symbols are available in the font
    inline bool verify_chess_symbols() {
        static bool verified = false;
        static bool all_available = false;
        
        if (!verified) {
            // Check actual codepoints in the font
            all_available = true;
            
            // Array of all chess symbol codepoints
            std::array<char32_t, 12> codepoints = {
                WHITE_KING_CODEPOINT, WHITE_QUEEN_CODEPOINT, WHITE_ROOK_CODEPOINT, 
                WHITE_BISHOP_CODEPOINT, WHITE_KNIGHT_CODEPOINT, WHITE_PAWN_CODEPOINT,
                BLACK_KING_CODEPOINT, BLACK_QUEEN_CODEPOINT, BLACK_ROOK_CODEPOINT, 
                BLACK_BISHOP_CODEPOINT, BLACK_KNIGHT_CODEPOINT, BLACK_PAWN_CODEPOINT
            };
            
            // Corresponding UTF-8 strings for logging
            std::array<const char*, 12> symbols = {
                WHITE_KING, WHITE_QUEEN, WHITE_ROOK, WHITE_BISHOP, WHITE_KNIGHT, WHITE_PAWN,
                BLACK_KING, BLACK_QUEEN, BLACK_ROOK, BLACK_BISHOP, BLACK_KNIGHT, BLACK_PAWN
            };
            
            // Check each codepoint directly
            for (size_t i = 0; i < codepoints.size(); ++i) {
                bool is_available = fonts::is_glyph_available(static_cast<ImWchar>(codepoints[i]), fonts::FontType::ChessSymbols);
                CHESS_INFO_FMT("Chess symbol '{}' (U+{:04X}) available in dedicated font: {}", 
                               symbols[i], static_cast<uint32_t>(codepoints[i]), 
                               is_available ? "yes" : "no");
                
                if (!is_available) {
                    all_available = false;
                }
            }
            
            verified = true;
            
            if (!all_available) {
                CHESS_WARN("Some chess symbols are not available in the chess symbols font");
            } else {
                CHESS_INFO("All chess symbols are available in the chess symbols font");
            }
        }
        
        return all_available;
    }
}

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
        get_color(4, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // White pieces
        get_color(5, ImVec4(0.2f, 0.2f, 0.2f, 1.0f)); // Black pieces
        get_color(6, ImVec4(0.9f, 0.5f, 0.5f, 0.7f)); // Highlighted square
        get_color(7, ImVec4(0.3f, 0.7f, 0.3f, 0.7f)); // Last move indicator
        get_color(8, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Fallback piece color (for text-based fallback)
        
        name("Chess Replay");
        requested_fps = 30;  // Higher refresh rate for smoother animations
        width = 700.0f;      // Wider card to accommodate board and move list
        
        // Create chess game model
        game = std::make_unique<models::chess::Game>();
        
        // If a PGN path was provided, load it
        if (!pgn_path.empty()) {
            load_pgn(std::string(pgn_path));
        }
        
        // Verify that chess symbols are available
        has_chess_symbols = chess_symbols::verify_chess_symbols();
        
        CHESS_INFO("Chess replay card initialized");
    }
    
    ~chess_replay() override {
        CHESS_INFO("Chess replay card destroyed");
    }
    
    std::string get_uri() const override {
        return "chess";
    }
    
    bool render() override {
        return render_window([this]() {
            if (!game) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Chess game model not initialized");
                return;
            }
            
            // Calculate board size based on card width
            float board_size = std::min(width - 40.0f, 400.0f);
            float square_size = board_size / 8.0f;
            
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
    
private:
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
                    // Determine piece color
                    ImVec4 piece_color = models::chess::Board::is_white(piece) ? colors[4] : colors[5];
                    
                    // Get the Unicode code point for this chess piece
                    char32_t piece_codepoint = chess_symbols::get_unicode_codepoint(piece);
                    
                    // Calculate center position for the piece
                    const float text_center_x = square_pos.x + square_size * 0.5f;
                    const float text_center_y = square_pos.y + square_size * 0.5f;
                    
                    // Use a larger font size for chess pieces
                    float font_size = square_size * 0.85f;
                    
                    // Get the dedicated chess symbols font
                    ImFont* chess_font = fonts::get_font(fonts::FontType::ChessSymbols);
                    
                    // If we have a dedicated chess font, use it; otherwise fall back to default
                    if (chess_font != nullptr) {
                        // We'll create a temporary single character string for drawing
                        // since ImGui's AddText expects a UTF-8 string
                        char piece_utf8[8] = {0}; // UTF-8 can require up to 4 bytes per character + null terminator
                        
                        // Convert the codepoint to a UTF-8 string
                        if (piece_codepoint < 0x80) {
                            // 1-byte character (ASCII)
                            piece_utf8[0] = static_cast<char>(piece_codepoint);
                        } else if (piece_codepoint < 0x800) {
                            // 2-byte character
                            piece_utf8[0] = static_cast<char>(0xC0 | ((piece_codepoint >> 6) & 0x1F));
                            piece_utf8[1] = static_cast<char>(0x80 | (piece_codepoint & 0x3F));
                        } else if (piece_codepoint < 0x10000) {
                            // 3-byte character (our chess symbols are in this range - U+2654 to U+265F)
                            piece_utf8[0] = static_cast<char>(0xE0 | ((piece_codepoint >> 12) & 0x0F));
                            piece_utf8[1] = static_cast<char>(0x80 | ((piece_codepoint >> 6) & 0x3F));
                            piece_utf8[2] = static_cast<char>(0x80 | (piece_codepoint & 0x3F));
                        } else {
                            // 4-byte character
                            piece_utf8[0] = static_cast<char>(0xF0 | ((piece_codepoint >> 18) & 0x07));
                            piece_utf8[1] = static_cast<char>(0x80 | ((piece_codepoint >> 12) & 0x3F));
                            piece_utf8[2] = static_cast<char>(0x80 | ((piece_codepoint >> 6) & 0x3F));
                            piece_utf8[3] = static_cast<char>(0x80 | (piece_codepoint & 0x3F));
                        }
                        
                        // Calculate text dimensions for perfect centering
                        ImVec2 text_size = chess_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, piece_utf8);
                        
                        // Calculate position for perfectly centered display
                        ImVec2 text_pos(
                            text_center_x - (text_size.x * 0.5f),
                            text_center_y - (text_size.y * 0.5f)
                        );
                        
                        // Draw the Unicode chess piece with the dedicated chess font
                        draw_list->AddText(chess_font, font_size, text_pos, 
                                           ImGui::ColorConvertFloat4ToU32(piece_color), piece_utf8);
                        
                        // Debug: Log what we're drawing
                        // CHESS_DEBUG_FMT("Drawing piece at {},{}: codepoint U+{:04X}, utf8: {}", 
                        //                file, rank, static_cast<uint32_t>(piece_codepoint), piece_utf8);
                    } else {
                        // Fall back to the current font if the chess font isn't available
                        ImFont* default_font = ImGui::GetFont();
                        
                        // Use a fallback color to indicate we're using the default font
                        if (!has_chess_symbols) {
                            piece_color = colors[8]; // Fallback color
                        }
                        
                        // Get the UTF-8 string for this piece
                        const char* piece_char = chess_symbols::get_unicode_piece(piece);
                        
                        // Calculate text dimensions with default font
                        ImVec2 text_size = default_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, piece_char);
                        
                        // Calculate position for perfectly centered display
                        ImVec2 text_pos(
                            text_center_x - (text_size.x * 0.5f),
                            text_center_y - (text_size.y * 0.5f)
                        );
                        
                        // Draw with default font as fallback
                        draw_list->AddText(default_font, font_size, text_pos, 
                                           ImGui::ColorConvertFloat4ToU32(piece_color), piece_char);
                    }
                }
            }
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
    
private:
    std::unique_ptr<models::chess::Game> game;
    std::string loaded_pgn_path;
    bool autoplay = false;
    float autoplay_speed = 1.0f;
    bool has_chess_symbols = false;
};

} // namespace rouen::cards