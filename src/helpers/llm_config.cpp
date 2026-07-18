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
            settings.base_url = "https://api.x.ai/v1";  // Direct hardcoded URL
            settings.model_name = "grok-3-latest";
            break;
            
        case Provider::OPENAI:
            settings.api_key = config_service_->get_env_optional("OPENAI_API_KEY").value_or("");
            settings.base_url = "https://api.openai.com/v1";  // Direct hardcoded URL  
            settings.model_name = "gpt-4";
            break;
            
        case Provider::GROQ:
            settings.api_key = config_service_->get_env_optional("GROQ_API_KEY").value_or("");
            settings.base_url = "https://api.groq.com/openai/v1";  // Direct hardcoded URL
            settings.model_name = "llama3-8b-8192";
            break;
            
        case Provider::GEMINI:
            settings.api_key = config_service_->get_env_optional("GEMINI_API_KEY").value_or("");
            // Note: Direct Google Gemini API is not OpenAI-compatible
            // For Gemini support, use Custom provider with an OpenAI-compatible proxy
            // such as: https://api.openai-proxy.org/v1 or similar service
            settings.base_url = "https://generativelanguage.googleapis.com/v1beta";
            settings.model_name = "gemini-2.5-flash-lite";
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

std::optional<LLMConfig::LLMInstance> LLMConfig::create_llm_instance() {
    auto settings = get_current_config();
    
    if (!settings.is_configured) {
        CONFIG_WARN("LLM configuration is incomplete. Cannot create LLM instance.");
        return std::nullopt;
    }
    
    try {
        // For Gemini provider, use the native Gemini adapter
        if (settings.provider == Provider::GEMINI) {
            auto adapter = std::make_unique<GeminiAdapter>(settings.api_key);
            
            adapter->add_instructions("You are a helpful AI assistant powered by Google's Gemini model, integrated into Rouen, a card-based desktop application. "
                                    "Rouen organizes its UI as cards - each feature (weather, git, terminal, etc.) is a visual card that can be opened, closed, and interacted with. "
                                    "When users ask you to 'open', 'show', 'create', or 'display' something (e.g., 'show me the weather in Paris', 'open a weather card'), "
                                    "use the appropriate create_* tool (like create_weather_card) to spawn a new card in the UI. "
                                    "You are knowledgeable, accurate, and provide helpful responses. "
                                    "You have access to tools that can run local commands (e.g. bash commands). "
                                    "If the user asks you to check repository status, files, find the current date/time, or execute any shell "
                                    "command (including curl), use the provided run_local_command tool to execute them instead of giving them instructions on how to run it themselves. "
                                    "IMPORTANT: Differentiate clearly between Pomodoro and general alarms. "
                                    "Only use the Pomodoro tool (start_pomodoro) if the user explicitly mentions the word 'pomodoro'. "
                                    "For all other general alarms, timers, or reminders (e.g., 'set an alarm/timer for 20 minutes', 'alarm at 10 AM', 'remind me in 1 hour'), "
                                    "you MUST use the 'create_alarm' tool instead.");
            
            CONFIG_INFO("Created Gemini adapter instance");
            return LLMInstance{std::move(adapter)};
        }
        
        // For all other providers, use cppgpt
        auto llm = std::make_unique<ignacionr::cppgpt>(settings.api_key, settings.base_url);
        
        // Debug logging to verify the configuration values
        CONFIG_INFO_FMT("Creating cppgpt instance with API key: {} chars, base URL: '{}'", 
                       settings.api_key.length(), settings.base_url);
        
        // Add provider-specific instructions
        std::string tool_instr = " You are integrated into Rouen, a card-based desktop application where each feature (weather, git, terminal, etc.) is a visual card. "
                                 "When users ask you to 'open', 'show', 'create', or 'display' something (e.g., 'show me the weather in Paris', 'open a weather card'), "
                                 "use the appropriate create_* tool (like create_weather_card) to spawn a new card in the UI. "
                                 "You have access to tools that can run local commands (e.g. bash commands). "
                                 "If the user asks you to check repository status, files, find the current date/time, or execute any shell "
                                 "command (including curl), use the provided run_local_command tool to execute them instead of giving them instructions on how to run it themselves. "
                                 "IMPORTANT: Differentiate clearly between Pomodoro and general alarms. "
                                 "Only use the Pomodoro tool (start_pomodoro) if the user explicitly mentions the word 'pomodoro'. "
                                 "For all other general alarms, timers, or reminders (e.g., 'set an alarm/timer for 20 minutes', 'alarm at 10 AM', 'remind me in 1 hour'), "
                                 "you MUST use the 'create_alarm' tool instead.";
        switch (settings.provider) {
            case Provider::GROK:
                llm->add_instructions("You are Grok, an AI assistant created by xAI. You are helpful, harmless, and honest." + tool_instr);
                break;
            case Provider::OPENAI:
                llm->add_instructions("You are ChatGPT, a helpful AI assistant created by OpenAI." + tool_instr);
                break;
            case Provider::GROQ:
                llm->add_instructions("You are a helpful AI assistant powered by Groq's fast inference." + tool_instr);
                break;
            case Provider::GEMINI:
                // This case should not be reached as Gemini is handled above
                llm->add_instructions("You are Gemini, a helpful AI assistant created by Google." + tool_instr);
                break;
            case Provider::CUSTOM:
                llm->add_instructions("You are a helpful AI assistant." + tool_instr);
                break;
        }
        
        CONFIG_INFO_FMT("Created cppgpt LLM instance for provider: {}", provider_to_string(settings.provider));
        return LLMInstance{std::move(llm)};
        
    } catch (const std::exception& e) {
        CONFIG_ERROR_FMT("Failed to create LLM instance: {}", e.what());
        return std::nullopt;
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
