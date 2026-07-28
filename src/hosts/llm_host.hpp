#pragma once

#include <string>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include <stdexcept>
#include <utility>

#include "config_service.hpp"
#include "cppgpt.hpp"
#include "gemini_adapter.hpp"
#include "glaze_include.hpp"

namespace rouen::hosts {

    /**
     * LLM Host & Service Manager
     * Manages LLM authentication API keys, provider endpoint configurations,
     * token usage metrics, and multi-provider instances (Grok, OpenAI, Groq, Gemini, custom).
     */
    class LLMHost {
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
            
            LLMSettings() = default;
            LLMSettings(Provider p, std::string key, std::string url, std::string model, bool configured, std::string name = "")
                : provider(p), api_key(std::move(key)), base_url(std::move(url)), 
                  model_name(std::move(model)), is_configured(configured), config_name(std::move(name)) {}
        };

        class LLMInstanceBase {
        public:
            virtual ~LLMInstanceBase() = default;
            virtual void add_instructions(std::string_view instructions, std::string_view role = "system") = 0;
            virtual void clear() = 0;
        };

        class LLMInstance {
        public:
            std::variant<
                std::unique_ptr<ignacionr::cppgpt>,
                std::unique_ptr<rouen::helpers::GeminiAdapter>
            > instance_;
            
            LLMInstance() = default;
            
            explicit LLMInstance(std::unique_ptr<ignacionr::cppgpt> ptr) 
                : instance_(std::move(ptr)) {}
            
            explicit LLMInstance(std::unique_ptr<rouen::helpers::GeminiAdapter> ptr) 
                : instance_(std::move(ptr)) {}
            
            LLMInstance(const LLMInstance&) = delete;
            LLMInstance& operator=(const LLMInstance&) = delete;
            LLMInstance(LLMInstance&&) = default;
            LLMInstance& operator=(LLMInstance&&) = default;
            
            explicit operator bool() const noexcept { 
                if (instance_.index() == 0) {
                    const auto* ptr = std::get_if<std::unique_ptr<ignacionr::cppgpt>>(&instance_);
                    return ptr && *ptr;
                } else if (instance_.index() == 1) {
                    const auto* ptr = std::get_if<std::unique_ptr<rouen::helpers::GeminiAdapter>>(&instance_);
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
        
        static LLMSettings get_current_config(const std::string& config_name = "");
        static std::optional<LLMInstance> create_llm_instance(const std::string& config_name = "");
        
        template<typename T, typename Func>
        static auto with_llm_instance(const LLMInstance& instance, Func&& func) -> decltype(func(std::declval<T&>())) {
            if (!instance) {
                throw std::runtime_error("LLM instance not initialized");
            }
            
            return std::visit([&](const auto& ptr) -> decltype(func(std::declval<T&>())) {
                if (!ptr) {
                    throw std::runtime_error("Null LLM instance");
                }
                
                if constexpr (std::is_same_v<T, std::decay_t<decltype(*ptr)>>) {
                    return func(*ptr);
                } else {
                    throw std::runtime_error("Invalid LLM instance type cast");
                }
            }, instance.instance_);
        }

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
        
        static bool is_configured(const std::string& config_name = "");
        static Provider string_to_provider(const std::string& provider_str);
        static std::string provider_to_string(Provider provider);
        static std::string get_default_model(Provider provider);
        static std::string get_base_url(Provider provider);
        static std::string get_api_key_env_name(Provider provider);

    private:
        static std::shared_ptr<rouen::helpers::ConfigService> config_service_;
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

} // namespace rouen::hosts

namespace rouen::helpers {
    using LLMConfig = ::rouen::hosts::LLMHost;
    using LLMConfigEntry = ::rouen::hosts::LLMConfigEntry;
    using LLMConfigManager = ::rouen::hosts::LLMConfigManager;
}
