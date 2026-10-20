#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "../helpers/glaze_include.hpp"

#include "llm_host.hpp"
#include "../helpers/config_service.hpp"
#include "../helpers/cppgpt.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/gemini_adapter.hpp"
#include "../helpers/persona_manager.hpp"
#include "../helpers/platform_utils.hpp"

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
        if (LLMConfigManager::instance().ensure_standard_configs()) {
            LLMConfigManager::instance().save_configs();
            entry = LLMConfigManager::instance().get_config(name);
        }
    }
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
        settings.thinking_level = entry->thinking_level;
        
        if (settings.api_key.empty()) {
            ensure_config_service();
            settings.api_key = config_service_->get_env_optional(get_api_key_env_name(settings.provider)).value_or("");
            if (settings.api_key.empty() && settings.provider == Provider::CUSTOM) {
                settings.api_key = "mlx-local";
            }
        }
        if (settings.base_url.empty()) {
            settings.base_url = get_base_url(settings.provider);
        }
        if (settings.model_name.empty()) {
            settings.model_name = get_default_model(settings.provider);
        }
    } else {
        ensure_config_service();
        std::string const provider_str = config_service_->get_env_optional("LLM_PROVIDER").value_or(
            !config_service_->get_env_optional("GEMINI_API_KEY").value_or("").empty() ? "gemini" : "grok"
        );
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
                settings.model_name = "gemini-3.8-flash";
                break;
            case Provider::CUSTOM:
                settings.api_key = config_service_->get_env_optional("LLM_CUSTOM_API_KEY").value_or(
                    config_service_->get_env_optional("LLM_API_KEY").value_or("mlx-local"));
                settings.base_url = config_service_->get_env_optional("LLM_CUSTOM_URL").value_or(
                    config_service_->get_env_optional("LLM_BASE_URL").value_or("http://localhost:8098/v1"));
                settings.model_name = config_service_->get_env_optional("LLM_CUSTOM_MODEL").value_or(
                    config_service_->get_env_optional("LLM_MODEL").value_or("mlx-community/Qwen3.5-9B-MLX-4bit"));
                break;
        }
    }
    
    settings.is_configured = !settings.api_key.empty() || settings.provider == Provider::CUSTOM;
    return settings;
}

std::optional<LLMHost::LLMInstance> LLMHost::create_llm_instance(const std::string& config_name) {
    auto settings = get_current_config(config_name);
    
    if (settings.api_key.empty()) {
        if (settings.provider == Provider::CUSTOM) {
            settings.api_key = "mlx-local";
        } else {
            // Intelligent fallback: try other standard profiles with active configured credentials
            std::vector<std::string> fallbacks = {"Gemini 3.8 Flash", "Gemini Flash Lite", "Gemini Flash", "Grok Default", "OpenAI GPT-4", "Local MLX"};
            for (const auto& f : fallbacks) {
                if (f == config_name) continue;
                auto fb_settings = get_current_config(f);
                if (!fb_settings.api_key.empty() || fb_settings.provider == Provider::CUSTOM) {
                    LOG_COMPONENT("LLMConfig", LOG_LEVEL_WARN, std::format("LLM config '{}' has empty API key; falling back to '{}'", config_name, f));
                    return create_llm_instance(f);
                }
            }
            LOG_COMPONENT("LLMConfig", LOG_LEVEL_WARN, "Cannot create LLM instance: API key is empty and no fallback available");
            return std::nullopt;
        }
    }
    
    try {
        if (settings.provider == Provider::GEMINI) {
            auto adapter = std::make_unique<rouen::helpers::GeminiAdapter>(settings.api_key, settings.model_name);
            std::string thinking = settings.thinking_level;
            if (thinking.empty()) {
                auto& pm = rouen::helpers::PersonaManager::instance();
                thinking = pm.get_active_persona().get_effective_thinking_level();
            }
            if (!thinking.empty()) {
                adapter->set_thinking_level(thinking);
            }
            return LLMInstance(std::move(adapter));
        }
        auto cppgpt = std::make_unique<ignacionr::cppgpt>(
            settings.api_key, 
            settings.base_url
        );
        return LLMInstance(std::move(cppgpt));
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
    }
    if (lower_provider == "openai") {
        return Provider::OPENAI;
    }
    if (lower_provider == "groq") {
        return Provider::GROQ;
    }
    if (lower_provider == "gemini") {
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
        case Provider::GEMINI: return "gemini-3.8-flash";
        case Provider::CUSTOM: return "mlx-community/Qwen3.5-9B-MLX-4bit";
    }
    return "grok-3-latest";
}

