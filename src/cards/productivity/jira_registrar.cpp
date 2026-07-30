#include "../interface/factory.hpp"
#include "cards/interface/card.hpp"
#include "jira_card.hpp"
#include "jira_projects.hpp"
#include "sdl_compat.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// This file provides Jira card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers all Jira cards with the factory
    struct jira_card_registrar {
        jira_card_registrar() noexcept {
            try {
                // Get the dictionary reference
                auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                    rouen::cards::factory::dictionary());
                
                // Register the main Jira card
                dict["jira"] = [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<rouen::cards::jira_card>();
                };
                
                // Register the Jira Projects card
                dict["jira-projects"] = [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<rouen::cards::jira_projects_card>();
                };
                
                // Register the Jira Search card with JQL query support
                dict["jira-search"] = [](std::string_view params, SDL_Renderer*) -> std::shared_ptr<card> {
                    auto card = std::make_shared<rouen::cards::jira_card>();
                    // TODO: Set up query parameter when jira_card supports it
                    if (!params.empty()) {
                        // In the future, set up the query parameter here
                    }
                    return card;
                };
            } catch (...) {
                (void)0;
            }
        }
    };

    // Create a static instance to register the Jira cards during initialization
    const jira_card_registrar registrar;
}
