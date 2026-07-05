#include "../interface/factory.hpp"
#include "movies.hpp"

// This file provides Movies card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers the Movies card with the factory
    struct movies_card_registrar {
        movies_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the Movies card
            dict["movies"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::movies>();
            };
        }
    };

    // Create a static instance to register the card during initialization
    static movies_card_registrar registrar;
}
