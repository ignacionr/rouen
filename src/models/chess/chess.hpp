#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>
#include <format>
#include <algorithm>
#include <array>
#include <sstream>
#include <fstream>

#include "../helpers/debug.hpp"

namespace rouen::models::chess {

// Represents a chess piece
enum class Piece {
    None,
    WhitePawn, WhiteKnight, WhiteBishop, WhiteRook, WhiteQueen, WhiteKing,
    BlackPawn, BlackKnight, BlackBishop, BlackRook, BlackQueen, BlackKing
};

// Represents a position on the chess board
struct Position {
    int file; // a-h (0-7)
    int rank; // 1-8 (0-7)
    
    // Create position from algebraic notation (e.g., "e4")
    static Position from_algebraic(const std::string& algebraic) {
        if (algebraic.length() < 2) return {-1, -1}; // Invalid
        
        int file = algebraic[0] - 'a';
        int rank = algebraic[1] - '1';
        
        return {file, rank};
    }
    
    // Convert position to algebraic notation
    std::string to_algebraic() const {
        if (file < 0 || file > 7 || rank < 0 || rank > 7) {
            return "??";
        }
        
        char file_char = static_cast<char>('a' + file);
        char rank_char = static_cast<char>('1' + rank);
        
        return std::string{file_char, rank_char};
    }
    
    bool is_valid() const {
        return file >= 0 && file <= 7 && rank >= 0 && rank <= 7;
    }
    
    bool operator==(const Position& other) const {
        return file == other.file && rank == other.rank;
    }
};

// Represents a chess move
struct Move {
    Position from;
    Position to;
    Piece promotion = Piece::None;
    bool is_castle = false;
    bool is_capture = false;
    bool is_check = false;
    bool is_checkmate = false;
    std::string algebraic; // The move in algebraic notation (e.g., "e4", "Nf3")
    
    // Constructor for standard moves
    Move(Position from, Position to, bool capture = false, bool check = false, bool checkmate = false)
        : from(from), to(to), is_capture(capture), is_check(check), is_checkmate(checkmate) {}
    
    // Constructor for moves from algebraic notation
    Move(const std::string& alg) : algebraic(alg) {}
    
    // Default constructor
    Move() : from{-1, -1}, to{-1, -1} {}
};

// Represents a chess board
class Board {
public:
    Board() {
        reset_to_starting_position();
    }
    
    // Reset the board to the standard starting position
    void reset_to_starting_position() {
        // Clear the board
        pieces.fill(Piece::None);
        
        // Set up white pieces
        pieces[position_to_index({0, 0})] = Piece::WhiteRook;
        pieces[position_to_index({1, 0})] = Piece::WhiteKnight;
        pieces[position_to_index({2, 0})] = Piece::WhiteBishop;
        pieces[position_to_index({3, 0})] = Piece::WhiteQueen;
        pieces[position_to_index({4, 0})] = Piece::WhiteKing;
        pieces[position_to_index({5, 0})] = Piece::WhiteBishop;
        pieces[position_to_index({6, 0})] = Piece::WhiteKnight;
        pieces[position_to_index({7, 0})] = Piece::WhiteRook;
        
        // Set up white pawns
        for (int file = 0; file < 8; ++file) {
            pieces[position_to_index({file, 1})] = Piece::WhitePawn;
        }
        
        // Set up black pieces
        pieces[position_to_index({0, 7})] = Piece::BlackRook;
        pieces[position_to_index({1, 7})] = Piece::BlackKnight;
        pieces[position_to_index({2, 7})] = Piece::BlackBishop;
        pieces[position_to_index({3, 7})] = Piece::BlackQueen;
        pieces[position_to_index({4, 7})] = Piece::BlackKing;
        pieces[position_to_index({5, 7})] = Piece::BlackBishop;
        pieces[position_to_index({6, 7})] = Piece::BlackKnight;
        pieces[position_to_index({7, 7})] = Piece::BlackRook;
        
        // Set up black pawns
        for (int file = 0; file < 8; ++file) {
            pieces[position_to_index({file, 6})] = Piece::BlackPawn;
        }
        
        CHESS_INFO("Board reset to starting position");
    }
    
