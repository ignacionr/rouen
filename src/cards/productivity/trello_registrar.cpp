#include "../interface/factory.hpp"
#include "cards/interface/card.hpp"
#include "sdl_compat.hpp"
#include "trello_card.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// This file provides Trello card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers all Trello cards with the factory
    struct trello_card_registrar {
        trello_card_registrar() noexcept {
            try {
                // Get the dictionary reference
                auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                    rouen::cards::factory::dictionary());
                
                // Register the main Trello card
                dict["trello"] = [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<rouen::cards::trello_card>();
                };
                
                // Register Trello board viewer with board ID support
                dict["trello-board"] = [](std::string_view board_id, SDL_Renderer*) -> card::ptr {
                    return std::make_shared<rouen::cards::trello_card>(std::string(board_id));
                };
                
                // Register Trello card viewer with card ID support
                dict["trello-card"] = [](std::string_view card_id, SDL_Renderer*) -> card::ptr {
                    return std::make_shared<rouen::cards::trello_card>(std::string(card_id), 
                                                                      rouen::cards::trello_card::card_context::card_specific);
                };
            } catch (...) {
                (void)0;
            }
        }
    };

    // Create a static instance to register the Trello cards during initialization
    const trello_card_registrar registrar;
}
