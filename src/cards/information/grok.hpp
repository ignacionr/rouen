#pragma once

#include "../../helpers/imgui_include.hpp"
#include <string>
#include <deque>
#include <future>
#include <optional>
#include <array>

#include "../../helpers/cppgpt.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/api_keys.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {
    class grok : public card {
    public:
        grok() {
            name("Chat with Grok");
            
            // Base colors (already in vector)
            colors[0] = ImVec4(0.15f, 0.25f, 1.0f, 0.7f);       // window background
            colors[1] = ImVec4(0.1f, 0.2f, 0.3f, 0.7f);       // Darker slate blue with alpha - secondary elements
            
            // Additional UI colors (add to the colors vector)
            get_color(2, ImVec4(0.25f, 0.35f, 0.45f, 0.7f));  // User message background
            get_color(3, ImVec4(0.15f, 0.25f, 0.35f, 0.7f));  // Assistant message background
            get_color(4, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));     // User message text
            get_color(5, ImVec4(0.6f, 0.9f, 1.0f, 1.0f));     // Assistant message text
            get_color(6, ImVec4(0.7f, 0.8f, 0.9f, 0.8f));     // Thinking indicator
            get_color(7, ImVec4(0.15f, 0.2f, 0.25f, 1.0f));   // Input field background
            get_color(8, ImVec4(0.3f, 0.4f, 0.5f, 1.0f));     // Button color
            get_color(9, ImVec4(0.4f, 0.5f, 0.6f, 1.0f));     // Button hover
            get_color(10, ImVec4(0.5f, 0.6f, 0.7f, 1.0f));    // Button active
            get_color(11, ImVec4(0.3f, 0.4f, 0.5f, 0.6f));    // Separator line color
            get_color(12, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));   // Chat background
            
            requested_fps = 10; // Higher FPS for responsive input
            
            // Read API key from centralized API key manager
            grok_api_key = helpers::ApiKeys::get_grok_api_key();
            if (!grok_api_key.empty()) {
                gpt = std::make_unique<ignacionr::cppgpt>(grok_api_key, ignacionr::cppgpt::grok_base);
                gpt->add_instructions("You are Grok, an AI assistant created by xAI. You are helpful, harmless, and honest.");
            }
            width *= 2.0f;
        }

        void render_llm_controls() {
            // whether to allow search
            ImGui::Checkbox("Allow Search", &allow_search);
            ImGui::SameLine();
            // temperature slider
            ImGui::SliderFloat("Temperature", &temperature, 0.0f, 1.0f);
        }

        bool render() override {
            return render_window([this]() {
                render_llm_controls();
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
                const float thinking_indicator_height = waiting_for_response ? ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
                const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
                const float footer_height_to_reserve = thinking_indicator_height + input_height + ImGui::GetStyle().ItemSpacing.y;
                
                // Chat area
                // Begin child with no horizontal scrollbar (use 0 width to auto-size without horizontal scroll)
                if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), true)) {
                    // Process any pending responses
                    process_pending_response();
                    
                    // Check if layout needs to be recalculated
                    float current_width = ImGui::GetContentRegionAvail().x;
                    if (layout_dirty || std::abs(last_width - current_width) > 1.0f) {
                        recalculate_layout(current_width);
                        last_width = current_width;
                        layout_dirty = false;
                    }
                    
                    // Pre-calculate common values
                    const ImVec2 padding(10.0f, 8.0f);
                    const float bubble_rounding = 5.0f;
                    
                    // Pre-convert colors to avoid repeated conversions
                    static const ImU32 user_bg_color = ImGui::ColorConvertFloat4ToU32(get_color(2));
                    static const ImU32 assistant_bg_color = ImGui::ColorConvertFloat4ToU32(get_color(3));
                    static const ImU32 user_text_color = ImGui::ColorConvertFloat4ToU32(get_color(4));
                    static const ImU32 assistant_text_color = ImGui::ColorConvertFloat4ToU32(get_color(5));
                    
                    // Display chat history using cached values
                    for (size_t i = 0; i < chat_history.size(); ++i) {
                        const auto& message = chat_history[i];
                        const auto& cache = message_cache[i];
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
                        ImGui::Text("%s", is_user ? "You" : "Grok");
                        ImGui::Separator();
                        
                        // Display message content with proper text wrapping
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cache.text_width);
                        ImGui::TextWrapped("%s", message.second.c_str());
                        ImGui::PopTextWrapPos();
                        
                        // End styling
                        ImGui::PopStyleColor(); // Text color
                        ImGui::EndChild();
                        
                        ImGui::PopStyleVar(3); // Pop the 3 style vars we pushed
                        ImGui::PopStyleColor(); // Pop the child bg color
                        
                        // Add consistent spacing between messages
                        ImGui::Spacing();
                        ImGui::Spacing();
                    }
                    
                    if (scroll_to_bottom) {
                        ImGui::SetScrollHereY(1.0f);
                        scroll_to_bottom = false;
                    }
                }
                ImGui::EndChild();
                
                // Show a loading indicator if waiting for response
                // Place the indicator outside the scrolling region
                if (waiting_for_response) {
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
                        "Grok is thinking",
                        "Grok is thinking.",
                        "Grok is thinking..",
                        "Grok is thinking..."
                    };
                    
                    float time = static_cast<float>(ImGui::GetTime());
                    int dots = (static_cast<int>(time * 2) % 4);
                    ImGui::Text("%s", thinking_frames[static_cast<size_t>(dots)]);
                    
                    ImGui::PopStyleColor();
                    ImGui::EndChild();
                    
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }
                
                // API key input if not set
                if (grok_api_key.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4]));
                    ImGui::TextWrapped("Please enter your Grok API key:");
                    ImGui::PopStyleColor();
                    ImGui::PushItemWidth(-1);
                    
                    // Copy current api_key_input to buffer
                    strncpy(api_key_buffer.data(), api_key_input.c_str(), api_key_buffer.size() - 1);
                    api_key_buffer[api_key_buffer.size() - 1] = '\0';
                    
                    if (ImGui::InputText("##apikey", api_key_buffer.data(), api_key_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        api_key_input = api_key_buffer.data();
                        grok_api_key = api_key_input;
                        gpt = std::make_unique<ignacionr::cppgpt>(grok_api_key, ignacionr::cppgpt::grok_base);
                        gpt->add_instructions("You are Grok, an AI assistant created by xAI. You are helpful, harmless, and honest.");
                    }
                    ImGui::PopItemWidth();
                }
                
                // Input area
                ImGui::Separator();
                reclaim_focus = false;
                
                // Only allow input if we have an API key and are not waiting for a response
                ImGui::BeginDisabled(grok_api_key.empty() || waiting_for_response);
                
                // Copy current input to buffer
                strncpy(input_buffer.data(), input_text.c_str(), input_buffer.size() - 1);
                input_buffer[input_buffer.size() - 1] = '\0';
                
                // Calculate space for the Send button to avoid it being cut off
                const float send_button_width = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
                
                // Focus on the input field initially
                if (grok_api_key.empty() == false && waiting_for_response == false && ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                
                ImGui::PushItemWidth(-send_button_width - ImGui::GetStyle().ItemSpacing.x);
                if (reclaim_focus) {
                    ImGui::SetKeyboardFocusHere();
                    ImGui::SetItemDefaultFocus();
                    reclaim_focus = false;
                    input_buffer.fill('\0'); // Clear the buffer
                }
                if (ImGui::InputText("##input", input_buffer.data(), input_buffer.size(), 
                    ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_NoHorizontalScroll)) {
                    input_text = input_buffer.data();
                    send_message(input_text);
                    input_text.clear();
                    reclaim_focus = true;
                }
                ImGui::PopItemWidth();
                
                ImGui::SameLine();
                if (ImGui::Button("Send", ImVec2(send_button_width, 0)) && !input_text.empty()) {
                    send_message(input_text);
                    input_text.clear();
                    reclaim_focus = true;
                }
                
                ImGui::EndDisabled();
                
                // Update input_text from buffer if it changed
                std::string new_input_text = input_buffer.data();
                if (new_input_text != input_text) {
                    input_text = new_input_text;
                }
                
                ImGui::PopStyleColor(8); // Pop all the colors we pushed at the beginning
            });
        }
        
        std::string get_uri() const override {
            return "grok";
        }

    private:
        struct MessageCache {
            float bubble_height;
            float content_width;
            float text_width;
            std::string child_id;
            bool needs_recalc = true;
        };
        
        std::unique_ptr<ignacionr::cppgpt> gpt{};
        std::string grok_api_key{};
        std::string api_key_input{};
        std::string input_text{};
        std::array<char, 1024> api_key_buffer{};
        std::array<char, 2048> input_buffer{};
        std::deque<std::pair<std::string, std::string>> chat_history{};
        std::deque<MessageCache> message_cache{};
        std::optional<std::future<void>> pending_response{};
        bool waiting_for_response{false};
        bool scroll_to_bottom{false};
        bool reclaim_focus{false};
        bool layout_dirty{true};
        float last_width{0.0f};
        http::fetch fetcher{};
        bool allow_search{false};
        float temperature{0.45f};
        
        void send_message(const std::string& message) {
            if (message.empty() || waiting_for_response) return;
            
            // Add user message to history
            chat_history.emplace_back("user", message);
            
            // Add cache entry for the new message
            message_cache.emplace_back();
            layout_dirty = true;
            
            scroll_to_bottom = true;
            waiting_for_response = true;
            
            // Start async request
            pending_response = std::make_optional(std::async(std::launch::async, [this, message]() {
                try {
                    auto response = gpt->sendMessage(message, 
                        [this](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher.post(url, data, header_client);
                        },
                        "user", "grok-3-latest", allow_search ? "on" : std::string_view{}, temperature);
                    
                    // Extract message content
                    std::string reply = response.choices[0].message.content;
                    
                    // Add to chat history
                    chat_history.emplace_back("assistant", reply);
                    
                    // Add cache entry for the response
                    message_cache.emplace_back();
                    layout_dirty = true;
                } catch (const std::exception& e) {
                    chat_history.emplace_back("assistant", std::string("Error: ") + e.what());
                    message_cache.emplace_back();
                    layout_dirty = true;
                }
                waiting_for_response = false;
                scroll_to_bottom = true;
            }));
        }
        
        void process_pending_response() {
            if (pending_response && pending_response->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                pending_response.reset();
                // Clear input text and set focus flag after a response is received
                input_text.clear();
                reclaim_focus = true;
            }
        }
        
        void recalculate_layout(float width_for_content) {
            // Ensure cache matches history size
            while (message_cache.size() < chat_history.size()) {
                message_cache.emplace_back();
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
            
            for (size_t i = 0; i < chat_history.size(); ++i) {
                const auto& message = chat_history[i];
                auto& cache = message_cache[i];
                
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
                
                // Generate unique child ID
                cache.child_id = std::to_string(reinterpret_cast<uintptr_t>(&message)) + "_bubble";
                
                cache.needs_recalc = false;
            }
        }
    };
}