    // Get the piece at a given position
    Piece get_piece(Position pos) const {
        if (!pos.is_valid()) return Piece::None;
        return pieces[position_to_index(pos)];
    }
    
    // Apply a move to the board
    void apply_move(const Move& move) {
        if (!move.from.is_valid() || !move.to.is_valid()) {
            CHESS_WARN_FMT("Invalid move: from={} to={}", 
                         move.from.to_algebraic(), move.to.to_algebraic());
            return;
        }
        
        Piece piece = get_piece(move.from);
        
        // Handle captures
        if (move.is_capture) {
            CHESS_INFO_FMT("Capture at {}", move.to.to_algebraic());
        }
        
        // Move the piece
        pieces[position_to_index(move.to)] = piece;
        pieces[position_to_index(move.from)] = Piece::None;
        
        // Handle promotion
        if (move.promotion != Piece::None) {
            pieces[position_to_index(move.to)] = move.promotion;
            CHESS_INFO_FMT("Promotion at {} to {}", 
                         move.to.to_algebraic(), 
                         get_piece_name(move.promotion));
        }
        
        CHESS_INFO_FMT("Move applied: {} from {} to {}", 
                     get_piece_name(piece),
                     move.from.to_algebraic(), 
                     move.to.to_algebraic());
    }
    
    // Clear the board
    void clear() {
        pieces.fill(Piece::None);
        CHESS_INFO("Board cleared");
    }
    
    // Helper to get a string representation of a piece
    static std::string get_piece_name(Piece piece) {
        switch (piece) {
            case Piece::None: return "Empty";
            case Piece::WhitePawn: return "White Pawn";
            case Piece::WhiteKnight: return "White Knight";
            case Piece::WhiteBishop: return "White Bishop";
            case Piece::WhiteRook: return "White Rook";
            case Piece::WhiteQueen: return "White Queen";
            case Piece::WhiteKing: return "White King";
            case Piece::BlackPawn: return "Black Pawn";
            case Piece::BlackKnight: return "Black Knight";
            case Piece::BlackBishop: return "Black Bishop";
            case Piece::BlackRook: return "Black Rook";
            case Piece::BlackQueen: return "Black Queen";
            case Piece::BlackKing: return "Black King";
            default: return "Unknown";
        }
    }
    
    // Get the Unicode character for a piece
    static const char* get_piece_unicode(Piece piece) {
        // Using a static map to store the Unicode characters
        static const std::map<Piece, std::string> unicode_pieces = {
            {Piece::None, " "},
            {Piece::WhiteKing, "\xE2\x99\x94"},   // WHITE CHESS KING ♔ U+2654
            {Piece::WhiteQueen, "\xE2\x99\x95"},  // WHITE CHESS QUEEN ♕ U+2655
            {Piece::WhiteRook, "\xE2\x99\x96"},   // WHITE CHESS ROOK ♖ U+2656
            {Piece::WhiteBishop, "\xE2\x99\x97"}, // WHITE CHESS BISHOP ♗ U+2657
            {Piece::WhiteKnight, "\xE2\x99\x98"}, // WHITE CHESS KNIGHT ♘ U+2658
            {Piece::WhitePawn, "\xE2\x99\x99"},   // WHITE CHESS PAWN ♙ U+2659
            {Piece::BlackKing, "\xE2\x99\x9A"},   // BLACK CHESS KING ♚ U+265A
            {Piece::BlackQueen, "\xE2\x99\x9B"},  // BLACK CHESS QUEEN ♛ U+265B
            {Piece::BlackRook, "\xE2\x99\x9C"},   // BLACK CHESS ROOK ♜ U+265C
            {Piece::BlackBishop, "\xE2\x99\x9D"}, // BLACK CHESS BISHOP ♝ U+265D
            {Piece::BlackKnight, "\xE2\x99\x9E"}, // BLACK CHESS KNIGHT ♞ U+265E
            {Piece::BlackPawn, "\xE2\x99\x9F"}    // BLACK CHESS PAWN ♟ U+265F
        };
        
        // Get the string from the map, with a fallback for unknown pieces
        auto it = unicode_pieces.find(piece);
        if (it != unicode_pieces.end()) {
            return it->second.c_str();
        }
        
        return "?";
    }
    
