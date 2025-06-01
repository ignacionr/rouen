#pragma once

#include "config_service.hpp"
#include "../registrar.hpp"
#include "debug.hpp"

namespace rouen::helpers {

    /**
     * Configuration service initializer
     * Registers the configuration service with the registrar and sets up initial configurations
     */
    class ConfigServiceInitializer {
    public:
        /**
         * Initialize and register the configuration service
         * This should be called early in the application startup
         */
        static void initialize() {
            try {
                // Get or create the configuration service instance
                auto config_service = ConfigService::instance();
                
                // Register with the registrar for dependency injection
                registrar::add<ConfigService>("config", config_service);
                
                // Log initial configuration status
                config_service->log_configuration_status();
                
                // Validate required configurations
                auto missing_configs = config_service->validate_required_configs();
                if (!missing_configs.empty()) {
                    CONFIG_WARN_FMT("Application started with {} missing required configurations", 
                                   missing_configs.size());
                }
                
                CONFIG_INFO("Configuration service initialized and registered successfully");
                
            } catch (const std::exception& e) {
                CONFIG_ERROR_FMT("Failed to initialize configuration service: {}", e.what());
                throw;
            }
        }
        
        /**
         * Get the configuration service from the registrar
         * @return Shared pointer to the configuration service
         */
        static std::shared_ptr<ConfigService> get_service() {
            try {
                return registrar::get<ConfigService>("config");
            } catch (const std::exception& e) {
                CONFIG_WARN_FMT("Configuration service not found in registrar, creating new instance: {}", e.what());
                return ConfigService::instance();
            }
        }
        
        /**
         * Register additional JIRA profile configurations dynamically
         * Scans environment variables for JIRA profiles and registers them
         */
        static void register_jira_profiles() {
            auto config_service = get_service();
            auto profiles = config_service->get_jira_profiles();
            
            for (const auto& profile : profiles) {
                // Register common JIRA configuration keys for each profile
                std::vector<std::string> jira_keys = {"URL", "USERNAME", "TOKEN", "PROJECT"};
                
                for (const auto& key : jira_keys) {
                    std::string config_name = "JIRA_" + profile + "_" + key;
                    bool is_sensitive = (key == "TOKEN" || key == "PASSWORD");
                    bool is_required = (key == "URL"); // URL is typically required
                    
                    config_service->register_config(
                        config_name,
                        ConfigService::Category::JIRA_PROFILES,
                        is_required,
                        is_sensitive,
                        "JIRA " + key + " for profile " + profile
                    );
                }
            }
            
            CONFIG_INFO_FMT("Registered configurations for {} JIRA profiles", profiles.size());
        }
        
        /**
         * Register Bybit-specific configurations
         */
        static void register_bybit_configs() {
            auto config_service = get_service();
            
            // Additional Bybit configurations beyond the defaults
            config_service->register_config("BYBIT_TESTNET", ConfigService::Category::BYBIT_CONFIG,
                                           false, false, "Enable Bybit testnet mode");
            config_service->register_config("BYBIT_RECV_WINDOW", ConfigService::Category::BYBIT_CONFIG,
                                           false, false, "Bybit API receive window timeout");
            config_service->register_config("BYBIT_TIMEOUT", ConfigService::Category::BYBIT_CONFIG,
                                           false, false, "Bybit API request timeout");
            
            CONFIG_INFO("Registered additional Bybit configurations");
        }
        
        /**
         * Set up configuration monitoring for dynamic updates
         */
        static void setup_monitoring() {
            auto config_service = get_service();
            
            // Set up change callback for configuration monitoring
            config_service->set_change_callback([](const std::string& key, const std::string& value) {
                CONFIG_DEBUG_FMT("Configuration changed: {} = {}", key, 
                               key.find("SECRET") != std::string::npos || 
                               key.find("KEY") != std::string::npos ||
                               key.find("TOKEN") != std::string::npos ? "***" : value);
            });
            
            CONFIG_INFO("Configuration monitoring enabled");
        }
    };

} // namespace rouen::helpers