std::string LLMHost::get_base_url(Provider provider) {
    switch (provider) {
        case Provider::GROK: return "https://api.x.ai/v1";
        case Provider::OPENAI: return "https://api.openai.com/v1";
        case Provider::GROQ: return "https://api.groq.com/openai/v1";
        case Provider::GEMINI: return "https://generativelanguage.googleapis.com";
        case Provider::CUSTOM: return "http://localhost:8098/v1";
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
    registrar::add<std::function<void()>>("llm_config_reload", std::make_shared<std::function<void()>>([this]() {
        this->reload();
    }));
    registrar::add<std::function<bool(std::string_view)>>("llm_config_merge", std::make_shared<std::function<bool(std::string_view)>>([this](std::string_view json_content) {
        return this->merge_from_json(json_content);
    }));

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
    ensure_standard_configs();
    save_configs();
}

bool LLMConfigManager::ensure_standard_configs() {
    auto config_service = rouen::helpers::ConfigService::instance();
    bool updated = false;

    std::string gemini_key = config_service ? config_service->get_env_optional("GEMINI_API_KEY").value_or("") : "";
    if (gemini_key.empty()) {
        for (const auto& cfg : configs_) {
            if (cfg.provider == "gemini" && !cfg.api_key.empty()) {
                gemini_key = cfg.api_key;
                break;
            }
        }
    }

    bool has_mlx = false;
    bool has_gemini_argon = false;
    bool has_gemini_38_flash = false;
    bool has_gemini_pro_tools = false;
    bool has_gemini_flash_lite = false;
    bool has_grok = false;
    bool has_openai = false;

    for (auto& cfg : configs_) {
        if (cfg.name == "Local MLX") {
            has_mlx = true;
            if (cfg.model_name != "mlx-community/Qwen3.5-9B-MLX-4bit") {
                cfg.model_name = "mlx-community/Qwen3.5-9B-MLX-4bit";
                updated = true;
            }
            if (cfg.api_key.empty()) {
                cfg.api_key = "mlx-local";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "http://localhost:8098/v1";
                updated = true;
            }
        } else if (cfg.name == "Gemini 4 Argon") {
            has_gemini_argon = true;
            if (cfg.model_name != "gemini-4-argon") {
                cfg.model_name = "gemini-4-argon";
                updated = true;
            }
            if (cfg.thinking_level != "high") {
                cfg.thinking_level = "high";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "https://generativelanguage.googleapis.com";
                updated = true;
            }
            if (cfg.api_key.empty() && !gemini_key.empty()) {
                cfg.api_key = gemini_key;
                updated = true;
            }
        } else if (cfg.name == "Gemini 3.8 Flash") {
            has_gemini_38_flash = true;
            if (cfg.model_name != "gemini-3.8-flash") {
                cfg.model_name = "gemini-3.8-flash";
                updated = true;
            }
            if (cfg.thinking_level != "high") {
                cfg.thinking_level = "high";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "https://generativelanguage.googleapis.com";
                updated = true;
            }
            if (cfg.api_key.empty() && !gemini_key.empty()) {
                cfg.api_key = gemini_key;
                updated = true;
            }
        } else if (cfg.name == "Gemini Pro Custom Tools") {
            has_gemini_pro_tools = true;
            if (cfg.model_name != "gemini-3.1-pro-preview-customtools") {
                cfg.model_name = "gemini-3.1-pro-preview-customtools";
                updated = true;
            }
            if (cfg.thinking_level != "high") {
                cfg.thinking_level = "high";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "https://generativelanguage.googleapis.com";
                updated = true;
            }
            if (cfg.api_key.empty() && !gemini_key.empty()) {
                cfg.api_key = gemini_key;
                updated = true;
            }
        } else if (cfg.name == "Gemini Flash Lite") {
            has_gemini_flash_lite = true;
            if (cfg.model_name != "gemini-3.5-flash-lite") {
                cfg.model_name = "gemini-3.5-flash-lite";
                updated = true;
            }
            if (cfg.thinking_level != "minimal") {
                cfg.thinking_level = "minimal";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "https://generativelanguage.googleapis.com";
                updated = true;
            }
            if (cfg.api_key.empty() && !gemini_key.empty()) {
                cfg.api_key = gemini_key;
                updated = true;
            }
        } else if (cfg.name == "Gemini Flash") {
            if (cfg.model_name != "gemini-3.8-flash") {
                cfg.model_name = "gemini-3.8-flash";
                updated = true;
            }
            if (cfg.base_url.empty()) {
                cfg.base_url = "https://generativelanguage.googleapis.com";
                updated = true;
            }
            if (cfg.api_key.empty() && !gemini_key.empty()) {
                cfg.api_key = gemini_key;
                updated = true;
            }
        } else if (cfg.name == "Grok Default") {
            has_grok = true;
            if (cfg.api_key.empty() && config_service) {
                auto env_key = config_service->get_env_optional("GROK_API_KEY").value_or("");
                if (!env_key.empty()) {
                    cfg.api_key = env_key;
                    updated = true;
                }
            }
        } else if (cfg.name == "OpenAI GPT-4") {
            has_openai = true;
            if (cfg.api_key.empty() && config_service) {
                auto env_key = config_service->get_env_optional("OPENAI_API_KEY").value_or("");
                if (!env_key.empty()) {
                    cfg.api_key = env_key;
                    updated = true;
                }
            }
        }
    }

    if (!has_gemini_argon) {
        LLMConfigEntry argon_entry{
            .name = "Gemini 4 Argon",
            .provider = "gemini",
            .api_key = gemini_key,
            .base_url = "https://generativelanguage.googleapis.com",
            .model_name = "gemini-4-argon",
            .thinking_level = "high"
        };
        configs_.push_back(argon_entry);
        updated = true;
    }

    if (!has_gemini_38_flash) {
        LLMConfigEntry flash_entry{
            .name = "Gemini 3.8 Flash",
            .provider = "gemini",
            .api_key = gemini_key,
            .base_url = "https://generativelanguage.googleapis.com",
            .model_name = "gemini-3.8-flash",
            .thinking_level = "high"
        };
        configs_.push_back(flash_entry);
        updated = true;
    }

    if (!has_gemini_pro_tools) {
        LLMConfigEntry pro_tools_entry{
            .name = "Gemini Pro Custom Tools",
            .provider = "gemini",
            .api_key = gemini_key,
            .base_url = "https://generativelanguage.googleapis.com",
            .model_name = "gemini-3.1-pro-preview-customtools",
            .thinking_level = "high"
        };
        configs_.push_back(pro_tools_entry);
        updated = true;
    }

    if (!has_gemini_flash_lite) {
        LLMConfigEntry lite_entry{
            .name = "Gemini Flash Lite",
            .provider = "gemini",
            .api_key = gemini_key,
            .base_url = "https://generativelanguage.googleapis.com",
            .model_name = "gemini-3.5-flash-lite",
            .thinking_level = "minimal"
        };
        configs_.push_back(lite_entry);
        updated = true;
    }

    if (!has_mlx) {
        LLMConfigEntry local_mlx_entry;
        local_mlx_entry.name = "Local MLX";
        local_mlx_entry.provider = "custom";
        local_mlx_entry.base_url = "http://localhost:8098/v1";
        local_mlx_entry.model_name = "mlx-community/Qwen3.5-9B-MLX-4bit";
        local_mlx_entry.api_key = "mlx-local";
        configs_.push_back(local_mlx_entry);
        updated = true;
    }

    if (!has_grok) {
        LLMConfigEntry grok_entry;
        grok_entry.name = "Grok Default";
        grok_entry.provider = "grok";
        grok_entry.model_name = "grok-3-latest";
        grok_entry.base_url = "https://api.x.ai/v1";
        grok_entry.api_key = config_service ? config_service->get_env_optional("GROK_API_KEY").value_or("") : "";
        configs_.push_back(grok_entry);
        updated = true;
    }

    if (!has_openai) {
        LLMConfigEntry openai_entry;
        openai_entry.name = "OpenAI GPT-4";
        openai_entry.provider = "openai";
        openai_entry.model_name = "gpt-4";
        openai_entry.base_url = "https://api.openai.com/v1";
        openai_entry.api_key = config_service ? config_service->get_env_optional("OPENAI_API_KEY").value_or("") : "";
        configs_.push_back(openai_entry);
        updated = true;
    }

    // Ensure default config name is valid and points to an existing entry
    if (default_config_name_.empty() || !get_config(default_config_name_) || default_config_name_ == "Local MLX" || default_config_name_ == "Default" || default_config_name_ == "Gemini Flash") {
        default_config_name_ = "Gemini 3.8 Flash";
        updated = true;
    }

    return updated;
}

void LLMConfigManager::load_configs() {
    is_sync_suppressed_ = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{is_sync_suppressed_};

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
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        LLMConfigSaveModel save_model;
        auto err = glz::read_json(save_model, content);
        if (err) {
            LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, "Failed to parse llm_configs.json");
            setup_default_configs();
            return;
        }

        configs_ = save_model.configs;
        default_config_name_ = save_model.default_config_name;
        
        bool updated = ensure_standard_configs();
        if (updated) {
            save_configs();
        }

        if (configs_.empty()) {
            setup_default_configs();
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, std::string("Error loading LLM configs: ") + e.what());
        setup_default_configs();
    }
}

