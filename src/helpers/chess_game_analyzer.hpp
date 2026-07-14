#pragma once

#include <string>
#include <format>
#include <future>
#include <memory>
#include <optional>

#include "glaze_include.hpp"

#include "cppgpt.hpp"
#include "fetch.hpp"
#include "api_keys.hpp"
#include "llm_config.hpp"
#include "../registrar.hpp"
#include "../models/chess/chess.hpp"

namespace rouen::helpers {

    // Structure to hold chess analysis results
    struct ChessAnalysisResult {
        std::string analysis_type;
        std::string content;
        bool success = false;
        std::string error_message;
    };

    class ChessGameAnalyzer {
    public:
        ChessGameAnalyzer() = default;

        // Generate full game commentary
        std::future<ChessAnalysisResult> generate_commentary_async(const models::chess::Game& game) {
            return std::async(std::launch::async, [this, &game]() {
                return generate_commentary(game);
            });
        }

        // Generate game summary with insight
        std::future<ChessAnalysisResult> generate_summary_async(const models::chess::Game& game) {
            return std::async(std::launch::async, [this, &game]() {
                return generate_summary(game);
            });
        }

        // Generate improvement suggestions for a specific player
        std::future<ChessAnalysisResult> generate_improvement_async(const models::chess::Game& game, const std::string& player_name) {
            return std::async(std::launch::async, [this, &game, player_name]() {
                return generate_improvement(game, player_name);
            });
        }

    private:
        http::fetch fetcher_{150};

        // Convert game data to JSON for AI processing
        std::string game_to_json(const models::chess::Game& game) const {
            try {
                // Create a structured representation of the game
                std::string json_data = "{\n";
                json_data += std::format("  \"event\": \"{}\",\n", game.event);
                json_data += std::format("  \"white_player\": \"{}\",\n", game.white_player);
                json_data += std::format("  \"black_player\": \"{}\",\n", game.black_player);
                json_data += std::format("  \"date\": \"{}\",\n", game.date);
                json_data += std::format("  \"result\": \"{}\",\n", game.result);
                json_data += std::format("  \"total_moves\": {},\n", game.get_move_count());
                
                // Add moves
                json_data += "  \"moves\": [\n";
                const auto& moves = game.get_moves();
                for (size_t i = 0; i < moves.size(); ++i) {
                    const auto& move = moves[i];
                    json_data += "    {\n";
                    json_data += std::format("      \"move_number\": {},\n", (i / 2) + 1);
                    json_data += std::format("      \"color\": \"{}\",\n", (i % 2 == 0) ? "white" : "black");
                    json_data += std::format("      \"algebraic\": \"{}\",\n", move.algebraic);
                    json_data += std::format("      \"is_check\": {},\n", move.is_check ? "true" : "false");
                    json_data += std::format("      \"is_checkmate\": {},\n", move.is_checkmate ? "true" : "false");
                    json_data += std::format("      \"is_capture\": {}\n", move.is_capture ? "true" : "false");
                    json_data += "    }";
                    if (i < moves.size() - 1) json_data += ",";
                    json_data += "\n";
                }
                json_data += "  ]\n";
                json_data += "}";
                
                return json_data;
            } catch (const std::exception& e) {
                return std::format("{{\"error\": \"Failed to serialize game: {}\"}}", e.what());
            }
        }

        // Generate full game commentary
        ChessAnalysisResult generate_commentary(const models::chess::Game& game) {
            ChessAnalysisResult result;
            result.analysis_type = "commentary";

            try {
                if (!LLMConfig::is_configured()) {
                    auto settings = LLMConfig::get_current_config();
                    std::string env_name = LLMConfig::get_api_key_env_name(settings.provider);
                    result.error_message = std::format("LLM not configured ({} is missing)", env_name);
                    return result;
                }

                auto llm_instance = LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    result.error_message = "Failed to create LLM instance";
                    return result;
                }

                auto settings = LLMConfig::get_current_config();

                // Add system instructions for game commentary
                llm_instance->add_instructions(
                    "You are a chess expert and commentator. Your task is to provide detailed, engaging commentary "
                    "on the chess game provided. Analyze the moves, strategies, tactics, and key moments. "
                    "Discuss opening principles, middlegame plans, endgame techniques, and any critical blunders or brilliant moves. "
                    "Write in an educational and entertaining style suitable for chess enthusiasts. "
                    "Structure your commentary with move-by-move analysis for key positions and overall strategic themes. "
                    "Respond with plain text, no JSON formatting."
                );

                // Prepare the game data
                std::string game_json = game_to_json(game);
                std::string prompt = std::format(
                    "Please provide comprehensive commentary for this chess game:\n\n{}", 
                    game_json
                );

                // Send request to AI
                auto response = llm_instance->sendMessage(prompt, 
                    [this](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher_.post(url, data, header_client);
                    }, 
                    "user", 
                    settings.model_name
                );

                result.content = response.choices[0].message.content;
                result.success = true;

            } catch (const std::exception& e) {
                result.error_message = std::format("Failed to generate commentary: {}", e.what());
            }

