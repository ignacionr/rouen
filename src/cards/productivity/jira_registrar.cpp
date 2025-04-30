#include "../interface/factory.hpp"
#include "jira_card.hpp"
#include "jira_projects.hpp"
#include "jira_search_dummy.hpp"

// This file provides Jira card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers all Jira cards with the factory
    struct jira_card_registrar {
        jira_card_registrar() {
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
                if (params.empty()) {
                    return std::make_shared<rouen::cards::jira_search_dummy_card>();
                } else {
                    // Use create_with_query for setting initial query
                    return rouen::cards::jira_search_dummy_card::create_with_query(params);
                }
            };
        }
    };

    // Create a static instance to register the Jira cards during initialization
    static jira_card_registrar registrar;
}