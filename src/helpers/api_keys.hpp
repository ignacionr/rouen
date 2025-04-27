#pragma once

#include <string>
#include <cstdlib>
#include <optional>

namespace rouen::helpers {
    /**
     * Utility class for managing API keys in a centralized way
     * Following the DRY principle to avoid duplicating environment variable access
     */
    class ApiKeys {
    public:
        /**
         * Get the Grok API key from the environment variable
         * @return The API key as a string, or empty string if not set
         */
        static std::string get_grok_api_key() {
            const char* api_key = std::getenv("GROK_API_KEY");
            return api_key ? api_key : "";
        }

        /**
         * Checks if the Grok API key is available
         * @return true if the API key is set and not empty
         */
        static bool has_grok_api_key() {
            return !get_grok_api_key().empty();
        }
    };
}