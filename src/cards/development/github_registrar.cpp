#include "../interface/factory.hpp"
#include "github/github_card.hpp"

// This file provides GitHub card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers the GitHub card with the factory
    struct github_card_registrar {
        github_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the GitHub card entry
            dict["github"] = [](std::string_view uri, SDL_Renderer*) {
                if (uri.empty()) {
                    return std::make_shared<rouen::cards::github_card>();
                } else {
                    return std::make_shared<rouen::cards::github_card>(uri);
                }
            };
        }
    };

    // Create a static instance to register the GitHub card during initialization
    static github_card_registrar registrar;
}