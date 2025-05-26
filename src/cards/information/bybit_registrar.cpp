#include "../interface/factory.hpp"
#include "bybit_assets.hpp"

// This file provides Bybit card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers the Bybit card with the factory
    struct bybit_card_registrar {
        bybit_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the Bybit assets card
            dict["bybit-assets"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::bybit_assets>();
            };
            
            // Also register with shorter name for convenience
            dict["bybit"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::bybit_assets>();
            };
        }
    };

    // Create a static instance to register the Bybit card during initialization
    static bybit_card_registrar registrar;
}
