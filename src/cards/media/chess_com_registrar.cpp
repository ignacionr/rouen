#include "../interface/factory.hpp"
#include "chess_replay.hpp"

// This file provides Chess.com card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers the Chess.com card with the factory
    struct chess_com_card_registrar {
        chess_com_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the Chess.com card entry
            dict["chess-com"] = [](std::string_view username, SDL_Renderer*) {
                auto card = std::make_shared<rouen::cards::chess_replay>();
                // If a username was provided, automatically search for that player's games
                if (!username.empty()) {
                    // Use the public method to set the username and trigger search
                    card->set_chess_com_username(std::string(username));
                }
                return card;
            };
        }
    };

    // Create a static instance to register the Chess.com card during initialization
    static chess_com_card_registrar registrar;
}