            return result;
        }

        // Generate game summary with insight
        ChessAnalysisResult generate_summary(const models::chess::Game& game) {
            ChessAnalysisResult result;
            result.analysis_type = "summary";

            try {
                if (!LLMConfig::is_configured()) {
                    auto settings = LLMConfig::get_current_config();
                    std::string env_name = LLMConfig::get_api_key_env_name(settings.provider);
                    result.error_message = std::format("LLM not configured ({} is missing)", env_name);
                    return result;
                }

                auto llm_instance = LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    result.error_message = "Failed to create LLM instance";
                    return result;
                }

                auto settings = LLMConfig::get_current_config();

                // Add system instructions for game summary
                llm_instance->add_instructions(
                    "You are a chess analyst. Your task is to provide a concise summary of the chess game "
                    "along with ONE key strategic insight. The summary should be 2-3 paragraphs maximum, "
                    "covering the opening, critical moments, and outcome. Then provide one important insight "
                    "about chess strategy, tactics, or principles that can be learned from this game. "
                    "Be specific and educational. Respond with plain text, no JSON formatting."
                );

                // Prepare the game data
                std::string game_json = game_to_json(game);
                std::string prompt = std::format(
                    "Please provide a summary and one key insight for this chess game:\n\n{}", 
                    game_json
                );

                // Send request to AI
                auto response = llm_instance->sendMessage(prompt, 
                    [this](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher_.post(url, data, header_client);
                    }, 
                    "user", 
                    settings.model_name
                );

                result.content = response.choices[0].message.content;
                result.success = true;

            } catch (const std::exception& e) {
                result.error_message = std::format("Failed to generate summary: {}", e.what());
            }

            return result;
        }

        // Generate improvement suggestions for a specific player
        ChessAnalysisResult generate_improvement(const models::chess::Game& game, const std::string& player_name) {
            ChessAnalysisResult result;
            result.analysis_type = "improvement";

            try {
                if (!LLMConfig::is_configured()) {
                    auto settings = LLMConfig::get_current_config();
                    std::string env_name = LLMConfig::get_api_key_env_name(settings.provider);
                    result.error_message = std::format("LLM not configured ({} is missing)", env_name);
                    return result;
                }

                auto llm_instance = LLMConfig::create_llm_instance();
                if (!llm_instance) {
                    result.error_message = "Failed to create LLM instance";
                    return result;
                }

                auto settings = LLMConfig::get_current_config();

                // Add system instructions for improvement suggestions
                llm_instance->add_instructions(
                    "You are a chess coach. Your task is to analyze the specified player's performance "
                    "in the given chess game and suggest ONE specific area for improvement. "
                    "Focus on concrete, actionable advice such as: opening preparation, tactical awareness, "
                    "positional understanding, time management, endgame technique, or calculation skills. "
                    "Provide specific examples from the game and practical advice for improvement. "
                    "Keep it focused on one main area. Respond with plain text, no JSON formatting."
                );

                // Determine if the player is white or black
                std::string player_color = "unknown";
                if (game.white_player == player_name) {
                    player_color = "white";
                } else if (game.black_player == player_name) {
                    player_color = "black";
                }

                // Prepare the game data
                std::string game_json = game_to_json(game);
                std::string prompt = std::format(
                    "Please analyze the performance of {} (playing as {}) in this chess game and suggest ONE specific area for improvement:\n\n{}", 
                    player_name, player_color, game_json
                );

                // Send request to AI
                auto response = llm_instance->sendMessage(prompt, 
                    [this](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher_.post(url, data, header_client);
                    }, 
                    "user", 
                    settings.model_name
                );

                result.content = response.choices[0].message.content;
                result.success = true;

            } catch (const std::exception& e) {
                result.error_message = std::format("Failed to generate improvement suggestions: {}", e.what());
            }

            return result;
        }
    };

} // namespace rouen::helpers
