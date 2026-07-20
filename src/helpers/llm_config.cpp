#include "llm_config.hpp"
#include "debug.hpp"
#include "persona_manager.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace rouen::helpers {

std::shared_ptr<ConfigService> LLMConfig::config_service_;

void LLMConfig::ensure_config_service() {
    if (!config_service_) {
        config_service_ = ConfigService::instance();
    }
}

LLMConfig::LLMSettings LLMConfig::get_current_config(const std::string& config_name) {
    std::string name = config_name;
    if (name.empty()) {
        auto& pm = PersonaManager::instance();
        name = pm.get_active_persona().llm_config_name;
    }
    
    if (name.empty()) {
        name = LLMConfigManager::instance().get_default_config_name();
    }
    
    const auto* entry = LLMConfigManager::instance().get_config(name);
    if (!entry) {
        // Try the default configuration if the persona's config name doesn't exist
        std::string def_name = LLMConfigManager::instance().get_default_config_name();
        entry = LLMConfigManager::instance().get_config(def_name);
    }
    
    LLMSettings settings{};
    if (entry) {
        settings.provider = string_to_provider(entry->provider);
        settings.model_name = entry->model_name;
        settings.base_url = entry->base_url;
        settings.api_key = entry->api_key;
        settings.config_name = entry->name;
        
        // Fallback for API key or URL or model to env/defaults if empty
        if (settings.api_key.empty()) {
            ensure_config_service();
            settings.api_key = config_service_->get_env_optional(get_api_key_env_name(settings.provider)).value_or("");
        }
        if (settings.base_url.empty()) {
            settings.base_url = get_base_url(settings.provider);
        }
        if (settings.model_name.empty()) {
            settings.model_name = get_default_model(settings.provider);
        }
    } else {
        // Fallback to global environment variables
        ensure_config_service();
        std::string provider_str = config_service_->get_env_optional("LLM_PROVIDER").value_or("grok");
        settings.provider = string_to_provider(provider_str);
        settings.config_name = "Global Env";
        
        switch (settings.provider) {
            case Provider::GROK:
                settings.api_key = config_service_->get_env_optional("GROK_API_KEY").value_or("");
                settings.base_url = "https://api.x.ai/v1";
                settings.model_name = "grok-3-latest";
                break;
            case Provider::OPENAI:
                settings.api_key = config_service_->get_env_optional("OPENAI_API_KEY").value_or("");
                settings.base_url = "https://api.openai.com/v1";
                settings.model_name = "gpt-4";
                break;
            case Provider::GROQ:
                settings.api_key = config_service_->get_env_optional("GROQ_API_KEY").value_or("");
                settings.base_url = "https://api.groq.com/openai/v1";
                settings.model_name = "llama3-8b-8192";
                break;
            case Provider::GEMINI:
                settings.api_key = config_service_->get_env_optional("GEMINI_API_KEY").value_or("");
                settings.base_url = "https://generativelanguage.googleapis.com/v1beta";
                settings.model_name = "gemini-2.5-flash-lite";
                break;
            case Provider::CUSTOM:
                settings.api_key = config_service_->get_env_optional("LLM_CUSTOM_API_KEY").value_or("");
                settings.base_url = config_service_->get_env_optional("LLM_CUSTOM_URL").value_or("");
                settings.model_name = config_service_->get_env_optional("LLM_CUSTOM_MODEL").value_or("gpt-3.5-turbo");
                break;
        }
    }
    
    settings.is_configured = !settings.api_key.empty() && !settings.base_url.empty() && !settings.model_name.empty();
    return settings;
}

std::optional<LLMConfig::LLMInstance> LLMConfig::create_llm_instance(const std::string& config_name) {
    auto settings = get_current_config(config_name);
    
    if (!settings.is_configured) {
        CONFIG_WARN("LLM configuration is incomplete. Cannot create LLM instance.");
        return std::nullopt;
    }
    
    try {
        // For Gemini provider, use the native Gemini adapter
        if (settings.provider == Provider::GEMINI) {
            auto adapter = std::make_unique<GeminiAdapter>(settings.api_key);
            CONFIG_INFO("Created Gemini adapter instance");
            return LLMInstance{std::move(adapter)};
        }
        
        // For all other providers, use cppgpt
        auto llm = std::make_unique<ignacionr::cppgpt>(settings.api_key, settings.base_url);
        
        // Debug logging to verify the configuration values
        CONFIG_INFO_FMT("Creating cppgpt instance with API key: {} chars, base URL: '{}'", 
                       settings.api_key.length(), settings.base_url);
        
        CONFIG_INFO_FMT("Created cppgpt LLM instance for provider: {}", provider_to_string(settings.provider));
        return LLMInstance{std::move(llm)};
        
    } catch (const std::exception& e) {
        CONFIG_ERROR_FMT("Failed to create LLM instance: {}", e.what());
        return std::nullopt;
    }
}

bool LLMConfig::is_configured(const std::string& config_name) {
    auto settings = get_current_config(config_name);
    return settings.is_configured;
}

LLMConfigManager::LLMConfigManager() {
    setup_default_configs();
    load_configs();
}

