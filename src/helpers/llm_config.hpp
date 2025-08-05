#pragma once

#include <string>
#include <memory>
#include <optional>
#include <variant>
#include "config_service.hpp"
#include "cppgpt.hpp"
#include "gemini_adapter.hpp"

namespace rouen::helpers {

    /**
     * LLM Configuration Manager
     * Template-based approach with zero runtime overhead
     * Supports multiple providers: Grok, OpenAI, Groq, Gemini, and custom endpoints
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

        // Type alias for LLM instance variant
        using LLMInstance = std::variant<
            std::unique_ptr<ignacionr::cppgpt>,
            std::unique_ptr<GeminiAdapter>
        >;
        
        /**
         * Get the current LLM configuration
         * @return LLMSettings struct with current configuration
         */
        static LLMSettings get_current_config();
        
        /**
         * Get a configured LLM instance based on current settings
         * Returns either a cppgpt instance or a GeminiAdapter instance
         * @return LLMInstance variant containing the appropriate adapter
         */
        static std::optional<LLMInstance> create_llm_instance();
        
        /**
         * Template function to execute operations on any LLM type
         * Provides unified interface regardless of underlying implementation
         * @param variant The LLM instance variant (cppgpt or GeminiAdapter)
         * @param operation Function/lambda to execute on the instance
         * @return Result of the operation
         */
        // True compile-time dispatch avoiding std::visit entirely
        template<typename Func>
        static auto with_llm_instance(const LLMInstance& variant, Func&& operation) {
            if (std::holds_alternative<std::unique_ptr<ignacionr::cppgpt>>(variant)) {
                const auto& ptr = std::get<std::unique_ptr<ignacionr::cppgpt>>(variant);
                if (!ptr) {
                    throw std::runtime_error("cppgpt instance is null");
                }
                return operation(*ptr);
            } else if (std::holds_alternative<std::unique_ptr<GeminiAdapter>>(variant)) {
                const auto& ptr = std::get<std::unique_ptr<GeminiAdapter>>(variant);
                if (!ptr) {
                    throw std::runtime_error("GeminiAdapter instance is null");
                }
                return operation(*ptr);
            } else {
                throw std::runtime_error("Unknown LLM variant type");
            }
        }

        /**
         * Template helper to create and use an LLM instance in one call
         * @param operation Function/lambda to execute on the LLM instance
         * @return Optional result of the operation
         */
        template<typename Func>
        static auto with_configured_llm(Func&& operation) {
            auto instance = create_llm_instance();
            if (!instance) {
                return std::nullopt;
            }
            
            return with_llm_instance(*instance, [&operation](auto& llm) {
                return std::make_optional(operation(llm));
            });
        }
        
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
