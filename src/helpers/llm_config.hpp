#pragma once

#include <string>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include "config_service.hpp"
#include "cppgpt.hpp"
#include "gemini_adapter.hpp"
#include "glaze_include.hpp"

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
            std::string config_name{};
            
            // Constructor for aggregate initialization with defaults
            LLMSettings() = default;
            LLMSettings(Provider p, std::string key, std::string url, std::string model, bool configured, std::string name = "")
                : provider(p), api_key(std::move(key)), base_url(std::move(url)), 
                  model_name(std::move(model)), is_configured(configured), config_name(std::move(name)) {}
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
         * @param config_name Optional configuration name. If empty, uses active persona's config or default config.
         * @return LLMSettings struct with current configuration
         */
        static LLMSettings get_current_config(const std::string& config_name = "");
        
        /**
         * Get a configured LLM instance based on current settings
         * Returns a memory-safe wrapped LLM instance
         * @param config_name Optional configuration name. If empty, uses active persona's config or default config.
         * @return LLMInstance containing the appropriate adapter
         */
        static std::optional<LLMInstance> create_llm_instance(const std::string& config_name = "");
        
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
         * @param config_name Optional configuration name. If empty, uses active persona's config or default config.
         * @return true if configuration is complete and usable
         */
        static bool is_configured(const std::string& config_name = "");
        
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

    struct LLMConfigEntry {
        std::string name;
        std::string provider{"grok"};
        std::string api_key{};
        std::string base_url{};
        std::string model_name{};

        struct glaze {
            using T = LLMConfigEntry;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "provider", &T::provider,
                "api_key", &T::api_key,
                "base_url", &T::base_url,
                "model_name", &T::model_name
            );
        };
    };

    struct LLMConfigSaveModel {
        std::string default_config_name{"Default"};
        std::vector<LLMConfigEntry> configs;

        struct glaze {
            using T = LLMConfigSaveModel;
            static constexpr auto value = glz::object(
                "default_config_name", &T::default_config_name,
                "configs", &T::configs
            );
        };
    };

    class LLMConfigManager {
    public:
        static LLMConfigManager& instance() {
            static LLMConfigManager mgr;
            return mgr;
        }

        const std::vector<LLMConfigEntry>& get_configs() const {
            return configs_;
        }

        const std::string& get_default_config_name() const {
            return default_config_name_;
        }

        void set_default_config_name(const std::string& name) {
            default_config_name_ = name;
            save_configs();
        }

        const LLMConfigEntry* get_config(const std::string& name) const {
            for (const auto& config : configs_) {
                if (config.name == name) {
                    return &config;
                }
            }
            return nullptr;
        }

        void add_config(const LLMConfigEntry& config) {
            configs_.push_back(config);
            save_configs();
        }

        void update_config(const std::string& old_name, const LLMConfigEntry& config) {
            for (auto& c : configs_) {
                if (c.name == old_name) {
                    c = config;
                    if (default_config_name_ == old_name) {
                        default_config_name_ = config.name;
                    }
                    save_configs();
                    return;
                }
            }
        }

        void delete_config(const std::string& name) {
            if (configs_.size() <= 1) {
                return;
            }
            auto it = std::remove_if(configs_.begin(), configs_.end(), [&](const auto& c) {
                return c.name == name;
            });
            if (it != configs_.end()) {
                configs_.erase(it, configs_.end());
                if (default_config_name_ == name) {
                    default_config_name_ = configs_[0].name;
                }
                save_configs();
            }
        }

        void reload() {
            load_configs();
        }

    private:
        LLMConfigManager();
        ~LLMConfigManager() = default;

        void setup_default_configs();
        void load_configs();
        void save_configs() const;

        std::vector<LLMConfigEntry> configs_;
        std::string default_config_name_{"Default"};
    };

} // namespace rouen::helpers