bool LLMConfigManager::merge_from_json(std::string_view json_content) {
    if (json_content.empty()) {
        return false;
    }

    is_sync_suppressed_ = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{is_sync_suppressed_};

    try {
        LLMConfigSaveModel remote_model;
        auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(remote_model, json_content);
        if (err) {
            LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, "Failed to parse remote llm_configs.json for merge");
            return false;
        }

        for (const auto& remote_cfg : remote_model.configs) {
            if (remote_cfg.name.empty()) continue;

            LLMConfigEntry* existing = nullptr;
            for (auto& local_cfg : configs_) {
                if (local_cfg.name == remote_cfg.name) {
                    existing = &local_cfg;
                    break;
                }
            }

            if (existing) {
                if (!remote_cfg.provider.empty()) existing->provider = remote_cfg.provider;
                if (!remote_cfg.base_url.empty()) existing->base_url = remote_cfg.base_url;
                if (!remote_cfg.model_name.empty()) existing->model_name = remote_cfg.model_name;
                if (!remote_cfg.thinking_level.empty()) existing->thinking_level = remote_cfg.thinking_level;
                // Preserve local non-empty API key when remote key is empty
                if (!remote_cfg.api_key.empty()) {
                    existing->api_key = remote_cfg.api_key;
                }
            } else {
                configs_.push_back(remote_cfg);
            }
        }

        if (!remote_model.default_config_name.empty() && get_config(remote_model.default_config_name)) {
            default_config_name_ = remote_model.default_config_name;
        }

        ensure_standard_configs();
        save_configs();
        return true;
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, std::string("Error merging LLM configs: ") + e.what());
        return false;
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

        if (!is_sync_suppressed_) {
            if (sync_hook_) {
                sync_hook_("config", "llm_configs.json", json_str, false);
            } else {
                auto hook_fn = registrar::try_get<std::function<bool(std::string_view, std::string_view, std::string_view, bool)>>("universal_sync_item");
                if (hook_fn) {
                    (*hook_fn)("config", "llm_configs.json", json_str, false);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT("LLMConfigManager", LOG_LEVEL_ERROR, std::string("Error saving LLM configs: ") + e.what());
    }
}

} // namespace rouen::hosts
