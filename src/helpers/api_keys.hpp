#pragma once

#include <string>
#include <cstdlib>
#include <optional>
#include "config_service.hpp"

namespace rouen::helpers {
    /**
     * Utility class for managing API keys in a centralized way
     * Following the DRY principle to avoid duplicating environment variable access
     * Now uses the centralized ConfigService for consistent configuration management
     */
    class ApiKeys {
    public:
        /**
         * Get the Grok API key from the environment variable
         * @return The API key as a string, or empty string if not set
         */
        static std::string get_grok_api_key() {
            auto config_service = ConfigService::instance();
            return config_service->get_api_key("GROK");
        }

        /**
         * Checks if the Grok API key is available
         * @return true if the API key is set and not empty
         */
        static bool has_grok_api_key() {
            return !get_grok_api_key().empty();
        }
        
        /**
         * Get Bybit API key using the centralized configuration service
         * @return The Bybit API key or empty string if not set
         */
        static std::string get_bybit_api_key() {
            auto config_service = ConfigService::instance();
            return config_service->get_bybit_api_key();
        }
        
        /**
         * Get Bybit API secret using the centralized configuration service
         * @return The Bybit API secret or empty string if not set
         */
        static std::string get_bybit_secret() {
            auto config_service = ConfigService::instance();
            return config_service->get_bybit_secret();
        }
        
        /**
         * Checks if Bybit API credentials are available
         * @return true if both API key and secret are set
         */
        static bool has_bybit_credentials() {
            return !get_bybit_api_key().empty() && !get_bybit_secret().empty();
        }
        
        /**
         * Get API key for any service using common patterns
         * @param service_name The name of the service (e.g., "GITHUB", "OPENAI")
         * @return The API key or empty string if not found
         */
        static std::string get_service_api_key(const std::string& service_name) {
            auto config_service = ConfigService::instance();
            return config_service->get_api_key(service_name);
        }
        
        /**
         * Check if API key exists for a service
         * @param service_name The name of the service
         * @return true if API key is available
         */
        static bool has_service_api_key(const std::string& service_name) {
            return !get_service_api_key(service_name).empty();
        }
    };
}