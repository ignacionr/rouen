#pragma once

#include "../interface/card.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/mcp_service.hpp"
#include <string>
#include <deque>
#include <future>
#include <optional>
#include <array>
#include <atomic>
#include <mutex>
#include <memory>

namespace rouen::cards {

    class ai_chat : public card {
    public:
        ai_chat(std::string_view initial_query = "");
        ~ai_chat() override = default;

        bool render() override;
        std::string get_uri() const override;

        static std::string get_assistant_name();
        static void maybe_speak_reply(const std::string& text);

    private:
        struct MessageCache {
            float bubble_height{0.0f};
            float content_width{0.0f};
            float text_width{0.0f};
            std::string child_id{};
            bool needs_recalc{true};
            
            MessageCache() = default;
            MessageCache(float h, float cw, float tw, std::string id)
                : bubble_height(h), content_width(cw), text_width(tw), 
                  child_id(std::move(id)), needs_recalc(false) {}
        };

        void populate_persona_buffers(size_t index);
        void render_llm_controls();
        void refresh_llm_config();
        
        void send_message(const std::string& message);
        void process_mcp_message(const std::string& message);
        void send_message_to_llm_with_functions(const std::string& message);
        void send_message_to_llm(const std::string& message);
        void process_pending_response();
        void recalculate_layout(float width_for_content, bool force_all = false);

        std::string execute_function_with_debug(const std::string& function_name, const std::string& args_json, int depth);
        std::string execute_persona_call(const std::string& function_name, const std::string& args_json, int depth);

        // Member variables
        std::string initial_query_{};
        std::string initial_message_{};
        
        std::optional<helpers::LLMConfig::LLMInstance> llm_instance_{};
        helpers::LLMConfig::LLMSettings current_llm_settings_{};
        bool llm_configured_{false};
        
        std::string input_text_{};
        std::array<char, 2048> input_buffer_{};
        
        std::deque<std::pair<std::string, std::string>> chat_history_{};
        std::deque<MessageCache> message_cache_{};
        
        std::optional<std::future<void>> pending_response_{};
        std::atomic<bool> waiting_for_response_{false};
        std::atomic<bool> clear_input_on_response_{false};
        std::mutex chat_history_mutex_;
        
        std::mutex dictation_mutex_;
        std::optional<std::string> pending_dictation_result_{std::nullopt};
        
        std::shared_ptr<helpers::mcp_service> mcp_service_{nullptr};
        
        std::atomic<bool> scroll_to_bottom_{false};
        bool reclaim_focus_{false};
        bool layout_dirty_{true};
        float last_width_{0.0f};
        
        http::fetch fetcher_{};
        
        static constexpr long ai_request_timeout_seconds_ = 180;
        bool debug_mode_{false};
        bool log_requests_{false};

        std::array<char, 128> persona_name_buf_{};
        std::array<char, 256> persona_desc_buf_{};
        std::array<char, 4096> persona_prompt_buf_{};
        size_t last_edited_persona_index_{static_cast<size_t>(-1)};
    };

} // namespace rouen::cards
