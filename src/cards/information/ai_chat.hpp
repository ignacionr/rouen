#pragma once

#include "../../helpers/imgui_include.hpp"
#include <string>
#include <deque>
#include <future>
#include <optional>
#include <array>
#include <atomic>
#include <typeinfo>
#include <format>
#include <memory>
#include <mutex>
#include <variant>

#include "../../helpers/cppgpt.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/api_keys.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/mcp_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/notify_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {
    class ai_chat : public card {
    public:
        ai_chat(std::string_view initial_query = "") {
            if (!initial_query.empty()) {
                initial_query_ = initial_query;
                initial_message_ = initial_query;
            }
            name("AI Chat");
            
            // Base colors (already in vector)
            colors[0] = ImVec4(0.08f, 0.11f, 0.16f, 1.0f);      // window background
            colors[1] = ImVec4(0.11f, 0.16f, 0.23f, 1.0f);      // secondary elements
            
            // Additional UI colors (add to the colors vector)
            get_color(2, ImVec4(0.18f, 0.30f, 0.44f, 1.0f));  // User message background
            get_color(3, ImVec4(0.12f, 0.18f, 0.28f, 1.0f));  // Assistant message background
            get_color(4, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));     // User message text
            get_color(5, ImVec4(0.94f, 0.97f, 1.0f, 1.0f));   // Assistant message text
            get_color(6, ImVec4(0.85f, 0.90f, 0.97f, 1.0f));  // Thinking indicator
            get_color(7, ImVec4(0.10f, 0.14f, 0.20f, 1.0f));  // Input field background
            get_color(8, ImVec4(0.24f, 0.39f, 0.59f, 1.0f));  // Button color
            get_color(9, ImVec4(0.30f, 0.49f, 0.72f, 1.0f));  // Button hover
            get_color(10, ImVec4(0.36f, 0.56f, 0.80f, 1.0f)); // Button active
            get_color(11, ImVec4(0.45f, 0.54f, 0.66f, 0.9f)); // Separator line color
            get_color(12, ImVec4(0.06f, 0.09f, 0.13f, 1.0f)); // Chat background
            get_color(13, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));   // MCP function call indicator
            
            requested_fps = 10; // Higher FPS for responsive input
            
            // Initialize LLM configuration
            refresh_llm_config();
            width *= 2.0f;
            
            // Get MCP service instance
            mcp_service_ = registrar::get<helpers::mcp_service>("mcp_service");
        }

        void render_llm_controls() {
            // Provider information
            auto settings = helpers::LLMConfig::get_current_config();
            ImGui::Text("Provider: %s", helpers::LLMConfig::provider_to_string(settings.provider).c_str());
            ImGui::SameLine();
            if (ImGui::Button("Configure")) {
                // This would open the settings card or configuration dialog
                "create_card"_sfn("settings");
            }
            
            // Configuration status
            ImGui::SameLine();
            if (settings.is_configured) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green
                ImGui::Text("✓ Configured");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red
                ImGui::Text("✗ Not Configured");
                ImGui::PopStyleColor();
            }
            
            // Model name
            if (settings.is_configured) {
                ImGui::Text("Model: %s", settings.model_name.c_str());
            }
            
            // whether to allow search (only for providers that support it)
            if (settings.provider == helpers::LLMConfig::Provider::GROK || 
                settings.provider == helpers::LLMConfig::Provider::GEMINI) {
                ImGui::Checkbox("Allow Search", &allow_search_);
                ImGui::SameLine();
            }
            
            // temperature slider
            ImGui::SliderFloat("Temperature", &temperature_, 0.0f, 1.0f);
            
            bool speak_replies = notify_service::spoken_notifications_enabled();
            if (ImGui::Checkbox("Speak replies", &speak_replies)) {
                notify_service::set_spoken_notifications_enabled(speak_replies);
            }
        }

        bool render() override {
            if (!initial_message_.empty()) {
                std::string msg = std::move(initial_message_);
                initial_message_.clear();
                send_message(msg);
            }
            
            // Periodically refresh LLM configuration
            static float last_config_refresh = 0.0f;
            float current_time = static_cast<float>(ImGui::GetTime());
            if (current_time - last_config_refresh > 2.0f) {
                refresh_llm_config();
                last_config_refresh = current_time;
            }
            
            return render_window([this]() {
                render_llm_controls();
                if (clear_input_on_response_.exchange(false)) {
                    input_text_.clear();
                    input_buffer_.fill('\0');
                    reclaim_focus_ = true;
                }
                // Apply custom colors to various UI elements
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertFloat4ToU32(colors[12]));
                ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertFloat4ToU32(colors[11]));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertFloat4ToU32(colors[7]));
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertFloat4ToU32(colors[8]));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertFloat4ToU32(colors[9]));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertFloat4ToU32(colors[10]));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4])); // Default text color (user_text_color)
                ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.4f, 0.6f, 0.5f))); // Text selection color
                
                // Calculate required space for the footer area
                const float thinking_indicator_height = waiting_for_response_.load() ? ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
                const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
                const float footer_height_to_reserve = thinking_indicator_height + input_height + ImGui::GetStyle().ItemSpacing.y;
                
                // Chat area
                // Begin child with no horizontal scrollbar (use 0 width to auto-size without horizontal scroll)
                if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), true)) {
                    // Process any pending responses
                    process_pending_response();
                    
                    // Check if layout needs to be recalculated
                    float current_width = ImGui::GetContentRegionAvail().x;
                    if (layout_dirty_ || std::abs(last_width_ - current_width) > 1.0f) {
                        recalculate_layout(current_width);
                        last_width_ = current_width;
                        layout_dirty_ = false;
                    }
                    
                    // Pre-calculate common values
                    const ImVec2 padding(10.0f, 8.0f);
                    const float bubble_rounding = 5.0f;
                    
                    // Derive readable text colors from current bubble backgrounds.
                    const ImVec4 raw_user_bg = get_color(0);
                    const ImVec4 raw_assistant_bg = get_color(1);
                    const ImVec4 chat_bg = get_color(12);
                    
                    const ImVec4 user_bg = ImVec4(raw_user_bg.x, raw_user_bg.y, raw_user_bg.z, 0.85f);
                    const ImVec4 assistant_bg = ImVec4(raw_assistant_bg.x, raw_assistant_bg.y, raw_assistant_bg.z, 0.15f);
                    
                    const ImU32 user_bg_color = ImGui::ColorConvertFloat4ToU32(user_bg);
                    const ImU32 assistant_bg_color = ImGui::ColorConvertFloat4ToU32(assistant_bg);
                    
                    auto pick_text_color = [](const ImVec4& bg) -> ImU32 {
                        const float bg_luma = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
                        const ImVec4 dark_text{0.05f, 0.06f, 0.08f, 1.0f};
                        const ImVec4 light_text{0.96f, 0.97f, 0.99f, 1.0f};
                        const float dark_luma = 0.299f * dark_text.x + 0.587f * dark_text.y + 0.114f * dark_text.z;
                        const float light_luma = 0.299f * light_text.x + 0.587f * light_text.y + 0.114f * light_text.z;
                        const float contrast_dark = (std::max(bg_luma, dark_luma) + 0.05f) / (std::min(bg_luma, dark_luma) + 0.05f);
                        const float contrast_light = (std::max(bg_luma, light_luma) + 0.05f) / (std::min(bg_luma, light_luma) + 0.05f);
                        return ImGui::ColorConvertFloat4ToU32(contrast_dark > contrast_light ? dark_text : light_text);
                    };
                    
                    auto blend_colors = [](const ImVec4& src, const ImVec4& dst) -> ImVec4 {
                        return ImVec4(
                            src.w * src.x + (1.0f - src.w) * dst.x,
                            src.w * src.y + (1.0f - src.w) * dst.y,
                            src.w * src.z + (1.0f - src.w) * dst.z,
                            1.0f
                        );
                    };
                    
                    const ImU32 user_text_color = pick_text_color(blend_colors(user_bg, chat_bg));
                    const ImU32 assistant_text_color = pick_text_color(blend_colors(assistant_bg, chat_bg));
                    
                    // Display chat history using cached values
                    // Create a safe snapshot of the chat history for rendering
                    std::vector<std::pair<std::string, std::string>> chat_snapshot;
                    std::vector<MessageCache> cache_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(chat_history_mutex_);
                        chat_snapshot.reserve(chat_history_.size());
                        cache_snapshot.reserve(message_cache_.size());
                        
                        // Copy the data safely
                        chat_snapshot.assign(chat_history_.begin(), chat_history_.end());
                        cache_snapshot.assign(message_cache_.begin(), message_cache_.end());
                    }
                    
                    for (size_t i = 0; i < chat_snapshot.size() && i < cache_snapshot.size(); ++i) {
                        const auto& message = chat_snapshot[i];
                        const auto& cache = cache_snapshot[i];
                        bool is_user = message.first == "user";
                        
                        // Set background color for message bubbles
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, is_user ? user_bg_color : assistant_bg_color);
                        
                        // Position user messages to the right
                        if (is_user) {
                            float available_width = ImGui::GetContentRegionAvail().x;
                            ImGui::SetCursorPosX(available_width - cache.content_width - 10.0f);
                        }
                        
                        // Create message bubble with cached size
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, bubble_rounding);
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
                        
                        // Use pre-calculated child ID
                        ImGui::BeginChild(cache.child_id.c_str(), 
                            ImVec2(cache.content_width, cache.bubble_height), true, 
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize);
                        
                        // Set text color based on sender
                        ImGui::PushStyleColor(ImGuiCol_Text, is_user ? user_text_color : assistant_text_color);
                        
                        // Display sender name
                        std::string sender_name = is_user ? "You" : get_assistant_name();
                        ImGui::Text("%s", sender_name.c_str());
                        
                        // Copy button aligned to the right inside the balloon
                        ImGui::SameLine();
                        float copy_btn_pos_x = cache.content_width - padding.x * 2.0f - 18.0f;
                        if (copy_btn_pos_x > ImGui::GetCursorPosX()) {
                            ImGui::SetCursorPosX(copy_btn_pos_x);
                        }
                        
                        // Transparent/subtle styling for the copy icon button
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
                        ImGui::PushStyleColor(ImGuiCol_Text, is_user ? user_text_color : assistant_text_color);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                        
                        std::string copy_btn_id = std::format("{}##copy_{}", ICON_MD_CONTENT_COPY, i);
                        if (ImGui::Button(copy_btn_id.c_str())) {
                            ImGui::SetClipboardText(message.second.c_str());
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Copy message text");
                        }
                        
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(4);
                        
                        ImGui::Separator();
                        
                        // Display message content with proper text wrapping
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cache.text_width);
                        ImGui::TextWrapped("%s", message.second.c_str());
                        ImGui::PopTextWrapPos();
                        
                        // Add right-click context menu for copying message
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                            ImGui::OpenPopup(std::format("MessageContextMenu_{}", i).c_str());
                        }
                        
                        // Context menu for message
                        if (ImGui::BeginPopup(std::format("MessageContextMenu_{}", i).c_str())) {
                            if (ImGui::MenuItem("Copy Message")) {
                                ImGui::SetClipboardText(message.second.c_str());
                            }
                            
                            if (ImGui::MenuItem("Copy with Sender")) {
                                std::string full_message = std::format("{}: {}", sender_name, message.second);
                                ImGui::SetClipboardText(full_message.c_str());
                            }
                            
                            ImGui::EndPopup();
                        }
                        
                        // End styling
                        ImGui::PopStyleColor(); // Text color
                        ImGui::EndChild();
                        
                        ImGui::PopStyleVar(3); // Pop the 3 style vars we pushed
                        ImGui::PopStyleColor(); // Pop the child bg color
                        
                        // Add consistent spacing between messages
                        ImGui::Spacing();
                        ImGui::Spacing();
                    }
                    
                    if (scroll_to_bottom_.load()) {
                        ImGui::SetScrollHereY(1.0f);
                        scroll_to_bottom_.store(false);
                    }
                }
                ImGui::EndChild();
                
                // Show a loading indicator if waiting for response
                // Place the indicator outside the scrolling region
                if (waiting_for_response_.load()) {
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    // Create a subtle "thinking" bubble
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertFloat4ToU32(colors[3]));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
                    
                    ImGui::BeginChild("thinking_indicator", ImVec2(150, 40), true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[6]));
                    
                    // Animated thinking dots - pre-computed strings for performance
                    static const std::array<const char*, 4> thinking_frames = {
                        "AI is thinking",
                        "AI is thinking.",
                        "AI is thinking..",
                        "AI is thinking..."
                    };
                    
                    float time = static_cast<float>(ImGui::GetTime());
                    int dots = (static_cast<int>(time * 2) % 4);
                    ImGui::Text("%s", thinking_frames[static_cast<size_t>(dots)]);
                    
                    ImGui::PopStyleColor();
                    ImGui::EndChild();
                    
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }
                
                // API key input if not configured
                if (!llm_configured_) {
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4]));
                    ImGui::TextWrapped("LLM not configured. Please configure your LLM provider in Settings.");
                    ImGui::PopStyleColor();
                    
                    if (ImGui::Button("Open Settings")) {
                        "create_card"_sfn("settings");
                    }
                }
                
                // Input area
                ImGui::Separator();
                reclaim_focus_ = false;
                
                // Only allow input if LLM is configured and not waiting for a response
                ImGui::BeginDisabled(!llm_configured_ || waiting_for_response_.load());
                
                // Copy current input to buffer with bounds checking
                const size_t copy_len = std::min(input_text_.length(), input_buffer_.size() - 1);
                std::strncpy(input_buffer_.data(), input_text_.c_str(), copy_len);
                input_buffer_[copy_len] = '\0';
                
                // Calculate space for the Send button to avoid it being cut off
                const float send_button_width = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
                
                // Focus on the input field initially
                if (llm_configured_ && !waiting_for_response_.load() && ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                
                ImGui::PushItemWidth(-send_button_width - ImGui::GetStyle().ItemSpacing.x);
                if (reclaim_focus_) {
                    ImGui::SetKeyboardFocusHere();
                    ImGui::SetItemDefaultFocus();
                    reclaim_focus_ = false;
                    input_buffer_.fill('\0'); // Clear the buffer
                }
                if (ImGui::InputText("##input", input_buffer_.data(), input_buffer_.size(), 
                    ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_NoHorizontalScroll)) {
                    input_text_ = input_buffer_.data();
                    send_message(input_text_);
                    input_text_.clear();
                    reclaim_focus_ = true;
                }
                ImGui::PopItemWidth();
                
                ImGui::SameLine();
                if (ImGui::Button("Send", ImVec2(send_button_width, 0)) && !input_text_.empty()) {
                    send_message(input_text_);
                    input_text_.clear();
                    reclaim_focus_ = true;
                }
                
                ImGui::EndDisabled();
                
                // Update input_text from buffer if it changed
                std::string new_input_text = input_buffer_.data();
                if (new_input_text != input_text_) {
                    input_text_ = std::move(new_input_text);
                }
                
                ImGui::PopStyleColor(8); // Pop all the colors we pushed at the beginning
            });
        }
        
        std::string get_uri() const override {
            if (!initial_query_.empty()) {
                return std::format("ai-chat:{}", ::helpers::StringHelper::url_encode(initial_query_));
            }
            return "ai-chat";
        }


        
        void maybe_speak_reply(const std::string& text) const {
            if (text.empty()) {
                return;
            }
            if (notify_service::spoken_notifications_enabled()) {
                rouen::platform::speak_text_async(text);
            }
        }

        // Async request context for memory-safe operations
        struct AsyncRequestContext {
            std::string user_message;
            helpers::LLMConfig::LLMSettings llm_settings;
            std::vector<std::pair<std::string, std::string>> conversation_snapshot;
            std::shared_ptr<http::fetch> fetcher;
            helpers::LLMConfig::LLMInstance llm_instance_copy;
            bool allow_search{false};
            float temperature{0.45f};
            
            // Ensure proper lifetime management
            AsyncRequestContext() = default;
            ~AsyncRequestContext() = default;
            
            // Non-copyable to avoid accidental copies
            AsyncRequestContext(const AsyncRequestContext&) = delete;
            AsyncRequestContext& operator=(const AsyncRequestContext&) = delete;
            
            // Movable for efficient transfer
            AsyncRequestContext(AsyncRequestContext&&) = default;
            AsyncRequestContext& operator=(AsyncRequestContext&&) = default;
        };
        
        struct MessageCache {
            float bubble_height{0.0f};
            float content_width{0.0f};
            float text_width{0.0f};
            std::string child_id{};
            bool needs_recalc{true};
            
            // Constructor for better initialization
            MessageCache() = default;
            MessageCache(float h, float cw, float tw, std::string id)
                : bubble_height(h), content_width(cw), text_width(tw), 
                  child_id(std::move(id)), needs_recalc(false) {}
        };
        
        std::string initial_query_{};
        std::string initial_message_{};
        
        // Use the new memory-safe LLM instance wrapper
        std::optional<helpers::LLMConfig::LLMInstance> llm_instance_{};
        helpers::LLMConfig::LLMSettings current_llm_settings_{};
        bool llm_configured_{false};
        
        // String management with better memory safety
        std::string input_text_{};
        std::array<char, 2048> input_buffer_{};
        
        // Conversation management with reserve capacity
        std::deque<std::pair<std::string, std::string>> chat_history_{};
        std::deque<MessageCache> message_cache_{};
        
        // Async operation management
        std::optional<std::future<void>> pending_response_{};
        std::atomic<bool> waiting_for_response_{false};
        std::atomic<bool> clear_input_on_response_{false};
        std::mutex chat_history_mutex_; // Protect chat_history_ and message_cache_ from concurrent access
        
        // MCP service for function calling
        std::shared_ptr<helpers::mcp_service> mcp_service_{nullptr};
        
        // UI state management
        std::atomic<bool> scroll_to_bottom_{false};
        bool reclaim_focus_{false};
        bool layout_dirty_{true};
        float last_width_{0.0f};
        
        // Network client - initialized once
        http::fetch fetcher_{};
        
        // Configuration settings
        static constexpr long ai_request_timeout_seconds_ = 180;
        bool allow_search_{false};
        float temperature_{0.45f};
        
        void refresh_llm_config() {
            current_llm_settings_ = helpers::LLMConfig::get_current_config();
            llm_configured_ = current_llm_settings_.is_configured;
            
            if (llm_configured_) {
                llm_instance_ = helpers::LLMConfig::create_llm_instance();
            } else {
                llm_instance_.reset();
            }
            
            // Update card name based on provider
            if (llm_configured_) {
                std::string provider_name = helpers::LLMConfig::provider_to_string(current_llm_settings_.provider);
                std::transform(provider_name.begin(), provider_name.end(), provider_name.begin(), ::toupper);
                name("AI Chat (" + provider_name + ")");
            } else {
                name("AI Chat (Not Configured)");
            }
        }
        
        std::string get_assistant_name() const {
            if (!llm_configured_) return "AI";
            
            switch (current_llm_settings_.provider) {
                case helpers::LLMConfig::Provider::GROK: return "Grok";
                case helpers::LLMConfig::Provider::OPENAI: return "ChatGPT";
                case helpers::LLMConfig::Provider::GROQ: return "Groq";
                case helpers::LLMConfig::Provider::GEMINI: return "Gemini";
                case helpers::LLMConfig::Provider::CUSTOM: return "AI";
                default: return "AI";
            }
        }
        
        void send_message(const std::string& message) {
            if (message.empty() || waiting_for_response_.load() || !llm_configured_ || !llm_instance_) {
                return;
            }
            
            // Process message with MCP functions first
            process_mcp_message(message);
        }
        
    private:
        void process_mcp_message(const std::string& message) {
            DEBUG_DEBUG("AI Chat: Processing message: '" + message + "'");
            
            // For function calling, we send the message directly to the LLM with function schemas
            // and let Gemini decide when to call functions
            send_message_to_llm_with_functions(message);
        }
        
        void send_message_to_llm_with_functions(const std::string& message) {
            if (message.empty() || waiting_for_response_.load() || !llm_configured_ || !llm_instance_) {
                return;
            }

            // Get available MCP functions and convert to Gemini function schemas
            std::vector<std::string> function_schemas;
            if (mcp_service_) {
                try {
                    auto functions = mcp_service_->get_available_functions();
                    for (const auto& func : functions) {
                        std::string raw_schema = std::format(
                            "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                            func.name, 
                            func.description.empty() ? "Repository operation" : func.description,
                            func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
                        );
                        
                        // Strip out control characters like newlines and tabs to ensure a clean JSON string
                        std::string schema;
                        schema.reserve(raw_schema.size());
                        for (char c : raw_schema) {
                            if (c != '\n' && c != '\r' && c != '\t') {
                                schema.push_back(c);
                            }
                        }
                        
                        function_schemas.push_back(schema);
                        DEBUG_DEBUG("AI Chat: Added function schema for: " + func.name);
                    }
                } catch (const std::exception& e) {
                    DEBUG_ERROR("AI Chat: Error getting MCP functions: " + std::string(e.what()));
                }
            }
            
            DEBUG_DEBUG("AI Chat: Sending message with " + std::to_string(function_schemas.size()) + " function schemas");
            
            // Pre-allocate space to avoid reallocations during conversation growth
            if (chat_history_.size() == chat_history_.max_size() - 2) {
                // Remove oldest messages if we're approaching container limits
                chat_history_.pop_front();
                message_cache_.pop_front();
            }

            try {
                // Add user message to history with move semantics
                chat_history_.emplace_back("user", message);
                
                // Add a new cache entry for the user message
                message_cache_.emplace_back();
                
                waiting_for_response_.store(true);
                scroll_to_bottom_.store(true);
                
                // Determine model and search mode before launching the thread (thread-safe capture)
                std::string model_name = current_llm_settings_.model_name;
                std::string search_mode_str;
                if ((current_llm_settings_.provider == helpers::LLMConfig::Provider::GROK || 
                     current_llm_settings_.provider == helpers::LLMConfig::Provider::GEMINI) && allow_search_) {
                    search_mode_str = "on";
                }
                
                // Launch response generation asynchronously to prevent UI blocking
                std::thread([this, function_schemas, message, model_name, search_mode_str]() {
                    try {
                        // Create a private local LLM instance for this request to ensure thread safety
                        auto local_llm_opt = helpers::LLMConfig::create_llm_instance();
                        if (!local_llm_opt) {
                            throw std::runtime_error("LLM configuration is incomplete");
                        }
                        auto& local_llm = *local_llm_opt;
                        
                        // Inject current local date and time so the model knows what "today" is
                        auto now = std::chrono::system_clock::now();
                        auto now_time_t = std::chrono::system_clock::to_time_t(now);
                        std::tm now_tm = *std::localtime(&now_time_t);
                        char time_buf[64];
                        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &now_tm);
                        std::string time_instr = std::format("The current local date and time is: {}. Use this to understand relative dates like 'today', 'tomorrow', 'yesterday', 'this week', etc.", std::string(time_buf));
                        local_llm.add_instructions(time_instr);

                        // Create conversion from our message format to the format expected by sendMessage with mutex protection
                        std::vector<std::pair<std::string, std::string>> conversation_for_llm;
                        {
                            std::lock_guard<std::mutex> lock(chat_history_mutex_);
                            for (const auto& chat_msg : chat_history_) {
                                conversation_for_llm.emplace_back(chat_msg.first, chat_msg.second);
                            }
                        }
                        
                        // Try to use function calling if we have a Gemini adapter directly
                        auto fetcher = std::make_shared<http::fetch>(ai_request_timeout_seconds_);
                        fetcher->set_max_retries(3);
                        auto chat_completion = std::visit([&](auto& adapter_ptr) -> ignacionr::ChatCompletion {
                            return adapter_ptr->sendMessageWithFunctionCalling(
                                message,
                                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                                    return fetcher->post(url, body, header_setter);
                                },
                                [this](const std::string& function_name, const std::string& args_json) -> std::string {
                                    // Execute MCP function
                                    if (mcp_service_) {
                                        try {
                                            auto result = mcp_service_->execute_function(function_name, args_json);
                                            return result.success ? result.result : "Error: " + result.error_message;
                                        } catch (const std::exception& e) {
                                            return "Error executing function: " + std::string(e.what());
                                        }
                                    }
                                    return "Error: MCP service not available";
                                },
                                "user", model_name, search_mode_str, 0.45f, &conversation_for_llm, &function_schemas
                            );
                        }, local_llm.instance_);
                        
                        // Process the response
                        if (!chat_completion.choices.empty()) {
                            const auto& response = chat_completion.choices[0].message.content;
                            
                            // Add AI response to history with mutex protection
                            {
                                std::lock_guard<std::mutex> lock(chat_history_mutex_);
                                chat_history_.emplace_back("assistant", response);
                                message_cache_.emplace_back();
                            }
                            maybe_speak_reply(response);
                        }
                        
                    } catch (const std::exception& e) {
                        // Add error message to chat history with mutex protection
                        std::string error_msg = "Error: " + std::string(e.what());
                        {
                            std::lock_guard<std::mutex> lock(chat_history_mutex_);
                            chat_history_.emplace_back("assistant", error_msg);
                            message_cache_.emplace_back();
                        }
                    }
                    
                    waiting_for_response_.store(false);
                    clear_input_on_response_.store(true);
                    scroll_to_bottom_.store(true);
                }).detach();
                
            } catch (const std::exception& e) {
                waiting_for_response_.store(false);
                std::string error_msg = "Failed to send message: " + std::string(e.what());
                chat_history_.emplace_back("assistant", error_msg);
                message_cache_.emplace_back();
            }
        }        void send_message_to_llm(const std::string& message) {
            if (message.empty() || waiting_for_response_.load() || !llm_configured_ || !llm_instance_) {
                return;
            }
            
            // Pre-allocate space to avoid reallocations during conversation growth
            if (chat_history_.size() == chat_history_.max_size() - 2) {
                // Remove oldest messages if we're approaching container limits
                chat_history_.pop_front();
                message_cache_.pop_front();
            }
            
            try {
                // Add user message to history with move semantics
                chat_history_.emplace_back("user", message);
                
                // Add cache entry for the new message
                message_cache_.emplace_back();
                layout_dirty_ = true;
                
                scroll_to_bottom_.store(true);
                waiting_for_response_.store(true);
                
                // Create shared context for the async operation to ensure memory safety
                auto async_context = std::make_shared<AsyncRequestContext>();
                async_context->user_message = message; // Copy the message safely
                async_context->llm_settings = current_llm_settings_; // Copy settings
                async_context->allow_search = allow_search_;
                async_context->temperature = temperature_;
                
                // Copy conversation history safely for async operation
                async_context->conversation_snapshot.reserve(chat_history_.size());
                for (const auto& msg : chat_history_) {
                    async_context->conversation_snapshot.emplace_back(msg.first, msg.second);
                }
                
                // Create a shared fetcher instance for this request to avoid accessing member fetcher_
                async_context->fetcher = std::make_shared<http::fetch>(ai_request_timeout_seconds_);
                async_context->fetcher->set_max_retries(3);
                
                // Create a new LLM instance for this async operation rather than copying the existing one
                auto async_llm_instance = helpers::LLMConfig::create_llm_instance();
                if (!async_llm_instance) {
                    throw std::runtime_error("Failed to create LLM instance for async operation");
                }
                async_context->llm_instance_copy = std::move(*async_llm_instance);
                
                // Start async request with proper memory management
                pending_response_ = std::make_optional(std::async(std::launch::async, [this, context = std::move(async_context)]() {
                    try {
                        if (!context || !context->fetcher) {
                            throw std::runtime_error("Invalid async context");
                        }
                        
                        // Determine search mode (only for Grok) using copied settings
                        std::string search_mode_str;
                        if (context->llm_settings.provider == helpers::LLMConfig::Provider::GROK && context->allow_search) {
                            search_mode_str = "on";
                        }
                        
                        // Use the copied LLM instance and context data
                        auto response = context->llm_instance_copy.sendMessage(
                            context->user_message, 
                            [fetcher = context->fetcher](const std::string& url, const std::string& data, auto header_client) {
                                // Capture fetcher by value to ensure it stays alive
                                return fetcher->post(url, data, header_client);
                            },
                            "user", 
                            context->llm_settings.model_name, 
                            search_mode_str, 
                            context->temperature, 
                            &context->conversation_snapshot
                        );
                        
                        // Extract message content safely with bounds checking
                        std::string reply;
                        if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                            reply = response.choices[0].message.content; // Copy construct
                        } else {
                            reply = "Error: Empty response received";
                        }
                        const std::string reply_for_tts = reply;
                        
                        // Safely add to chat history using thread-safe operations
                        // Note: We need to be careful about concurrent access to chat_history_
                        // For now, we rely on the fact that only one async operation runs at a time
                        // due to waiting_for_response_ gate, but we should add proper synchronization
                        {
                            std::lock_guard<std::mutex> lock(chat_history_mutex_);
                            chat_history_.emplace_back("assistant", std::move(reply));
                            message_cache_.emplace_back();
                            layout_dirty_ = true;
                        }
                        maybe_speak_reply(reply_for_tts);
                        
                    } catch (const std::bad_alloc&) {
                        std::lock_guard<std::mutex> lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", "Memory allocation error");
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    } catch (const std::runtime_error& e) {
                        std::lock_guard<std::mutex> lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", std::string("Runtime error: ") + e.what());
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    } catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", std::string("Error [") + typeid(e).name() + "]: " + e.what());
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", "Unknown error occurred");
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    }
                    
                    waiting_for_response_.store(false);
                    clear_input_on_response_.store(true);
                    scroll_to_bottom_.store(true);
                }));
                
            } catch (const std::exception& e) {
                // Handle synchronous errors
                chat_history_.emplace_back("assistant", std::string("Error starting request: ") + e.what());
                message_cache_.emplace_back();
                layout_dirty_ = true;
                waiting_for_response_.store(false);
            }
        }
        
        void process_pending_response() {
            if (pending_response_ && pending_response_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    pending_response_->get(); // This will rethrow any exception from the async task
                } catch (const std::exception& e) {
                    // Log or handle any unhandled exceptions from the async task
                    std::lock_guard<std::mutex> lock(chat_history_mutex_);
                    chat_history_.emplace_back("assistant", std::string("Async task error: ") + e.what());
                    message_cache_.emplace_back();
                    layout_dirty_ = true;
                }
                
                pending_response_.reset();
                // Clear input text and set focus flag after a response is received
                input_text_.clear();
                reclaim_focus_ = true;
            }
        }
        
        void recalculate_layout(float width_for_content) {
            std::lock_guard<std::mutex> lock(chat_history_mutex_);
            
            // Ensure cache matches history size with exception safety
            try {
                while (message_cache_.size() < chat_history_.size()) {
                    message_cache_.emplace_back();
                }
            } catch (const std::bad_alloc&) {
                // If we can't allocate cache entries, skip layout recalculation
                return;
            }
            
            // Get the actual available width within the child window
            // Account for scrollbar that may appear when content overflows vertically
            float available_width = width_for_content;
            if (available_width <= 0) {
                available_width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 20.0f;
            }
            
            const ImVec2 padding(10.0f, 8.0f);
            // Make max width more conservative to prevent horizontal overflow
            const float max_width = available_width * 0.75f;  // More conservative from 0.80f
            const float min_width = available_width * 0.15f;  
            const float line_height = ImGui::GetTextLineHeightWithSpacing();
            const float separator_height = ImGui::GetStyle().ItemSpacing.y + 1.0f;
            
            for (size_t i = 0; i < chat_history_.size() && i < message_cache_.size(); ++i) {
                const auto& message = chat_history_[i];
                auto& cache = message_cache_[i];
                
                if (!cache.needs_recalc) continue;
                
                bool is_user = message.first == "user";
                
                // Calculate content width with extra margin for scrollbar
                cache.content_width = is_user ? 
                    std::min(max_width, std::max(min_width, max_width)) : max_width;
                cache.text_width = cache.content_width - padding.x * 2.0f;
                
                // Calculate message text height with proper wrapping
                ImVec2 text_size = ImGui::CalcTextSize(message.second.c_str(), nullptr, true, cache.text_width);
                const float message_height = std::max(text_size.y, line_height);
                
                // Calculate total bubble height
                cache.bubble_height = line_height + separator_height + message_height + 
                                    padding.y * 2.0f + ImGui::GetStyle().ItemSpacing.y;
                
                // Generate unique child ID using message index for better stability
                cache.child_id = "msg_bubble_" + std::to_string(i);
                
                cache.needs_recalc = false;
            }
        }
    };

} // namespace rouen::cards
