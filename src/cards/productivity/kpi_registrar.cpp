#include "../interface/factory.hpp"
#include "kpi_dashboard_card.hpp"
#include "kpi_management_card.hpp"
#include "kpi_hierarchy_card.hpp"
#include "kpi_reports_card.hpp"

// This file provides KPI card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers all KPI cards with the factory
    struct kpi_card_registrar {
        kpi_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the KPI Dashboard card
            dict["kpi-dashboard"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_dashboard_card>();
            };
            
            // Register the KPI Management card
            dict["kpi-management"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_management_card>();
            };
            
            // Register the KPI Hierarchy card
            dict["kpi-hierarchy"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_hierarchy_card>();
            };
            
            // Register the KPI Reports card
            dict["kpi-reports"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_reports_card>();
            };
            
            // Register convenience aliases
            dict["kpi"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_dashboard_card>();
            };
            
            dict["kpis"] = [](std::string_view, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_dashboard_card>();
            };
        }
    };

    // Create a static instance to register the KPI cards during initialization
    static kpi_card_registrar registrar;
}

// This file provides KPI card registration for Rouen
// It's compiled separately to avoid circular dependency issues

namespace {
    // This initializer registers all KPI cards with the factory
    struct kpi_card_registrar {
        kpi_card_registrar() {
            // Get the dictionary reference
            auto& dict = const_cast<std::unordered_map<std::string, rouen::cards::factory::factory_t>&>(
                rouen::cards::factory::dictionary());
            
            // Register the KPI Dashboard card
            dict["kpi-dashboard"] = [](std::string_view uri, SDL_Renderer*) {
                if (uri.empty()) {
                    return std::make_shared<rouen::cards::kpi_dashboard_card>();
                } else {
                    // Parse dashboard ID or other parameters from URI
                    return std::make_shared<rouen::cards::kpi_dashboard_card>(std::string(uri));
                }
            };
            
            // Register the KPI Management card
            dict["kpi-management"] = [](std::string_view uri, SDL_Renderer*) {
                if (uri.empty()) {
                    return std::make_shared<rouen::cards::kpi_management_card>();
                } else {
                    // Parse management parameters from URI (e.g., "edit:123")
                    return std::make_shared<rouen::cards::kpi_management_card>(std::string(uri));
                }
            };
            
            // Register the KPI Hierarchy card
            dict["kpi-hierarchy"] = [](std::string_view uri, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_hierarchy_card>();
            };
            
            // Register the KPI Reports card
            dict["kpi-reports"] = [](std::string_view uri, SDL_Renderer*) {
                return std::make_shared<rouen::cards::kpi_reports_card>();
            };
            
            // Register convenience aliases
            dict["kpi"] = [](std::string_view uri, SDL_Renderer*) {
                // Default to dashboard view
                if (uri.empty()) {
                    return std::make_shared<rouen::cards::kpi_dashboard_card>();
                } else {
                    return std::make_shared<rouen::cards::kpi_dashboard_card>(std::string(uri));
                }
            };
            
            dict["kpis"] = [](std::string_view uri, SDL_Renderer*) {
                // Default to management view for plural form
                return std::make_shared<rouen::cards::kpi_management_card>();
            };
        }
    };

    // Create a static instance to register the KPI cards during initialization
    static kpi_card_registrar registrar;
}
