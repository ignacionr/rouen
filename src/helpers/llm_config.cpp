#include "llm_config.hpp"
#include "debug.hpp"

namespace rouen::helpers {

std::shared_ptr<ConfigService> LLMConfig::config_service_;

void LLMConfig::ensure_config_service() {
    if (!config_service_) {
        config_service_ = ConfigService::instance();
    }
}

LLMConfig::LLMSettings LLMConfig::get_current_config() {
    ensure_config_service();
    
    LLMSettings settings{};
    
    // Get provider
    std::string provider_str = config_service_->get_env_optional("LLM_PROVIDER").value_or("grok");
    settings.provider = string_to_provider(provider_str);
    
    // Get provider-specific configuration
    switch (settings.provider) {
        case Provider::GROK:
            settings.api_key = config_service_->get_env_optional("GROK_API_KEY").value_or("");
            settings.base_url = ignacionr::cppgpt::grok_base;
            settings.model_name = "grok-3-latest";
            break;
            
        case Provider::OPENAI:
            settings.api_key = config_service_->get_env_optional("OPENAI_API_KEY").value_or("");
            settings.base_url = ignacionr::cppgpt::open_ai_base;
            settings.model_name = "gpt-4";
            break;
            
        case Provider::GROQ:
            settings.api_key = config_service_->get_env_optional("GROQ_API_KEY").value_or("");
            settings.base_url = ignacionr::cppgpt::groq_base;
            settings.model_name = "llama3-8b-8192";
            break;
            
        case Provider::GEMINI:
            settings.api_key = config_service_->get_env_optional("GEMINI_API_KEY").value_or("");
            settings.base_url = "https://generativelanguage.googleapis.com/v1beta/";
            settings.model_name = "gemini-1.5-pro";
            break;
            
        case Provider::CUSTOM:
            settings.api_key = config_service_->get_env_optional("LLM_CUSTOM_API_KEY").value_or("");
            settings.base_url = config_service_->get_env_optional("LLM_CUSTOM_URL").value_or("");
            settings.model_name = config_service_->get_env_optional("LLM_CUSTOM_MODEL").value_or("gpt-3.5-turbo");
            break;
    }
    
    // Check if configuration is complete
    settings.is_configured = !settings.api_key.empty() && !settings.base_url.empty() && !settings.model_name.empty();
    
    return settings;
}

std::unique_ptr<ignacionr::cppgpt> LLMConfig::create_llm_instance() {
    auto settings = get_current_config();
    
    if (!settings.is_configured) {
        CONFIG_WARN("LLM configuration is incomplete. Cannot create LLM instance.");
        return nullptr;
    }
    
    try {
        auto llm = std::make_unique<ignacionr::cppgpt>(settings.api_key, settings.base_url);
        
        // Add provider-specific instructions
        switch (settings.provider) {
            case Provider::GROK:
                llm->add_instructions("You are Grok, an AI assistant created by xAI. You are helpful, harmless, and honest.");
                break;
            case Provider::OPENAI:
                llm->add_instructions("You are ChatGPT, a helpful AI assistant created by OpenAI.");
                break;
            case Provider::GROQ:
                llm->add_instructions("You are a helpful AI assistant powered by Groq's fast inference.");
                break;
            case Provider::GEMINI:
                llm->add_instructions("You are Gemini, a helpful AI assistant created by Google.");
                break;
            case Provider::CUSTOM:
                llm->add_instructions("You are a helpful AI assistant.");
                break;
        }
        
        CONFIG_INFO_FMT("Created LLM instance for provider: {}", provider_to_string(settings.provider));
        return llm;
        
    } catch (const std::exception& e) {
        CONFIG_ERROR_FMT("Failed to create LLM instance: {}", e.what());
        return nullptr;
    }
}

bool LLMConfig::is_configured() {
    auto settings = get_current_config();
    return settings.is_configured;
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
        case Provider::GEMINI: return "gemini-1.5-pro";
        case Provider::CUSTOM: return "gpt-3.5-turbo";
        default: return "grok-3-latest";
    }
}

std::string LLMConfig::get_base_url(Provider provider) {
    switch (provider) {
        case Provider::GROK: return ignacionr::cppgpt::grok_base;
        case Provider::OPENAI: return ignacionr::cppgpt::open_ai_base;
        case Provider::GROQ: return ignacionr::cppgpt::groq_base;
        case Provider::GEMINI: return "https://generativelanguage.googleapis.com/v1beta/";
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
