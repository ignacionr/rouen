#pragma once

#include "llm_config.hpp"
#include "cppgpt.hpp"
#include "gemini_adapter.hpp"
#include <memory>
#include <stdexcept>

namespace rouen::helpers {

    // Memory-safe wrapper for cppgpt
    class CppGptWrapper : public LLMConfig::LLMInstanceBase {
    private:
        std::unique_ptr<ignacionr::cppgpt> instance_;
        
    public:
        explicit CppGptWrapper(std::unique_ptr<ignacionr::cppgpt> instance)
            : instance_(std::move(instance)) {
            if (!instance_) {
                throw std::invalid_argument("Cannot create CppGptWrapper with null instance");
            }
        }
        
        void add_instructions(std::string_view instructions, std::string_view role = "system") override {
            if (!instance_) throw std::runtime_error("CppGpt instance is null");
            instance_->add_instructions(instructions, role);
        }
        
        void clear() override {
            if (!instance_) throw std::runtime_error("CppGpt instance is null");
            instance_->clear();
        }
        
        // Expose the templated sendMessage method directly
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
            if (!instance_) throw std::runtime_error("CppGpt instance is null");
            return instance_->sendMessage(message, std::forward<DoPostFunc>(do_post), role, model, search_mode, temperature, full_conversation);
        }
    };

    // Memory-safe wrapper for GeminiAdapter
    class GeminiWrapper : public LLMConfig::LLMInstanceBase {
    private:
        std::unique_ptr<GeminiAdapter> instance_;
        
    public:
        explicit GeminiWrapper(std::unique_ptr<GeminiAdapter> instance)
            : instance_(std::move(instance)) {
            if (!instance_) {
                throw std::invalid_argument("Cannot create GeminiWrapper with null instance");
            }
        }
        
        void add_instructions(std::string_view instructions, std::string_view role = "system") override {
            if (!instance_) throw std::runtime_error("Gemini instance is null");
            instance_->add_instructions(instructions, role);
        }
        
        void clear() override {
            if (!instance_) throw std::runtime_error("Gemini instance is null");
            instance_->clear();
        }
        
        // Expose the templated sendMessage method directly
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
            if (!instance_) throw std::runtime_error("Gemini instance is null");
            return instance_->sendMessage(message, std::forward<DoPostFunc>(do_post), role, model, search_mode, temperature, full_conversation);
        }
    };

} // namespace rouen::helpers
