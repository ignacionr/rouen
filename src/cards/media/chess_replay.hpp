#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <array>
#include <unordered_map>
#include <filesystem>

#include "imgui.h"
#include <SDL.h>
#include <SDL_image.h>

#include "../interface/card.hpp"
#include "../../models/chess/chess.hpp"
#include "../../helpers/debug.hpp"  // Include debug system for better logging
#include "../../helpers/texture_helper.hpp"
#include "../../registrar.hpp"
#include "../../fonts.hpp"  // For font utilities

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
};

} // namespace rouen::cards