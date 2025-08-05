#pragma once

#include <string>
#include <memory>
#include <optional>
#include "config_service.hpp"
#include "cppgpt.hpp"

namespace rouen::helpers {

    /**
     * LLM Configuration Manager
     * Provides centralized management of LLM backend configurations
     * Supports multiple providers: Grok, OpenAI, Groq, and custom endpoints
     */
    class LLMConfig {
    public:
        enum class Provider {
            GROK,
            OPENAI, 
            GROQ,
            GEMINI,
            CUSTOM
        };
        
        struct LLMSettings {
            Provider provider;
            std::string api_key;
            std::string base_url;
            std::string model_name;
            bool is_configured;
        };
        
        /**
         * Get the current LLM configuration
         * @return LLMSettings struct with current configuration
         */
        static LLMSettings get_current_config();
        
        /**
         * Get a configured cppgpt instance based on current settings
         * @return std::unique_ptr to configured cppgpt instance, or nullptr if not configured
         */
        static std::unique_ptr<ignacionr::cppgpt> create_llm_instance();
        
        /**
         * Check if the current LLM configuration is valid and complete
         * @return true if configuration is complete and usable
         */
        static bool is_configured();
        
        /**
         * Get the provider enum from string value
         * @param provider_str String representation of provider
         * @return Provider enum value
         */
        static Provider string_to_provider(const std::string& provider_str);
        
        /**
         * Get the string representation of a provider
         * @param provider Provider enum value
         * @return String representation
         */
        static std::string provider_to_string(Provider provider);
        
        /**
         * Get the default model name for a provider
         * @param provider Provider enum value
         * @return Default model name
         */
        static std::string get_default_model(Provider provider);
        
        /**
         * Get the base URL for a provider
         * @param provider Provider enum value
         * @return Base URL for the provider
         */
        static std::string get_base_url(Provider provider);
        
        /**
         * Get the environment variable name for the API key of a provider
         * @param provider Provider enum value
         * @return Environment variable name for API key
         */
        static std::string get_api_key_env_name(Provider provider);

    private:
        static std::shared_ptr<ConfigService> config_service_;
        
        /**
         * Initialize the config service instance
         */
        static void ensure_config_service();
    };

} // namespace rouen::helpers
