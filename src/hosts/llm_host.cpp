#include "llm_host.hpp"
#include "config_service.hpp"
#include "cppgpt.hpp"
#include "debug.hpp"
#include "gemini_adapter.hpp"
#include "persona_manager.hpp"
#include "platform_utils.hpp"
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rouen::hosts {

std::shared_ptr<rouen::helpers::ConfigService> LLMHost::config_service_;

void LLMHost::ensure_config_service() {
    if (!config_service_) {
        config_service_ = rouen::helpers::ConfigService::instance();
    }
}

LLMHost::LLMSettings LLMHost::get_current_config(const std::string& config_name) {
    std::string name = config_name;
    if (name.empty()) {
        auto& pm = rouen::helpers::PersonaManager::instance();
        name = pm.get_active_persona().llm_config_name;
    }
    
    if (name.empty()) {
        name = LLMConfigManager::instance().get_default_config_name();
    }
    
    const auto* entry = LLMConfigManager::instance().get_config(name);
    if (!entry) {
        std::string const def_name = LLMConfigManager::instance().get_default_config_name();
        entry = LLMConfigManager::instance().get_config(def_name);
    }
    
    LLMSettings settings{};
    if (entry) {
        settings.provider = string_to_provider(entry->provider);
        settings.model_name = entry->model_name;
        settings.base_url = entry->base_url;
        settings.api_key = entry->api_key;
        settings.config_name = entry->name;
        
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
        ensure_config_service();
        std::string const provider_str = config_service_->get_env_optional("LLM_PROVIDER").value_or("grok");
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
                settings.base_url = "https://generativelanguage.googleapis.com";
                settings.model_name = "gemini-1.5-flash";
                break;
            case Provider::CUSTOM:
                settings.api_key = config_service_->get_env_optional("LLM_API_KEY").value_or("");
                settings.base_url = config_service_->get_env_optional("LLM_BASE_URL").value_or("http://localhost:11434");
                settings.model_name = config_service_->get_env_optional("LLM_MODEL").value_or("llama2");
                break;
        }
    }
    
    settings.is_configured = !settings.api_key.empty();
    return settings;
}

std::optional<LLMHost::LLMInstance> LLMHost::create_llm_instance(const std::string& config_name) {
    auto settings = get_current_config(config_name);
    
    if (settings.api_key.empty()) {
        LOG_COMPONENT("LLMConfig", LOG_LEVEL_WARN, "Cannot create LLM instance: API key is empty");
        return std::nullopt;
    }
    
    try {
        if (settings.provider == Provider::GEMINI) {
            auto adapter = std::make_unique<rouen::helpers::GeminiAdapter>(settings.api_key, settings.model_name);
            return LLMInstance(std::move(adapter));
        } else {
            auto cppgpt = std::make_unique<ignacionr::cppgpt>(
                settings.api_key, 
                settings.base_url
            );
            return LLMInstance(std::move(cppgpt));
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfig", LOG_LEVEL_ERROR, std::string("Failed to create LLM instance: ") + e.what());
        return std::nullopt;
    }
}

bool LLMHost::is_configured(const std::string& config_name) {
    auto settings = get_current_config(config_name);
    return settings.is_configured;
}

LLMHost::Provider LLMHost::string_to_provider(const std::string& provider_str) {
    std::string lower_provider = provider_str;
    std::transform(lower_provider.begin(), lower_provider.end(), lower_provider.begin(), ::tolower);
    
    if (lower_provider == "grok" || lower_provider == "xai") {
        return Provider::GROK;
    } else if (lower_provider == "openai") {
        return Provider::OPENAI;
    } else if (lower_provider == "groq") {
        return Provider::GROQ;
    } else if (lower_provider == "gemini") {
        return Provider::GEMINI;
    } else {
        return Provider::CUSTOM;
    }
}

std::string LLMHost::provider_to_string(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "grok";
        case Provider::OPENAI: return "openai";
        case Provider::GROQ: return "groq";
        case Provider::GEMINI: return "gemini";
        case Provider::CUSTOM: return "custom";
    }
    return "custom";
}

std::string LLMHost::get_default_model(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "grok-3-latest";
        case Provider::OPENAI: return "gpt-4";
        case Provider::GROQ: return "llama3-8b-8192";
        case Provider::GEMINI: return "gemini-1.5-flash";
        case Provider::CUSTOM: return "llama2";
    }
    return "grok-3-latest";
}

std::string LLMHost::get_base_url(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "https://api.x.ai/v1";
        case Provider::OPENAI: return "https://api.openai.com/v1";
        case Provider::GROQ: return "https://api.groq.com/openai/v1";
        case Provider::GEMINI: return "https://generativelanguage.googleapis.com";
        case Provider::CUSTOM: return "http://localhost:11434";
    }
    return "https://api.x.ai/v1";
}

std::string LLMHost::get_api_key_env_name(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "GROK_API_KEY";
        case Provider::OPENAI: return "OPENAI_API_KEY";
        case Provider::GROQ: return "GROQ_API_KEY";
        case Provider::GEMINI: return "GEMINI_API_KEY";
        case Provider::CUSTOM: return "LLM_API_KEY";
    }
    return "GROK_API_KEY";
}

LLMConfigManager::LLMConfigManager() {
    auto config_dir = rouen::platform::get_user_config_directory();
    config_dir /= "llm_configs.json";
    
    if (std::filesystem::exists(config_dir)) {
        load_configs();
    } else {
        setup_default_configs();
    }
}

void LLMConfigManager::setup_default_configs() {
    configs_.clear();
    
    LLMConfigEntry grok_entry;
    grok_entry.name = "Grok Default";
    grok_entry.provider = "grok";
    grok_entry.model_name = "grok-3-latest";
    configs_.push_back(grok_entry);
    
    LLMConfigEntry openai_entry;
    openai_entry.name = "OpenAI GPT-4";
    openai_entry.provider = "openai";
    openai_entry.model_name = "gpt-4";
    configs_.push_back(openai_entry);

    LLMConfigEntry gemini_entry;
    gemini_entry.name = "Gemini Flash";
    gemini_entry.provider = "gemini";
    gemini_entry.model_name = "gemini-1.5-flash";
    configs_.push_back(gemini_entry);
    
    default_config_name_ = "Grok Default";
    save_configs();
}

void LLMConfigManager::load_configs() {
    auto config_path = rouen::platform::get_user_config_directory() / "llm_configs.json";
    if (!std::filesystem::exists(config_path)) {
        setup_default_configs();
        return;
    }

    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            setup_default_configs();
            return;
        }

        std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        LLMConfigSaveModel save_model;
        auto err = glz::read_json(save_model, json_str);
        if (err) {
            LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, "Failed to parse llm_configs.json");
            setup_default_configs();
            return;
        }

        configs_ = save_model.configs;
        default_config_name_ = save_model.default_config_name;

        if (configs_.empty()) {
            setup_default_configs();
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, std::string("Error loading LLM configs: ") + e.what());
        setup_default_configs();
    }
}

void LLMConfigManager::save_configs() const {
    auto config_path = rouen::platform::get_user_config_directory() / "llm_configs.json";
    
    try {
        LLMConfigSaveModel save_model;
        save_model.default_config_name = default_config_name_;
        save_model.configs = configs_;

        std::string json_str;
        auto err = glz::write_json(save_model, json_str);
        if (err) {
            LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, "Failed to serialize LLM configs");
            return;
        }

        std::ofstream file(config_path);
        if (file.is_open()) {
            file << json_str;
            file.close();
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, std::string("Error saving LLM configs: ") + e.what());
    }
}

} // namespace rouen::hosts