    // Check if a piece is white
    static bool is_white(Piece piece) {
        return piece == Piece::WhitePawn || piece == Piece::WhiteKnight ||
               piece == Piece::WhiteBishop || piece == Piece::WhiteRook ||
               piece == Piece::WhiteQueen || piece == Piece::WhiteKing;
    }
    
    // Check if a piece is black
    static bool is_black(Piece piece) {
        return piece == Piece::BlackPawn || piece == Piece::BlackKnight ||
               piece == Piece::BlackBishop || piece == Piece::BlackRook ||
               piece == Piece::BlackQueen || piece == Piece::BlackKing;
    }
    
    // Get a reference to the internal board array
    const std::array<Piece, 64>& get_board() const {
        return pieces;
    }
    
private:
    std::array<Piece, 64> pieces;
    
    // Convert position to array index
    static std::size_t position_to_index(Position pos) {
        return static_cast<std::size_t>(pos.rank) * 8 + static_cast<std::size_t>(pos.file);
    }
};

// Represents a chess game with moves
class Game {
public:
    Game() : current_move_index(0) {
        reset();
    }
    
    // Reset the game to the starting position
    void reset() {
        board.reset_to_starting_position();
        moves.clear();
        current_move_index = 0;
        white_player = "White";
        black_player = "Black";
        event = "Chess Game";
        site = "";
        date = "";
        round = "";
        result = "*";
        CHESS_INFO("Game reset");
    }
    
    // Add a move to the game
    void add_move(const Move& move) {
        moves.push_back(move);
        CHESS_INFO_FMT("Move added: {}", move.algebraic);
    }
    
    // Batch add moves to the game
    void add_moves(const std::vector<Move>& new_moves) {
        moves.insert(moves.end(), new_moves.begin(), new_moves.end());
        CHESS_INFO_FMT("Added {} moves to the game", new_moves.size());
    }
    
    // Get all moves in the game
    const std::vector<Move>& get_moves() const {
        return moves;
    }
    
    // Get total number of moves
    size_t get_move_count() const {
        return moves.size();
    }
    
    // Get the current position of the board
    Board get_current_position() const {
        return board;
    }
    
    // Set the current move index and update the board accordingly
    void set_position(size_t move_index) {
        if (move_index > moves.size()) {
            CHESS_WARN_FMT("Invalid move index: {}. Total moves: {}", 
                         move_index, moves.size());
            return;
        }
        
        // Reset the board and replay moves up to the specified index
        board.reset_to_starting_position();
        
        for (size_t i = 0; i < move_index; ++i) {
            board.apply_move(moves[i]);
        }
        
        current_move_index = move_index;
        CHESS_INFO_FMT("Position set to move {}/{}", move_index, moves.size());
    }
    
    // Move to the next position
    bool next_move() {
        if (current_move_index >= moves.size()) {
            CHESS_INFO("Already at the last move");
            return false;
        }
        
        board.apply_move(moves[current_move_index]);
        current_move_index++;
        CHESS_INFO_FMT("Advanced to move {}/{}", current_move_index, moves.size());
        return true;
    }
    
    // Move to the previous position
    bool previous_move() {
        if (current_move_index == 0) {
            CHESS_INFO("Already at the starting position");
            return false;
        }
        
        current_move_index--;
        set_position(current_move_index);
        CHESS_INFO_FMT("Went back to move {}/{}", current_move_index, moves.size());
        return true;
    }
    
    // Jump to the first move
    void first_move() {
        set_position(0);
    }
    
    // Jump to the last move
    void last_move() {
        set_position(moves.size());
    }
    
    // Get current move index
    size_t get_current_move_index() const {
        return current_move_index;
    }
    
