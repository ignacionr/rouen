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
            Provider provider{Provider::GROK};
            std::string api_key{};
            std::string base_url{};
            std::string model_name{};
            bool is_configured{false};
            
            // Constructor for aggregate initialization with defaults
            LLMSettings() = default;
            LLMSettings(Provider p, std::string key, std::string url, std::string model, bool configured)
                : provider(p), api_key(std::move(key)), base_url(std::move(url)), 
                  model_name(std::move(model)), is_configured(configured) {}
        };

        // Forward declaration for the base class
        class LLMInstanceBase {
        public:
            virtual ~LLMInstanceBase() = default;
            virtual void add_instructions(std::string_view instructions, std::string_view role = "system") = 0;
            virtual void clear() = 0;
        };

        // Memory-safe wrapper for LLM instances using variant
        class LLMInstance {
        public:
            // Use variant to store different wrapper types safely
            std::variant<
                std::unique_ptr<ignacionr::cppgpt>,
                std::unique_ptr<GeminiAdapter>
            > instance_;
            
            LLMInstance() = default;
            
            // Constructors for different types
            explicit LLMInstance(std::unique_ptr<ignacionr::cppgpt> ptr) 
                : instance_(std::move(ptr)) {}
            
            explicit LLMInstance(std::unique_ptr<GeminiAdapter> ptr) 
                : instance_(std::move(ptr)) {}
            
            // Non-copyable but movable
            LLMInstance(const LLMInstance&) = delete;
            LLMInstance& operator=(const LLMInstance&) = delete;
            LLMInstance(LLMInstance&&) = default;
            LLMInstance& operator=(LLMInstance&&) = default;
            
            explicit operator bool() const noexcept { 
                if (instance_.index() == 0) {
                    const auto* ptr = std::get_if<std::unique_ptr<ignacionr::cppgpt>>(&instance_);
                    return ptr && *ptr;
                } else if (instance_.index() == 1) {
                    const auto* ptr = std::get_if<std::unique_ptr<GeminiAdapter>>(&instance_);
                    return ptr && *ptr;
                }
                return false;
            }
            
            void add_instructions(std::string_view instructions, std::string_view role = "system") {
                std::visit([&](auto& ptr) {
                    if (!ptr) throw std::runtime_error("Null LLM instance access");
                    ptr->add_instructions(instructions, role);
                }, instance_);
            }
            
            void clear() {
                std::visit([](auto& ptr) {
                    if (!ptr) throw std::runtime_error("Null LLM instance access");
                    ptr->clear();
                }, instance_);
            }
            
            template<typename DoPostFunc>
            ignacionr::ChatCompletion sendMessage(
                std::string_view message,
                DoPostFunc&& do_post,
                std::string_view role = "user",
                std::string_view model = "",
                std::string_view search_mode = {},
                float temperature = 0.45f,
                const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr
            ) {
                return std::visit([&](auto& ptr) -> ignacionr::ChatCompletion {
                    if (!ptr) throw std::runtime_error("Null LLM instance access");
                    return ptr->sendMessage(message, std::forward<DoPostFunc>(do_post), role, model, search_mode, temperature, full_conversation);
                }, instance_);
            }
            
            void reset() { 
                std::visit([](auto& ptr) { ptr.reset(); }, instance_);
            }
        };
        
        /**
         * Get the current LLM configuration
         * @return LLMSettings struct with current configuration
         */
        static LLMSettings get_current_config();
        
        /**
         * Get a configured LLM instance based on current settings
         * Returns a memory-safe wrapped LLM instance
         * @return LLMInstance containing the appropriate adapter
         */
        static std::optional<LLMInstance> create_llm_instance();
        
        /**
         * Template function to execute operations on any LLM type
         * Provides unified interface with strong exception safety
         * @param instance The LLM instance wrapper
         * @param func Function/lambda to execute on the instance
         * @return Result of the operation
         */
        template<typename T, typename Func>
        static auto with_llm_instance(const LLMInstance& instance, Func&& func) -> decltype(func(std::declval<T&>())) {
            if (!instance) {
                throw std::runtime_error("LLM instance not initialized");
            }
            
            return std::visit([&](const auto& ptr) -> decltype(func(std::declval<T&>())) {
                if (!ptr) {
                    throw std::runtime_error("Null LLM instance");
                }
                
                // Check if the stored type matches the requested type
                if constexpr (std::is_same_v<T, std::decay_t<decltype(*ptr)>>) {
                    return func(*ptr);
                } else {
                    throw std::runtime_error("Invalid LLM instance type cast");
                }
            }, instance.instance_);
        }

        /**
         * Template helper to create and use an LLM instance in one call
         * @param func Function/lambda to execute on the LLM instance
         * @return Optional result of the operation
         */
        template<typename Func>
        static auto with_configured_llm(Func&& func) {
            auto instance = create_llm_instance();
            if (!instance) {
                return std::nullopt;
            }
            
            return std::visit([&](const auto& ptr) {
                if (!ptr) {
                    return std::nullopt;
                }
                return std::make_optional(func(*ptr));
            }, instance->instance_);
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
