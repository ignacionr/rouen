#pragma once

#include <imgui.h>
#include <string>
#include <deque>
#include <future>
#include <optional>
#include <array>

#include "../../helpers/cppgpt.hpp"
#include "../../helpers/fetch.hpp"
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
            
            requested_fps = 30;
            
            // Read API key from environment
            char* api_key = std::getenv("GROK_API_KEY");
            if (api_key) {
                grok_api_key = api_key;
                gpt = std::make_unique<ignacionr::cppgpt>(grok_api_key, ignacionr::cppgpt::grok_base);
                gpt->add_instructions("You are Grok, an AI assistant created by xAI. You are helpful, harmless, and honest.");
            }
        }

        bool render() override {
            return render_window([this]() {
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
                // Calculate window width properly accounting for padding
                const float window_width = ImGui::GetContentRegionAvail().x;
                // Reserve some space for scrollbar and window decorations
                const float width_for_content = window_width - ImGui::GetStyle().ScrollbarSize - 10.0f;
                
                // Begin child with fixed width to prevent horizontal shifting
                if (ImGui::BeginChild("ScrollingRegion", ImVec2(width_for_content, -footer_height_to_reserve), true, ImGuiWindowFlags_HorizontalScrollbar)) {
                    // Process any pending responses
                    process_pending_response();
                    
                    // Display chat history
                    for (const auto& message : chat_history) {
                        bool is_user = message.first == "user";
                        
                        // Set background color for message bubbles
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertFloat4ToU32(
                            is_user ? colors[2] : colors[3]));
                        
                        // Add padding and rounded corners for message bubbles
                        const float bubble_rounding = 5.0f;
                        const ImVec2 padding(10.0f, 8.0f);
                        
                        // Create a child window for the message with proper styling
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, bubble_rounding);
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
                        
                        // Align user messages to the right, assistant messages to the left
                        const float max_width = width_for_content * 0.85f;
                        const float min_width = width_for_content * 0.2f;
                        
                        if (is_user) {
                            // Right-align user messages (with some margin)
                            float content_width = std::min(max_width, 
                                std::max(min_width, ImGui::CalcTextSize(message.second.c_str()).x + padding.x * 2));
                            ImGui::SetCursorPosX(width_for_content - content_width - 10.0f);
                        }
                        
                        // Generate unique ID for each message child window
                        ImGui::BeginChild(std::to_string(reinterpret_cast<uintptr_t>(&message)).c_str(), 
                            ImVec2(0, 0), true);
                        
                        // Set text color based on sender
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(
                            is_user ? colors[4] : colors[5]));
                        
                        // Display sender name in bold
                        ImGui::TextWrapped("%s", is_user ? "You" : "Grok");
                        ImGui::Separator();
                        
                        // Display message content
                        ImGui::TextWrapped("%s", message.second.c_str());
                        
                        // End styling
                        ImGui::PopStyleColor(); // Text color
                        ImGui::EndChild();
                        
                        ImGui::PopStyleVar(3); // Pop the 3 style vars we pushed
                        ImGui::PopStyleColor(); // Pop the child bg color
                        
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
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[6]));
                    ImGui::TextWrapped("Grok is thinking...");
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
                bool reclaim_focus = false;
                
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
        std::unique_ptr<ignacionr::cppgpt> gpt;
        std::string grok_api_key;
        std::string api_key_input;
        std::string input_text;
        std::array<char, 1024> api_key_buffer{};
        std::array<char, 2048> input_buffer{};
        std::deque<std::pair<std::string, std::string>> chat_history;
        std::optional<std::future<void>> pending_response;
        bool waiting_for_response = false;
        bool scroll_to_bottom = false;
        bool reclaim_focus = false;
        http::fetch fetcher;

        // Color fields
        std::array<ImVec4, 12> colors;
        
        void send_message(const std::string& message) {
            if (message.empty() || waiting_for_response) return;
            
            // Add user message to history
            chat_history.emplace_back("user", message);
            scroll_to_bottom = true;
            waiting_for_response = true;
            
            // Start async request
            pending_response = std::make_optional(std::async(std::launch::async, [this, message]() {
                try {
                    auto response = gpt->sendMessage(message, 
                        [this](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher.post(url, data, header_client);
                        });
                    
                    // Extract message content
                    std::string reply = response.choices[0].message.content;
                    
                    // Add to chat history
                    chat_history.emplace_back("assistant", reply);
                } catch (const std::exception& e) {
                    chat_history.emplace_back("assistant", std::string("Error: ") + e.what());
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

        void get_color(size_t index, const ImVec4& color) {
            if (index < colors.size()) {
                colors[index] = color;
            }
        }
    };
}