    // Load a game from PGN (Portable Game Notation) string
    bool load_from_pgn(const std::string& pgn) {
        reset();
        
        std::istringstream pgn_stream(pgn);
        std::string line;
        
        // Parse header information
        bool in_header = true;
        while (std::getline(pgn_stream, line)) {
            // Skip empty lines
            if (line.empty()) continue;
            
            // Check for header tags
            if (in_header && line[0] == '[') {
                parse_pgn_tag(line);
            } else {
                in_header = false;
                
                // Parse moves
                std::vector<Move> parsed_moves = parse_pgn_moves(line);
                add_moves(parsed_moves);
            }
        }
        
        // Reset position to the start
        set_position(0);
        
        CHESS_INFO_FMT("Loaded game from PGN with {} moves", moves.size());
        return !moves.empty();
    }
    
    // Load a game from a PGN file
    bool load_from_pgn_file(const std::string& filepath) {
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                CHESS_ERROR_FMT("Could not open PGN file: {}", filepath);
                return false;
            }
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();
            
            return load_from_pgn(buffer.str());
        } catch (const std::exception& e) {
            CHESS_ERROR_FMT("Error loading PGN file: {}", e.what());
            return false;
        }
    }
    
    // Get game metadata as a formatted string
    std::string get_game_info() const {
        std::stringstream info;
        
        info << "Event: " << event << "\n";
        if (!site.empty()) info << "Site: " << site << "\n";
        if (!date.empty()) info << "Date: " << date << "\n";
        if (!round.empty()) info << "Round: " << round << "\n";
        info << "White: " << white_player << "\n";
        info << "Black: " << black_player << "\n";
        info << "Result: " << result << "\n";
        
        return info.str();
    }
    
    // Properties
    std::string event;
    std::string site;
    std::string date;
    std::string round;
    std::string white_player;
    std::string black_player;
    std::string result;
    
private:
    Board board;
    std::vector<Move> moves;
    size_t current_move_index;
    
    // Parse a PGN tag (e.g., [Event "World Championship"])
    void parse_pgn_tag(const std::string& tag_line) {
        // Extract tag name and value
        size_t name_start = tag_line.find('[') + 1;
        size_t name_end = tag_line.find(' ', name_start);
        
        size_t value_start = tag_line.find('"', name_end) + 1;
        size_t value_end = tag_line.find('"', value_start);
        
        if (name_start == std::string::npos || name_end == std::string::npos ||
            value_start == std::string::npos || value_end == std::string::npos) {
            CHESS_WARN_FMT("Invalid PGN tag format: {}", tag_line);
            return;
        }
        
        std::string name = tag_line.substr(name_start, name_end - name_start);
        std::string value = tag_line.substr(value_start, value_end - value_start);
        
        // Set game metadata based on tag name
        if (name == "Event") {
            event = value;
        } else if (name == "Site") {
            site = value;
        } else if (name == "Date") {
            date = value;
        } else if (name == "Round") {
            round = value;
        } else if (name == "White") {
            white_player = value;
        } else if (name == "Black") {
            black_player = value;
        } else if (name == "Result") {
            result = value;
        }
        
        CHESS_DEBUG_FMT("Parsed PGN tag: [{}] = '{}'", name, value);
    }
    
    // Parse PGN move text and convert to Move objects
    std::vector<Move> parse_pgn_moves(const std::string& move_text) {
        std::vector<Move> parsed_moves;
        
        std::istringstream stream(move_text);
        std::string token;
        
        while (stream >> token) {
            // Skip move numbers (e.g., "1.", "2.", etc.)
            if (token.back() == '.' || token == "1/2-1/2" || token == "1-0" || token == "0-1" || token == "*") {
                continue;
            }
            
            // Check for check/checkmate indicators
            bool is_check = token.back() == '+';
            bool is_checkmate = token.back() == '#';
            
            if (is_check || is_checkmate) {
                token.pop_back(); // Remove the indicator
            }
            
            // Create a simple move object with the algebraic notation
            Move move;
            move.algebraic = token;
            move.is_check = is_check;
            move.is_checkmate = is_checkmate;
            
            parsed_moves.push_back(move);
            CHESS_DEBUG_FMT("Parsed move: {}{}{}", 
                         token, 
                         is_check ? "+" : "", 
                         is_checkmate ? "#" : "");
        }
        
        return parsed_moves;
    }
};

} // namespace rouen::models::chess