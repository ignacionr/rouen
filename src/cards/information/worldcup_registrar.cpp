#include "../interface/factory.hpp"
#include "worldcup_dashboard.hpp"

// This file provides FIFA World Cup card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers the World Cup card with the factory
    struct worldcup_card_registrar {
        worldcup_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the World Cup card
            dict["worldcup"] = [](std::string_view, SDL_Renderer* renderer) {
                auto card_ptr = std::make_shared<rouen::cards::worldcup_dashboard>();
                card_ptr->set_renderer(renderer);
                return card_ptr;
            };
        }
    };

    // Create a static instance to register the card during initialization
    static worldcup_card_registrar registrar;
}