void LLMConfigManager::setup_default_configs() {
    configs_.clear();
    
    LLMConfigEntry default_config;
    default_config.name = "Default";
    
    auto config_service = ConfigService::instance();
    std::string provider_str = config_service->get_env_optional("LLM_PROVIDER").value_or("grok");
    default_config.provider = provider_str;
    
    auto prov = LLMConfig::string_to_provider(provider_str);
    switch (prov) {
        case LLMConfig::Provider::GROK:
            default_config.api_key = config_service->get_env_optional("GROK_API_KEY").value_or("");
            default_config.base_url = "https://api.x.ai/v1";
            default_config.model_name = "grok-3-latest";
            break;
        case LLMConfig::Provider::OPENAI:
            default_config.api_key = config_service->get_env_optional("OPENAI_API_KEY").value_or("");
            default_config.base_url = "https://api.openai.com/v1";
            default_config.model_name = "gpt-4";
            break;
        case LLMConfig::Provider::GROQ:
            default_config.api_key = config_service->get_env_optional("GROQ_API_KEY").value_or("");
            default_config.base_url = "https://api.groq.com/openai/v1";
            default_config.model_name = "llama3-8b-8192";
            break;
        case LLMConfig::Provider::GEMINI:
            default_config.api_key = config_service->get_env_optional("GEMINI_API_KEY").value_or("");
            default_config.base_url = "https://generativelanguage.googleapis.com/v1beta";
            default_config.model_name = "gemini-2.5-flash-lite";
            break;
        case LLMConfig::Provider::CUSTOM:
            default_config.api_key = config_service->get_env_optional("LLM_CUSTOM_API_KEY").value_or("");
            default_config.base_url = config_service->get_env_optional("LLM_CUSTOM_URL").value_or("");
            default_config.model_name = config_service->get_env_optional("LLM_CUSTOM_MODEL").value_or("gpt-3.5-turbo");
            break;
    }
    
    configs_.push_back(default_config);
    default_config_name_ = "Default";
}

void LLMConfigManager::load_configs() {
    try {
        auto path = rouen::platform::get_user_config_directory() / "llm_configs.json";
        if (!std::filesystem::exists(path)) {
            save_configs(); // Save the defaults
            return;
        }

        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        LLMConfigSaveModel model;
        auto err = glz::read_json(model, content);
        if (err) {
            std::cerr << "[LLMConfig] Failed to parse llm_configs.json: " << glz::format_error(err, content) << std::endl;
            return;
        }

        if (!model.configs.empty()) {
            configs_ = std::move(model.configs);
        }
        
        if (!model.default_config_name.empty()) {
            default_config_name_ = model.default_config_name;
        } else if (!configs_.empty()) {
            default_config_name_ = configs_[0].name;
        }
    } 
    catch (const std::exception& e) {
        std::cerr << "[LLMConfig] Exception loading llm_configs: " << e.what() << std::endl;
    }
}

void LLMConfigManager::save_configs() const {
    try {
        auto path = rouen::platform::get_user_config_directory() / "llm_configs.json";
        
        LLMConfigSaveModel model;
        model.default_config_name = default_config_name_;
        model.configs = configs_;

        std::string buffer = glz::write<glz::opts{.prettify = true}>(model).value_or("");
        if (!buffer.empty()) {
            std::ofstream file(path);
            if (file.is_open()) {
                file << buffer;
            }
        }
    } 
    catch (const std::exception& e) {
        std::cerr << "[LLMConfig] Exception saving llm_configs: " << e.what() << std::endl;
    }
}

LLMConfig::Provider LLMConfig::string_to_provider(const std::string& provider_str) {
    if (provider_str == "grok") return Provider::GROK;
    if (provider_str == "openai") return Provider::OPENAI;
    if (provider_str == "groq") return Provider::GROQ;
    if (provider_str == "gemini") return Provider::GEMINI;
    if (provider_str == "custom") return Provider::CUSTOM;
    
    // Default to Grok if unknown
    CONFIG_WARN_FMT("Unknown LLM provider '{}', defaulting to Grok", provider_str);
    return Provider::GROK;
}

std::string LLMConfig::provider_to_string(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "grok";
        case Provider::OPENAI: return "openai";
        case Provider::GROQ: return "groq";
        case Provider::GEMINI: return "gemini";
        case Provider::CUSTOM: return "custom";
        default: return "grok";
    }
}

std::string LLMConfig::get_default_model(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "grok-3-latest";
        case Provider::OPENAI: return "gpt-4";
        case Provider::GROQ: return "llama3-8b-8192";
        case Provider::GEMINI: return "gemini-2.5-flash-lite";
        case Provider::CUSTOM: return "gpt-3.5-turbo";
        default: return "grok-3-latest";
    }
}

std::string LLMConfig::get_base_url(Provider provider) {
    switch (provider) {
        case Provider::GROK: return ignacionr::cppgpt::grok_base;
        case Provider::OPENAI: return ignacionr::cppgpt::open_ai_base;
        case Provider::GROQ: return ignacionr::cppgpt::groq_base;
        case Provider::GEMINI: return "https://generativelanguage.googleapis.com/v1beta";
        case Provider::CUSTOM: 
            ensure_config_service();
            return config_service_->get_env_optional("LLM_CUSTOM_URL").value_or("");
        default: return ignacionr::cppgpt::grok_base;
    }
}

std::string LLMConfig::get_api_key_env_name(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "GROK_API_KEY";
        case Provider::OPENAI: return "OPENAI_API_KEY";
        case Provider::GROQ: return "GROQ_API_KEY";
        case Provider::GEMINI: return "GEMINI_API_KEY";
        case Provider::CUSTOM: return "LLM_CUSTOM_API_KEY";
        default: return "GROK_API_KEY";
    }
}

} // namespace rouen::helpers
