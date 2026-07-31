#include "ai_chat.hpp"

#include "../../helpers/cppgpt.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/notify_service.hpp"
#include "../../helpers/persona_manager.hpp"
#include "../../helpers/debug.hpp"
#include "../../fonts.hpp"
#include "../../registrar.hpp"
#include "../../../external/IconsMaterialDesign.h"
#include "../../hosts/dictation_host.hpp"
#include "llm_host.hpp"
#include "mcp_host.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <glaze/core/common.hpp>
#include <glaze/json/read.hpp>
#include <imgui.h>
#include <new>
#include <stdexcept>
#include <string>
#include <deque>
#include <future>
#include <optional>
#include <array>
#include <atomic>
#include <string_view>
#include <typeinfo>
#include <format>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <chrono>
#include <vector>

namespace rouen::cards {
    namespace {

    std::string sanitize_persona_name_for_tool(const std::string& name) {
        std::string sanitized = "call_persona_";
        for (char const c : name) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else {
                sanitized.push_back('_');
            }
        }
        std::string clean;
        for (char const c : sanitized) {
            if (c == '_' && !clean.empty() && clean.back() == '_') {
                continue;
            }
            clean.push_back(c);
        }
        return clean;
    }

    const helpers::Persona* find_persona_by_sanitized_name(const std::string& sanitized_name) {
        const auto& personas = helpers::PersonaManager::instance().get_personas();
        for (const auto& p : personas) {
            if (sanitize_persona_name_for_tool(p.name) == sanitized_name) {
                return &p;
            }
        }
        return nullptr;
    }

    std::string get_modular_mcp_instructions(const std::vector<std::string>& allowed_mcps) {
        std::string instr;
        auto has_mcp = [&](const std::string& name) {
            if (name == "adaptive_card" || name == "deck") {
                return std::find(allowed_mcps.begin(), allowed_mcps.end(), "deck") != allowed_mcps.end() ||
                       std::find(allowed_mcps.begin(), allowed_mcps.end(), "adaptive_card") != allowed_mcps.end();
            }
            if (name == "contacts" || name == "directory") {
                return std::find(allowed_mcps.begin(), allowed_mcps.end(), "contacts") != allowed_mcps.end() ||
                       std::find(allowed_mcps.begin(), allowed_mcps.end(), "directory") != allowed_mcps.end();
            }
            return std::find(allowed_mcps.begin(), allowed_mcps.end(), name) != allowed_mcps.end();
        };

        if (has_mcp("terminal")) {
            instr += "\nTERMINAL INSTRUCTIONS:\nYou have access to tools that can run local commands (e.g. bash commands). If the user asks you to check repository status, files, find the current date/time, or execute any shell command (including curl), use the provided `run_local_command` tool to execute them instead of giving them instructions on how to run it themselves.\n";
        }
        if (has_mcp("deck") || has_mcp("adaptive_card")) {
            instr += "\nDECK & ADAPTIVE CARD INSTRUCTIONS:\nWhen users ask you to 'open', 'show', 'create', 'design', or 'display' something, use the appropriate tool:\n- To create and present interactive, structured Adaptive Cards (such as flight passes, invoices, user profiles, status dashboards, forms, polls, or rich UI components), call `create_adaptive_card`. Provide a descriptive `title` and a valid Adaptive Card JSON structure for `card_json` (plus optional `context_json`).\n- To visualize numerical data or category comparisons, call `create_number_series_card`.\n- To open standard built-in cards, call `create_card` (e.g. 'pomodoro', 'terminal', 'git', 'calendar').\nCRITICAL INSTRUCTIONS ON DATA RETRIEVAL AND VISUALIZATION:\n1. If the user asks you to build, show, or create a card using data that can be retrieved via other tools (such as weather forecasts, git metrics, calendar events, or notes), you MUST follow a two-step process:\n- Step 1: Call the appropriate retrieval tool first to obtain real data.\n- Step 2: Call `create_adaptive_card` or `create_number_series_card` with the retrieved data.\n2. Never use placeholder data for visualization cards if there is a retrieval tool available to fetch actual data.\n";
        }
        if (has_mcp("wikipedia")) {
            instr += "\nWIKIPEDIA INSTRUCTIONS:\nCRITICAL INSTRUCTIONS ON WIKIPEDIA TOOL USAGE:\n1. If the user asks you to read, summarize, explain, or answer questions about a Wikipedia article or concept (for example: \"summarize 'The Garden of Forking Paths' by Borges\"), you MUST use retrieval tools: first search using wikipedia_search_concepts if needed to find the exact title, and then retrieve the full article using wikipedia_get_article_text. You must then summarize or answer directly in your chat response. DO NOT call wikipedia_create_card or create_card for this purpose!\n2. ONLY call wikipedia_create_card (or create_card) when the user explicitly requests to \"open\", \"show\", \"display\", or \"create\" a card/view on their screen (for example: \"open the wikipedia card for quantum computing\" or \"show the wikipedia card\").\n";
        }
        if (has_mcp("alarm")) {
            instr += "\nALARM INSTRUCTIONS:\nFor all other general alarms, timers, or reminders (e.g., 'set an alarm/timer for 20 minutes', 'alarm at 10 AM', 'remind me in 1 hour'), you MUST use the 'create_alarm' tool instead.\n";
        }
        if (has_mcp("pomodoro")) {
            instr += "\nPOMODORO INSTRUCTIONS:\nIMPORTANT: Differentiate clearly between Pomodoro and general alarms. Only use the Pomodoro tool (start_pomodoro) if the user explicitly mentions the word 'pomodoro'.\n";
        }
        if (has_mcp("notes")) {
            instr += "\nNOTES INSTRUCTIONS:\nYou have access to tools that can list, retrieve, create, update/append, or delete markdown notes. If the user wants to list/search notes, get a note's full text, write/save a new note, append information to a note, or delete a note, you MUST use the corresponding `notes_list`, `notes_get`, `notes_save`, `notes_append`, or `notes_delete` tool instead of guiding the user to do it manually.\n";
        }
        if (has_mcp("contacts") || has_mcp("directory")) {
            instr += "\nCONTACTS INSTRUCTIONS:\nYou have access to tools that can list, retrieve, create/update, delete, or import macOS contacts. If the user wants to search contacts, view contact details, save or import contacts, use the `contacts_list`, `contacts_get`, `contacts_save`, `contacts_delete`, or `contacts_import_macos` tools.\n";
        }
        return instr;
    }

    std::string get_function_category(const helpers::mcp_service::function_definition& func) {
        if (func.name == "run_local_command") return "terminal";
        if (func.name == "edit_file") return "editor";
        if (func.name == "create_card" || func.name == "create_number_series_card" || func.name == "create_adaptive_card" || func.name == "list_adaptive_cards" || func.name == "get_adaptive_card") return "deck";
        if (func.name.starts_with("wikipedia_")) return "wikipedia";
        if (func.name.starts_with("youtube_")) return "youtube";
        if (func.name.starts_with("contacts_")) return "contacts";
        if (func.name == "create_alarm") return "alarm";
        return func.card_type;
    }

    struct CallPersonaArgs {
        std::string message;
        struct glaze {
            using T = CallPersonaArgs;
            static constexpr auto value = glz::object(
                "message", &T::message
            );
        };
    };

    struct AsyncRequestContext {
        std::string user_message;
        helpers::LLMConfig::LLMSettings llm_settings;
        std::vector<std::pair<std::string, std::string>> conversation_snapshot;
        std::shared_ptr<http::fetch> fetcher;
        helpers::LLMConfig::LLMInstance llm_instance_copy;
        bool allow_search{false};
        float temperature{0.45f};
    };

    } // namespace

    ai_chat::ai_chat(std::string_view initial_query) {
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
        
        populate_persona_buffers(helpers::PersonaManager::instance().get_active_persona_index());
    }

    void ai_chat::render_llm_controls() {
        if (ImGui::CollapsingHeader("Configuration")) {
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
            
            bool speak_replies = notify_service::spoken_notifications_enabled();
            if (ImGui::Checkbox("Speak replies", &speak_replies)) {
                notify_service::set_spoken_notifications_enabled(speak_replies);
            }

            ImGui::SameLine();
            ImGui::Checkbox("Debug Mode", &debug_mode_);

            ImGui::SameLine();
            ImGui::Checkbox("Log Requests", &log_requests_);

            // Persona Configuration Section
            ImGui::SeparatorText("Personas");
            
            auto& pm = helpers::PersonaManager::instance();
            const auto& personas = pm.get_personas();
            size_t const active_idx = pm.get_active_persona_index();
            
            // Sync UI buffers if persona changed externally or wasn't loaded
            if (last_edited_persona_index_ != active_idx) {
                populate_persona_buffers(active_idx);
            }
            
            // Dropdown to select persona
            if (active_idx < personas.size()) {
                if (ImGui::BeginCombo("Active Persona", personas[active_idx].name.c_str())) {
                    for (size_t i = 0; i < personas.size(); ++i) {
                        bool const is_selected = (active_idx == i);
                        if (ImGui::Selectable(personas[i].name.c_str(), is_selected)) {
                            pm.select_persona(i);
                            populate_persona_buffers(i);
                            refresh_llm_config();
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            
            // Show fields for editing active persona
            if (active_idx < personas.size()) {
                auto p = personas[active_idx]; // Copy to update
                bool changed = false;
                
                // Name
                ImGui::Text("Persona Name:");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##persona_name", persona_name_buf_.data(), persona_name_buf_.size())) {
                    p.name = persona_name_buf_.data();
                    changed = true;
                }
                
                // Description
                ImGui::Text("Description:");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##persona_desc", persona_desc_buf_.data(), persona_desc_buf_.size())) {
                    p.description = persona_desc_buf_.data();
                    changed = true;
                }
                
                // Bound LLM Config dropdown
                ImGui::Text("Bound LLM Config:");
                auto& lcm = helpers::LLMConfigManager::instance();
                const auto& llm_configs = lcm.get_configs();
                std::string current_bound = p.llm_config_name;
                if (current_bound.empty()) {
                    current_bound = lcm.get_default_config_name();
                }
                
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##persona_llm_config", current_bound.c_str())) {
                    for (const auto& config : llm_configs) {
                        bool const is_selected = (current_bound == config.name);
                        if (ImGui::Selectable(config.name.c_str(), is_selected)) {
                            p.llm_config_name = config.name;
                            changed = true;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Enable Web Search checkbox
                auto bound_settings = helpers::LLMConfig::get_current_config(p.llm_config_name);
                if (bound_settings.provider == helpers::LLMConfig::Provider::GROK ||
                    bound_settings.provider == helpers::LLMConfig::Provider::GEMINI) {
                    bool enable_search = p.enable_search;
                    if (ImGui::Checkbox("Enable Web Search", &enable_search)) {
                        p.enable_search = enable_search;
                        changed = true;
                    }
                }
                
                // Temperature
                ImGui::Text("Temperature:");
                float temp = p.temperature;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##persona_temp", &temp, 0.0f, 1.0f, "%.2f")) {
                    p.temperature = temp;
                    changed = true;
                }
                
                // Allowed MCPs selection (Dynamic Discovery)
                ImGui::Text("Allowed MCPs:");
                std::vector<std::string> mcp_options = {
                    "terminal", "editor", "deck", "adaptive_card", "wikipedia", "youtube", 
                    "git", "calendar", "weather", "alarm", "pomodoro", "notes", "contacts"
                };
                
                if (mcp_service_) {
                    auto registered_funcs = mcp_service_->get_available_functions();
                    for (const auto& func : registered_funcs) {
                        std::string const cat = get_function_category(func);
                        if (!cat.empty() && std::find(mcp_options.begin(), mcp_options.end(), cat) == mcp_options.end()) {
                            mcp_options.push_back(cat);
                        }
                        if (!func.card_type.empty() && std::find(mcp_options.begin(), mcp_options.end(), func.card_type) == mcp_options.end()) {
                            mcp_options.push_back(func.card_type);
                        }
                    }
                }
                for (const auto& existing_mcp : p.allowed_mcps) {
                    if (!existing_mcp.empty() && std::find(mcp_options.begin(), mcp_options.end(), existing_mcp) == mcp_options.end()) {
                        mcp_options.push_back(existing_mcp);
                    }
                }
                std::sort(mcp_options.begin(), mcp_options.end());
                
                // Render in 3 columns for space efficiency
                ImGui::Columns(3, "mcp_columns", false);
                for (size_t m = 0; m < mcp_options.size(); ++m) {
                    const auto& mcp_name = mcp_options[m];
                    bool is_allowed = std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), mcp_name) != p.allowed_mcps.end();
                    if (ImGui::Checkbox(mcp_name.c_str(), &is_allowed)) {
                        if (is_allowed) {
                            p.allowed_mcps.push_back(mcp_name);
                        } else {
                            p.allowed_mcps.erase(std::remove(p.allowed_mcps.begin(), p.allowed_mcps.end(), mcp_name), p.allowed_mcps.end());
                        }
                        changed = true;
                    }
                    ImGui::NextColumn();
                }
                ImGui::Columns(1); // Restore columns

                // Allowed Personas selection
                if (personas.size() > 1) {
                    ImGui::Text("Allowed Personas:");
                    ImGui::Columns(2, "persona_columns", false);
                    for (size_t i = 0; i < personas.size(); ++i) {
                        if (i == active_idx) {
                            continue; // Don't allow calling oneself directly
                        }
                        const auto& other_p = personas[i];
                        bool is_allowed = std::find(p.allowed_personas.begin(), p.allowed_personas.end(), other_p.name) != p.allowed_personas.end();
                        if (ImGui::Checkbox(other_p.name.c_str(), &is_allowed)) {
                            if (is_allowed) {
                                p.allowed_personas.push_back(other_p.name);
                            } else {
                                p.allowed_personas.erase(std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), other_p.name), p.allowed_personas.end());
                            }
                            changed = true;
                        }
                        ImGui::NextColumn();
                    }
                    ImGui::Columns(1); // Restore columns
                }
                
                // System Prompt Instruction Text (multi-line)
                ImGui::Text("System Prompt Additions:");
                if (ImGui::InputTextMultiline("##persona_prompt", persona_prompt_buf_.data(), persona_prompt_buf_.size(), ImVec2(-1, 80))) {
                    p.system_prompt = persona_prompt_buf_.data();
                    changed = true;
                }
                
                if (changed) {
                    pm.update_persona(active_idx, p);
                    refresh_llm_config();
                }
                
                // New and Delete buttons
                if (ImGui::Button("New Persona")) {
                    helpers::Persona new_p;
                    new_p.name = std::format("Custom Persona {}", personas.size() + 1);
                    new_p.description = "A custom AI assistant persona.";
                    new_p.allowed_mcps = {"deck"};
                    new_p.system_prompt = "You are a custom assistant.";
                    new_p.llm_config_name = "Default";
                    pm.add_persona(new_p);
                    pm.select_persona(personas.size()); // select the newly added one
                    populate_persona_buffers(personas.size());
                    refresh_llm_config();
                }
                
                if (personas.size() > 1) {
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Persona")) {
                        pm.delete_persona(active_idx);
                        size_t const new_idx = pm.get_active_persona_index();
                        populate_persona_buffers(new_idx);
                        refresh_llm_config();
                    }
                }
            }
        }
    }

    bool ai_chat::render() {
        if (!initial_message_.empty()) {
            std::string const msg = std::move(initial_message_);
            initial_message_.clear();
            send_message(msg);
        }
        
        // Periodically refresh LLM configuration
        static float last_config_refresh = 0.0f;
        float const current_time = static_cast<float>(ImGui::GetTime());
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
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); // transparent chat area background
            ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertFloat4ToU32(colors[11]));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertFloat4ToU32(colors[7]));
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertFloat4ToU32(colors[8]));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertFloat4ToU32(colors[9]));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertFloat4ToU32(colors[10]));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4])); // Default text color (user_text_color)
            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.4f, 0.6f, 0.5f))); // Text selection color
            
            // Calculate required space for the footer area
            const float thinking_indicator_height = 0.0f;
            const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
            const float footer_height_to_reserve = thinking_indicator_height + input_height + ImGui::GetStyle().ItemSpacing.y;
            
            // Chat area
            // Begin child with no horizontal scrollbar (use 0 width to auto-size without horizontal scroll)
            if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false)) {
                // Process any pending responses
                process_pending_response();
                
                // Check if layout needs to be recalculated
                float const current_width = ImGui::GetContentRegionAvail().x;
                if (layout_dirty_ || std::abs(last_width_ - current_width) > 1.0f) {
                    bool const force_all = std::abs(last_width_ - current_width) > 1.0f;
                    recalculate_layout(current_width, force_all);
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
                const ImVec4 assistant_bg = ImVec4(raw_assistant_bg.x, raw_assistant_bg.y, raw_assistant_bg.z, 0.55f);
                
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
                
                const ImVec4 debug_bg = ImVec4(0.12f, 0.15f, 0.22f, 0.85f);
                const ImU32 debug_bg_color = ImGui::ColorConvertFloat4ToU32(debug_bg);
                const ImU32 debug_text_color = pick_text_color(blend_colors(debug_bg, chat_bg));
                
                // Display chat history using cached values
                // Create a safe snapshot of the chat history for rendering
                std::vector<std::pair<std::string, std::string>> chat_snapshot;
                std::vector<MessageCache> cache_snapshot;
                {
                    std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                    chat_snapshot.reserve(chat_history_.size());
                    cache_snapshot.reserve(message_cache_.size());
                    
                    // Copy the data safely
                    chat_snapshot.assign(chat_history_.begin(), chat_history_.end());
                    cache_snapshot.assign(message_cache_.begin(), message_cache_.end());
                }
                
                for (size_t i = 0; i < chat_snapshot.size() && i < cache_snapshot.size(); ++i) {
                    const auto& message = chat_snapshot[i];
                    const auto& cache = cache_snapshot[i];
                    bool const is_user = message.first == "user";
                    bool const is_debug = message.first == "debug";
                    
                    // Set background color for message bubbles
                    ImU32 bg_color = assistant_bg_color;
                    if (is_user) bg_color = user_bg_color;
                    else if (is_debug) bg_color = debug_bg_color;
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg_color);
                    
                    // Position user messages to the right
                    if (is_user) {
                        float const available_width = ImGui::GetContentRegionAvail().x;
                        ImGui::SetCursorPosX(available_width - cache.content_width - 10.0f);
                    }
                    
                    // Create message bubble with cached size
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, bubble_rounding);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
                    
                    // Use pre-calculated child ID
                    ImGui::BeginChild(cache.child_id.c_str(), 
                        ImVec2(cache.content_width, cache.bubble_height), true, 
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    
                    // Set text color based on sender
                    ImU32 txt_color = assistant_text_color;
                    if (is_user) txt_color = user_text_color;
                    else if (is_debug) txt_color = debug_text_color;
                    ImGui::PushStyleColor(ImGuiCol_Text, txt_color);
                    
                    // Display sender name
                    std::string sender_name;
                    if (is_user) {
                        sender_name = "You";
                    } else if (is_debug) {
                        sender_name = "Debug Info";
                    } else {
                        sender_name = get_assistant_name();
                    }
                    ImGui::Text("%s", sender_name.c_str());
                    
                    // Copy button aligned to the right inside the balloon
                    ImGui::SameLine();
                    float const copy_btn_pos_x = cache.content_width - padding.x * 2.0f - 18.0f;
                    if (copy_btn_pos_x > ImGui::GetCursorPosX()) {
                        ImGui::SetCursorPosX(copy_btn_pos_x);
                    }
                    
                    // Transparent/subtle styling for the copy icon button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
                    ImGui::PushStyleColor(ImGuiCol_Text, txt_color);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                    
                    std::string const copy_btn_id = std::format("{}##copy_{}", ICON_MD_CONTENT_COPY, i);
                    if (ImGui::Button(copy_btn_id.c_str())) {
                        ImGui::SetClipboardText(message.second.c_str());
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Copy message text");
                    }
                    
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(4);
                    
                    ImGui::Separator();
                    
                    // Display message content: use Markdown rendering for
                    // assistant replies (which may contain MD formatting),
                    // plain text for user messages.
                    if (!is_user) {
                        const helpers::markdown_render_config md_cfg{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
                        };
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cache.text_width);
                        helpers::render_markdown_block(
                            message.second,
                            md_cfg,
                            [](const std::string& url) {
                                rouen::platform::open_url(url);
                            }
                        );
                        ImGui::PopTextWrapPos();
                    } else {
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cache.text_width);
                        ImGui::TextWrapped("%s", message.second.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    
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
                            std::string const full_message = std::format("{}: {}", sender_name, message.second);
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
                
                if (waiting_for_response_.load()) {
                    // Render a thinking preview bubble with a blinking cursor at the end
                    float available_width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 20.0f;
                    if (available_width < 100.0f) available_width = last_width_ - 40.0f;
                    
                    const float max_width = available_width * 0.92f;
                    const float min_width = available_width * 0.10f;
                    const float line_height = ImGui::GetTextLineHeightWithSpacing();
                    const float separator_height = ImGui::GetStyle().ItemSpacing.y + 1.0f;
                    
                    float const time = static_cast<float>(ImGui::GetTime());
                    bool const show_cursor = (static_cast<int>(time * 2.0f) % 2) == 0;
                    std::string const cursor_str = show_cursor ? ">" : " ";
                    
                    // Bubble dimensions - 1 line of message height
                    float const content_width = std::clamp(32.0f + padding.x * 2.0f + 24.0f, min_width, max_width);
                    float const bubble_height = line_height + separator_height + line_height + 
                                        padding.y * 2.0f + ImGui::GetStyle().ItemSpacing.y + 6.0f;
                                        
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, assistant_bg_color);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, bubble_rounding);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
                    
                    ImGui::BeginChild("msg_bubble_thinking", 
                        ImVec2(content_width, bubble_height), true, 
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                        
                    ImGui::PushStyleColor(ImGuiCol_Text, assistant_text_color);
                    
                    std::string const sender_name = get_assistant_name();
                    ImGui::Text("%s", sender_name.c_str());
                    
                    // Transparent/subtle copy icon spacing alignment (disabled)
                    ImGui::SameLine();
                    float const copy_btn_pos_x = content_width - padding.x * 2.0f - 18.0f;
                    if (copy_btn_pos_x > ImGui::GetCursorPosX()) {
                        ImGui::SetCursorPosX(copy_btn_pos_x);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                    ImGui::Button(ICON_MD_CONTENT_COPY);
                    ImGui::PopStyleColor(2);
                    
                    ImGui::Separator();
                    
                    ImGui::Text("%s", cursor_str.c_str());
                    
                    ImGui::PopStyleColor(); // assistant_text_color
                    ImGui::EndChild();
                    
                    ImGui::PopStyleVar(3);
                    ImGui::PopStyleColor(); // assistant_bg_color
                    
                    ImGui::Spacing();
                    ImGui::Spacing();
                }
                
                if (scroll_to_bottom_.load()) {
                    ImGui::SetScrollHereY(1.0f);
                    scroll_to_bottom_.store(false);
                }
            }
            ImGui::EndChild();
            
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

            // Process pending dictation transcription if available
            std::string dictation_result;
            {
                std::lock_guard<std::mutex> const lock(dictation_mutex_);
                if (pending_dictation_result_.has_value()) {
                    dictation_result = std::move(*pending_dictation_result_);
                    pending_dictation_result_.reset();
                }
            }
            if (!dictation_result.empty()) {
                input_text_ = dictation_result;
                send_message(input_text_);
                input_text_.clear();
                input_buffer_.fill('\0');
                reclaim_focus_ = true;
            }
            
            // Get dictation host status
            auto host = rouen::hosts::dictation_host::get_host();
            auto dict_state = host->get_state();
            if (dict_state != rouen::hosts::dictation_host::State::Idle) {
                requested_fps = 30; // Boost FPS for smooth UI color updates
            }

            // Copy current input to buffer with bounds checking
            const size_t copy_len = std::min(input_text_.length(), input_buffer_.size() - 1);
            std::strncpy(input_buffer_.data(), input_text_.c_str(), copy_len);
            input_buffer_[copy_len] = '\0';
            
            // Calculate space for Dictation, Send and Clear buttons
            const float dict_button_width = ImGui::CalcTextSize(ICON_MD_MIC " Dictate").x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
            const float send_button_width = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
            const float clear_button_width = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
            
            // Focus on the input field initially
            if (llm_configured_ && !waiting_for_response_.load() && ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            
            bool const disable_text_input = !llm_configured_ || waiting_for_response_.load();
            if (disable_text_input) {
                ImGui::BeginDisabled();
            }

            ImGui::PushItemWidth(-dict_button_width - send_button_width - clear_button_width - ImGui::GetStyle().ItemSpacing.x * 3.0f);
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

            if (disable_text_input) {
                ImGui::EndDisabled();
            }
            
            // Dictation Button - Always active
            ImGui::SameLine();
            std::string dict_label;
            ImVec4 dict_normal_col, dict_hover_col, dict_active_col;
            bool const disable_dict_button = (dict_state == rouen::hosts::dictation_host::State::Transcribing);

            if (dict_state == rouen::hosts::dictation_host::State::Recording) {
                // Recording state -> Bright Red color
                dict_label = ICON_MD_MIC " Recording...";
                dict_normal_col = ImVec4(0.85f, 0.18f, 0.18f, 1.0f);
                dict_hover_col  = ImVec4(0.95f, 0.28f, 0.28f, 1.0f);
                dict_active_col = ImVec4(0.75f, 0.12f, 0.12f, 1.0f);
            } else if (dict_state == rouen::hosts::dictation_host::State::Transcribing) {
                // Transcribing state -> Third color (Amber / Gold)
                dict_label = ICON_MD_HOURGLASS_EMPTY " Processing...";
                dict_normal_col = ImVec4(0.90f, 0.58f, 0.12f, 1.0f);
                dict_hover_col  = ImVec4(0.98f, 0.68f, 0.22f, 1.0f);
                dict_active_col = ImVec4(0.80f, 0.48f, 0.08f, 1.0f);
            } else if (dict_state == rouen::hosts::dictation_host::State::Starting) {
                // Starting state -> Standard button color (recording process launching...)
                dict_label = ICON_MD_MIC " Starting...";
                dict_normal_col = get_color(8);
                dict_hover_col  = get_color(9);
                dict_active_col = get_color(10);
            } else {
                // Idle state -> Standard button color
                dict_label = ICON_MD_MIC " Dictate";
                dict_normal_col = get_color(8);
                dict_hover_col  = get_color(9);
                dict_active_col = get_color(10);
            }

            ImGui::PushStyleColor(ImGuiCol_Button, dict_normal_col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dict_hover_col);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, dict_active_col);

            if (disable_dict_button) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button(dict_label.c_str(), ImVec2(dict_button_width, 0))) {
                if (dict_state == rouen::hosts::dictation_host::State::Idle || dict_state == rouen::hosts::dictation_host::State::Error) {
                    host->start_recording();
                } else if (dict_state == rouen::hosts::dictation_host::State::Recording || dict_state == rouen::hosts::dictation_host::State::Starting) {
                    host->stop_recording([this](std::string text) {
                        std::lock_guard<std::mutex> const lock(dictation_mutex_);
                        pending_dictation_result_ = text;
                    });
                }
            }

            if (disable_dict_button) {
                ImGui::EndDisabled();
            }

            ImGui::PopStyleColor(3);

            if (disable_text_input) {
                ImGui::BeginDisabled();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Send", ImVec2(send_button_width, 0)) && !input_text_.empty()) {
                send_message(input_text_);
                input_text_.clear();
                reclaim_focus_ = true;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(clear_button_width, 0))) {
                std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                chat_history_.clear();
                message_cache_.clear();
                layout_dirty_ = true;
            }
            
            if (disable_text_input) {
                ImGui::EndDisabled();
            }
            
            // Update input_text from buffer if it changed
            std::string new_input_text = input_buffer_.data();
            if (new_input_text != input_text_) {
                input_text_ = std::move(new_input_text);
            }
            
            ImGui::PopStyleColor(8); // Pop all the colors we pushed at the beginning
        });
    }

    std::string ai_chat::get_uri() const {
        if (!initial_query_.empty()) {
            return std::format("ai-chat:{}", ::helpers::StringHelper::url_encode(initial_query_));
        }
        return "ai-chat";
    }

    void ai_chat::maybe_speak_reply(const std::string& text) {
        if (text.empty()) {
            return;
        }
        if (notify_service::spoken_notifications_enabled()) {
            rouen::platform::speak_text_async(text);
        }
    }

    void ai_chat::populate_persona_buffers(size_t index) {
        auto& pm = helpers::PersonaManager::instance();
        if (index < pm.get_personas().size()) {
            const auto& p = pm.get_personas()[index];
            
            std::strncpy(persona_name_buf_.data(), p.name.c_str(), persona_name_buf_.size() - 1);
            persona_name_buf_[persona_name_buf_.size() - 1] = '\0';
            
            std::strncpy(persona_desc_buf_.data(), p.description.c_str(), persona_desc_buf_.size() - 1);
            persona_desc_buf_[persona_desc_buf_.size() - 1] = '\0';
            
            std::strncpy(persona_prompt_buf_.data(), p.system_prompt.c_str(), persona_prompt_buf_.size() - 1);
            persona_prompt_buf_[persona_prompt_buf_.size() - 1] = '\0';
            
            last_edited_persona_index_ = index;
        }
    }

    std::string ai_chat::execute_function_with_debug(const std::string& function_name, const std::string& args_json, int depth) {
        if (debug_mode_) {
            std::lock_guard<std::mutex> const lock(chat_history_mutex_);
            chat_history_.emplace_back("debug", std::format("🔧 **Tool Call**: `{}` (depth: {})\n\nArguments:\n```json\n{}\n```", function_name, depth, args_json));
            message_cache_.emplace_back();
            layout_dirty_ = true;
            scroll_to_bottom_.store(true);
        }

        std::string result;
        if (function_name.starts_with("call_persona_")) {
            result = execute_persona_call(function_name, args_json, depth);
        } else if (mcp_service_) {
            try {
                auto res = mcp_service_->execute_function(function_name, args_json);
                result = res.success ? res.result : "Error: " + res.error_message;
            } catch (const std::exception& e) {
                result = "Error executing function: " + std::string(e.what());
            }
        } else {
            result = "Error: MCP service not available";
        }

        if (debug_mode_) {
            std::lock_guard<std::mutex> const lock(chat_history_mutex_);
            chat_history_.emplace_back("debug", std::format("📤 **Tool Result**: `{}`\n\n```\n{}\n```", function_name, result));
            message_cache_.emplace_back();
            layout_dirty_ = true;
            scroll_to_bottom_.store(true);
        }

        return result;
    }

    std::string ai_chat::execute_persona_call(const std::string& function_name, const std::string& args_json, int depth) {
        if (depth > 5) {
            return "Error: Maximum inter-persona communication depth exceeded (potential circular dependency).";
        }

        const auto* target_persona = find_persona_by_sanitized_name(function_name);
        if (!target_persona) {
            return "Error: Target persona not found.";
        }

        CallPersonaArgs args;
        auto err = glz::read_json(args, args_json);
        if (err) {
            return "Error: Failed to parse arguments for persona call. Expected JSON object with a 'message' field.";
        }

        if (args.message.empty()) {
            return "Error: Message argument cannot be empty.";
        }

        auto target_llm_opt = helpers::LLMConfig::create_llm_instance(target_persona->llm_config_name);
        if (!target_llm_opt) {
            return "Error: Failed to initialize LLM for persona " + target_persona->name + ". Check config.";
        }
        auto target_llm = std::move(*target_llm_opt);

        auto target_settings = helpers::LLMConfig::get_current_config(target_persona->llm_config_name);
        std::string model_name = target_settings.model_name;
        std::string search_mode_str;
        if ((target_settings.provider == helpers::LLMConfig::Provider::GROK || 
             target_settings.provider == helpers::LLMConfig::Provider::GEMINI) && target_persona->enable_search) {
            search_mode_str = "on";
        }

        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm const now_tm = *std::localtime(&now_time_t);
        char time_buf[64];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &now_tm);
        std::string const time_instr = std::format("The current local date and time is: {}. Use this to understand relative dates like 'today', 'tomorrow', 'yesterday', 'this week', etc.", std::string(time_buf));
        target_llm.add_instructions(time_instr);

        target_llm.add_instructions(target_persona->system_prompt);
        std::string const modular_instr = get_modular_mcp_instructions(target_persona->allowed_mcps);
        if (!modular_instr.empty()) {
            target_llm.add_instructions(modular_instr);
        }

        std::vector<std::string> function_schemas;
        if (mcp_service_) {
            try {
                auto has_mcp = [&](const std::string& cat) {
                    if (cat == "adaptive_card" || cat == "deck") {
                        return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "deck") != target_persona->allowed_mcps.end() ||
                               std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "adaptive_card") != target_persona->allowed_mcps.end();
                    }
                    if (cat == "contacts" || cat == "directory") {
                        return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "contacts") != target_persona->allowed_mcps.end() ||
                               std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "directory") != target_persona->allowed_mcps.end();
                    }
                    return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), cat) != target_persona->allowed_mcps.end();
                };

                auto functions = mcp_service_->get_available_functions();
                for (const auto& func : functions) {
                    std::string const cat = get_function_category(func);
                    if (!has_mcp(cat)) {
                        continue;
                    }

                    std::string const raw_schema = std::format(
                        "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                        func.name, 
                        func.description.empty() ? "Repository operation" : func.description,
                        func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
                    );
                    
                    std::string schema;
                    schema.reserve(raw_schema.size());
                    for (char const c : raw_schema) {
                        if (c != '\n' && c != '\r' && c != '\t') {
                            schema.push_back(c);
                        }
                    }
                    function_schemas.push_back(schema);
                }
            } catch (const std::exception& e) {
                LOG_COMPONENT("AIChat", LOG_LEVEL_WARN, std::format("Failed to parse tool schema: {}", e.what()));
            }
        }

        for (const auto& allowed_sub_name : target_persona->allowed_personas) {
            const auto& personas = helpers::PersonaManager::instance().get_personas();
            const helpers::Persona* sub_p = nullptr;
            for (const auto& p : personas) {
                if (p.name == allowed_sub_name) {
                    sub_p = &p;
                    break;
                }
            }
            if (!sub_p) continue;

            std::string sub_func_name = sanitize_persona_name_for_tool(sub_p->name);
            std::string desc = "Delegates a task or asks a question to the Persona '" + sub_p->name + "'. Description: " + sub_p->description;
            std::string const schema = std::format(
                "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{{\"type\":\"object\",\"properties\":{{\"message\":{{\"type\":\"string\",\"description\":\"The message, query, or instruction to send to the persona.\"}}}},\"required\":[\"message\"]}}}}",
                sub_func_name, desc
            );
            function_schemas.push_back(schema);
        }

        auto fetcher = std::make_shared<http::fetch>(ai_request_timeout_seconds_);
        fetcher->set_max_retries(3);

        try {
            auto chat_completion = std::visit([&](auto& adapter_ptr) -> ignacionr::ChatCompletion {
                return adapter_ptr->sendMessageWithFunctionCalling(
                    args.message,
                    [fetcher, log_requests = log_requests_](const std::string& url, const std::string& body, auto header_setter) {
                        if (log_requests) {
                            std::cerr << "[LLM Request] URL: " << url << "\n[LLM Request Body]: " << body << "\n";
                        }
                        auto response = fetcher->post(url, body, header_setter);
                        if (log_requests) {
                            std::cerr << "[LLM Response]: " << response << "\n";
                        }
                        return response;
                    },
                    [this, depth](const std::string& func_name, const std::string& func_args_json) -> std::string {
                        return execute_function_with_debug(func_name, func_args_json, depth + 1);
                    },
                    "user", model_name, search_mode_str, target_persona->temperature, nullptr, &function_schemas
                );
            }, target_llm.instance_);

            if (!chat_completion.choices.empty()) {
                return chat_completion.choices[0].message.content;
            }
            return "Error: No response from target persona " + target_persona->name;
        } catch (const std::exception& e) {
            return "Error calling persona " + target_persona->name + ": " + std::string(e.what());
        }
    }

    void ai_chat::refresh_llm_config() {
        current_llm_settings_ = helpers::LLMConfig::get_current_config();
        llm_configured_ = current_llm_settings_.is_configured;
        
        if (llm_configured_) {
            llm_instance_ = helpers::LLMConfig::create_llm_instance();
        } else {
            llm_instance_.reset();
        }
        
        // Update card name based on active persona name
        if (llm_configured_) {
            auto& pm = helpers::PersonaManager::instance();
            name("AI Chat (" + pm.get_active_persona().name + ")");
        } else {
            name("AI Chat (Not Configured)");
        }
    }

    std::string ai_chat::get_assistant_name() {
        return helpers::PersonaManager::instance().get_active_persona().name;
    }

    void ai_chat::send_message(const std::string& message) {
        if (message.empty() || waiting_for_response_.load() || !llm_configured_ || !llm_instance_) {
            return;
        }
        
        // Process message with MCP functions first
        process_mcp_message(message);
    }

    void ai_chat::process_mcp_message(const std::string& message) {
        DEBUG_DEBUG("AI Chat: Processing message: '" + message + "'");
        
        // For function calling, we send the message directly to the LLM with function schemas
        // and let Gemini decide when to call functions
        send_message_to_llm_with_functions(message);
    }

    void ai_chat::send_message_to_llm_with_functions(const std::string& message) {
        if (message.empty() || waiting_for_response_.load() || !llm_configured_ || !llm_instance_) {
            return;
        }

        // Get available MCP functions and convert to Gemini function schemas
        std::vector<std::string> function_schemas;
        if (mcp_service_) {
            try {
                auto& active_persona = helpers::PersonaManager::instance().get_active_persona();
                auto has_mcp = [&](const std::string& cat) {
                    if (cat == "adaptive_card" || cat == "deck") {
                        return std::find(active_persona.allowed_mcps.begin(), active_persona.allowed_mcps.end(), "deck") != active_persona.allowed_mcps.end() ||
                               std::find(active_persona.allowed_mcps.begin(), active_persona.allowed_mcps.end(), "adaptive_card") != active_persona.allowed_mcps.end();
                    }
                    if (cat == "contacts" || cat == "directory") {
                        return std::find(active_persona.allowed_mcps.begin(), active_persona.allowed_mcps.end(), "contacts") != active_persona.allowed_mcps.end() ||
                               std::find(active_persona.allowed_mcps.begin(), active_persona.allowed_mcps.end(), "directory") != active_persona.allowed_mcps.end();
                    }
                    return std::find(active_persona.allowed_mcps.begin(), active_persona.allowed_mcps.end(), cat) != active_persona.allowed_mcps.end();
                };

                auto functions = mcp_service_->get_available_functions();
                for (const auto& func : functions) {
                    std::string const cat = get_function_category(func);
                    if (!has_mcp(cat)) {
                        continue;
                    }

                    std::string const raw_schema = std::format(
                        "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                        func.name, 
                        func.description.empty() ? "Repository operation" : func.description,
                        func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
                    );
                    
                    // Strip out control characters like newlines and tabs to ensure a clean JSON string
                    std::string schema;
                    schema.reserve(raw_schema.size());
                    for (char const c : raw_schema) {
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

        // Also add schemas for the active persona's allowed personas
        try {
            auto& active_persona = helpers::PersonaManager::instance().get_active_persona();
            for (const auto& allowed_sub_name : active_persona.allowed_personas) {
                // Find the sub-persona to get its description
                const auto& personas = helpers::PersonaManager::instance().get_personas();
                const helpers::Persona* sub_p = nullptr;
                for (const auto& p : personas) {
                    if (p.name == allowed_sub_name) {
                        sub_p = &p;
                        break;
                    }
                }
                if (!sub_p) continue;

                std::string sub_func_name = sanitize_persona_name_for_tool(sub_p->name);
                std::string desc = "Delegates a task or asks a question to the Persona '" + sub_p->name + "'. Description: " + sub_p->description;
                std::string const schema = std::format(
                    "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{{\"type\":\"object\",\"properties\":{{\"message\":{{\"type\":\"string\",\"description\":\"The message, query, or instruction to send to the persona.\"}}}},\"required\":[\"message\"]}}}}",
                    sub_func_name, desc
                );
                function_schemas.push_back(schema);
                DEBUG_DEBUG("AI Chat: Added function schema for persona: " + sub_p->name);
            }
        } catch (const std::exception& e) {
            DEBUG_ERROR("AI Chat: Error getting allowed personas schemas: " + std::string(e.what()));
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
            layout_dirty_ = true;
            
            waiting_for_response_.store(true);
            scroll_to_bottom_.store(true);
            
            // Determine model and search mode before launching the thread (thread-safe capture)
            std::string model_name = current_llm_settings_.model_name;
            std::string search_mode_str;
            bool const allow_search = helpers::PersonaManager::instance().get_active_persona().enable_search;
            if ((current_llm_settings_.provider == helpers::LLMConfig::Provider::GROK || 
                 current_llm_settings_.provider == helpers::LLMConfig::Provider::GEMINI) && allow_search) {
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
                    std::tm const now_tm = *std::localtime(&now_time_t);
                    char time_buf[64];
                    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &now_tm);
                    std::string const time_instr = std::format("The current local date and time is: {}. Use this to understand relative dates like 'today', 'tomorrow', 'yesterday', 'this week', etc.", std::string(time_buf));
                    local_llm.add_instructions(time_instr);

                    // Set system instructions based on persona and its allowed MCPs
                    auto& active_persona = helpers::PersonaManager::instance().get_active_persona();
                    local_llm.add_instructions(active_persona.system_prompt);
                    
                    std::string const modular_instr = get_modular_mcp_instructions(active_persona.allowed_mcps);
                    if (!modular_instr.empty()) {
                        local_llm.add_instructions(modular_instr);
                    }

                    // Create conversion from our message format to the format expected by sendMessage with mutex protection
                    std::vector<std::pair<std::string, std::string>> conversation_for_llm;
                    {
                        std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                        for (const auto& chat_msg : chat_history_) {
                            if (chat_msg.first == "user" || chat_msg.first == "assistant") {
                                conversation_for_llm.emplace_back(chat_msg.first, chat_msg.second);
                            }
                        }
                    }
                    
                    // Try to use function calling if we have a Gemini adapter directly
                    auto fetcher = std::make_shared<http::fetch>(ai_request_timeout_seconds_);
                    fetcher->set_max_retries(3);
                    auto chat_completion = std::visit([&](auto& adapter_ptr) -> ignacionr::ChatCompletion {
                        return adapter_ptr->sendMessageWithFunctionCalling(
                            message,
                            [fetcher, log_requests = log_requests_](const std::string& url, const std::string& body, auto header_setter) {
                                if (log_requests) {
                                    std::cerr << "[LLM Request] URL: " << url << "\n[LLM Request Body]: " << body << "\n";
                                }
                                auto response = fetcher->post(url, body, header_setter);
                                if (log_requests) {
                                    std::cerr << "[LLM Response]: " << response << "\n";
                                }
                                return response;
                            },
                            [this](const std::string& func_name, const std::string& func_args_json) -> std::string {
                                return execute_function_with_debug(func_name, func_args_json, 1);
                            },
                            "user", model_name, search_mode_str, active_persona.temperature, &conversation_for_llm, &function_schemas
                        );
                    }, local_llm.instance_);
                    
                    // Process the response
                    if (!chat_completion.choices.empty()) {
                        const auto& response = chat_completion.choices[0].message.content;
                        
                        // Add AI response to history with mutex protection
                        {
                            std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                            chat_history_.emplace_back("assistant", response);
                            message_cache_.emplace_back();
                            layout_dirty_ = true;
                        }
                        maybe_speak_reply(response);
                    }
                    
                } catch (const std::exception& e) {
                    // Add error message to chat history with mutex protection
                    std::string const error_msg = "Error: " + std::string(e.what());
                    {
                        std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", error_msg);
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    }
                }
                
                waiting_for_response_.store(false);
                clear_input_on_response_.store(true);
                scroll_to_bottom_.store(true);
            }).detach();
            
        } catch (const std::exception& e) {
            waiting_for_response_.store(false);
            std::string const error_msg = "Failed to send message: " + std::string(e.what());
            chat_history_.emplace_back("assistant", error_msg);
            message_cache_.emplace_back();
            layout_dirty_ = true;
        }
    }

    void ai_chat::send_message_to_llm(const std::string& message) {
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
            async_context->allow_search = helpers::PersonaManager::instance().get_active_persona().enable_search;
            async_context->temperature = helpers::PersonaManager::instance().get_active_persona().temperature;
            
            // Copy conversation history safely for async operation
            async_context->conversation_snapshot.reserve(chat_history_.size());
            for (const auto& msg : chat_history_) {
                if (msg.first == "user" || msg.first == "assistant") {
                    async_context->conversation_snapshot.emplace_back(msg.first, msg.second);
                }
            }
            
            // Create a shared fetcher instance for this request to avoid accessing member fetcher_
            async_context->fetcher = std::make_shared<http::fetch>(ai_request_timeout_seconds_);
            async_context->fetcher->set_max_retries(3);
            
            // Create a new LLM instance for this async operation rather than copying the existing one
            auto async_llm_instance = helpers::LLMConfig::create_llm_instance();
            if (!async_llm_instance) {
                throw std::runtime_error("Failed to create LLM instance for async operation");
            }
            
            // Add persona instructions
            auto& active_persona = helpers::PersonaManager::instance().get_active_persona();
            async_llm_instance->add_instructions(active_persona.system_prompt);
            std::string const modular_instr = get_modular_mcp_instructions(active_persona.allowed_mcps);
            if (!modular_instr.empty()) {
                async_llm_instance->add_instructions(modular_instr);
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
                    
                    auto const t_start = std::chrono::steady_clock::now();
                    auto response = context->llm_instance_copy.sendMessage(
                        context->user_message, 
                        [fetcher = context->fetcher, log_requests = log_requests_](const std::string& url, const std::string& data, auto header_client) {
                            if (log_requests) {
                                std::cerr << "[LLM Request] URL: " << url << "\n[LLM Request Body]: " << data << "\n";
                            }
                            // Capture fetcher by value to ensure it stays alive
                            auto res = fetcher->post(url, data, header_client);
                            if (log_requests) {
                                std::cerr << "[LLM Response]: " << res << "\n";
                            }
                            return res;
                        },
                        "user", 
                        context->llm_settings.model_name, 
                        search_mode_str, 
                        context->temperature, 
                        &context->conversation_snapshot
                    );
                    auto const t_end = std::chrono::steady_clock::now();
                    last_response_latency_ms_ = std::chrono::duration<double, std::milli>(t_end - t_start).count();
                    total_queries_++;
                    
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
                        std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                        chat_history_.emplace_back("assistant", std::move(reply));
                        message_cache_.emplace_back();
                        layout_dirty_ = true;
                    }
                    maybe_speak_reply(reply_for_tts);
                    
                } catch (const std::bad_alloc&) {
                    std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                    chat_history_.emplace_back("assistant", "Memory allocation error");
                    message_cache_.emplace_back();
                    layout_dirty_ = true;
                } catch (const std::runtime_error& e) {
                    std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                    chat_history_.emplace_back("assistant", std::string("Runtime error: ") + e.what());
                    message_cache_.emplace_back();
                    layout_dirty_ = true;
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> const lock(chat_history_mutex_);
                    chat_history_.emplace_back("assistant", std::string("Error [") + typeid(e).name() + "]: " + e.what());
                    message_cache_.emplace_back();
                    layout_dirty_ = true;
                } catch (...) {
                    std::lock_guard<std::mutex> const lock(chat_history_mutex_);
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

    void ai_chat::process_pending_response() {
        if (pending_response_ && pending_response_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                pending_response_->get(); // This will rethrow any exception from the async task
            } catch (const std::exception& e) {
                // Log or handle any unhandled exceptions from the async task
                std::lock_guard<std::mutex> const lock(chat_history_mutex_);
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

    void ai_chat::recalculate_layout(float width_for_content, bool force_all) {
        std::lock_guard<std::mutex> const lock(chat_history_mutex_);
        
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
        const float max_width = available_width * 0.92f;  // Use 92% of available width to avoid wasted margin
        const float min_width = available_width * 0.10f;  
        const float line_height = ImGui::GetTextLineHeightWithSpacing();
        const float separator_height = ImGui::GetStyle().ItemSpacing.y + 1.0f;
        
        for (size_t i = 0; i < chat_history_.size() && i < message_cache_.size(); ++i) {
            const auto& message = chat_history_[i];
            auto& cache = message_cache_[i];
            
            if (force_all) {
                cache.needs_recalc = true;
            }
            
            if (!cache.needs_recalc) continue;
            
            bool const is_user = message.first == "user";
            float message_height = 0.0f;
            if (is_user) {
                // For user messages, simple plain text CalcTextSize is perfectly accurate
                const float max_text_width = max_width - padding.x * 2.0f - 24.0f;
                ImVec2 const text_size = ImGui::CalcTextSize(message.second.c_str(), nullptr, true, max_text_width);
                message_height = std::max(text_size.y, line_height);
                cache.content_width = std::clamp(text_size.x + padding.x * 2.0f + 24.0f, min_width, max_width);
            } else {
                // For assistant messages, calculate height line-by-line to handle markdown features correctly
                std::istringstream lines_stream{message.second};
                std::string md_line;
                bool in_code_block = false;
                float accumulated_height = 0.0f;
                float max_line_width = 0.0f;
                
                const float max_text_width = max_width - padding.x * 2.0f - 24.0f;
                
                while (std::getline(lines_stream, md_line)) {
                    // 1. Code fence toggle
                    if (md_line.starts_with("```")) {
                        in_code_block = !in_code_block;
                        accumulated_height += ImGui::GetStyle().ItemSpacing.y;
                        continue;
                    }
                    
                    if (in_code_block) {
                        // Code lines are pre-formatted, but can still wrap if extremely long.
                        // Usually they render as-is.
                        ImVec2 const size = ImGui::CalcTextSize(md_line.c_str(), nullptr, true, max_text_width);
                        accumulated_height += std::max(size.y, line_height) + ImGui::GetStyle().ItemSpacing.y;
                        max_line_width = std::max(max_line_width, size.x);
                        continue;
                    }
                    
                    // 2. Headings
                    if (md_line.starts_with("# ")) {
                        std::string_view const content = std::string_view{md_line}.substr(2);
                        ImVec2 const size = ImGui::CalcTextSize(content.data(), content.data() + content.size(), true, max_text_width);
                        accumulated_height += std::max(size.y, line_height) + separator_height + ImGui::GetStyle().ItemSpacing.y * 2.0f;
                        max_line_width = std::max(max_line_width, size.x);
                        continue;
                    }
                    if (md_line.starts_with("## ")) {
                        // SeparatorText
                        accumulated_height += line_height + ImGui::GetStyle().ItemSpacing.y * 3.0f;
                        continue;
                    }
                    if (md_line.starts_with("### ")) {
                        std::string_view const content = std::string_view{md_line}.substr(4);
                        ImVec2 const size = ImGui::CalcTextSize(content.data(), content.data() + content.size(), true, max_text_width);
                        accumulated_height += std::max(size.y, line_height) + ImGui::GetStyle().ItemSpacing.y;
                        max_line_width = std::max(max_line_width, size.x);
                        continue;
                    }
                    
                    // 3. Horizontal rule
                    if (md_line == "---" || md_line == "***" || md_line == "___") {
                        accumulated_height += separator_height + ImGui::GetStyle().ItemSpacing.y;
                        continue;
                    }
                    
                    // 4. Blockquotes
                    if (md_line.starts_with("> ")) {
                        std::string_view const content = std::string_view{md_line}.substr(2);
                        // Indent reduces available width by 20.0f
                        float const wrap_w = std::max(max_text_width - 20.0f, 50.0f);
                        ImVec2 const size = ImGui::CalcTextSize(content.data(), content.data() + content.size(), true, wrap_w);
                        accumulated_height += std::max(size.y, line_height) + ImGui::GetStyle().ItemSpacing.y;
                        max_line_width = std::max(max_line_width, size.x + 20.0f);
                        continue;
                    }
                    
                    // 5. Unordered / Ordered Bullets
                    bool is_bullet = false;
                    std::string_view bullet_content;
                    if (md_line.starts_with("- ") || md_line.starts_with("* ")) {
                        is_bullet = true;
                        bullet_content = std::string_view{md_line}.substr(2);
                    } else {
                        // Check for ordered list "1. "
                        const std::size_t dot = md_line.find(". ");
                        if (dot != std::string::npos && dot > 0 && dot < 5) {
                            bool all_digits = true;
                            for (std::size_t idx = 0; idx < dot; ++idx) {
                                if (md_line[idx] < '0' || md_line[idx] > '9') { all_digits = false; break; }
                            }
                            if (all_digits) {
                                is_bullet = true;
                                bullet_content = std::string_view{md_line}.substr(dot + 2);
                            }
                        }
                    }
                    
                    if (is_bullet) {
                        // Bullet spacing reduces wrap width by 24.0f
                        float const wrap_w = std::max(max_text_width - 24.0f, 50.0f);
                        ImVec2 const size = ImGui::CalcTextSize(bullet_content.data(), bullet_content.data() + bullet_content.size(), true, wrap_w);
                        accumulated_height += std::max(size.y, line_height) + ImGui::GetStyle().ItemSpacing.y;
                        max_line_width = std::max(max_line_width, size.x + 24.0f);
                        continue;
                    }
                    
                    // 6. Tables
                    if (md_line.starts_with("|")) {
                        // Delimiter rows don't add visible height, but table rows do.
                        // CellPadding.y * 2 + border/spacing
                        accumulated_height += line_height + ImGui::GetStyle().CellPadding.y * 2.0f + 4.0f;
                        continue;
                    }
                    
                    // 7. Empty line
                    if (md_line.empty()) {
                        accumulated_height += ImGui::GetStyle().ItemSpacing.y;
                        continue;
                    }
                    
                    // 8. Normal paragraph
                    ImVec2 const size = ImGui::CalcTextSize(md_line.c_str(), nullptr, true, max_text_width);
                    accumulated_height += std::max(size.y, line_height) + ImGui::GetStyle().ItemSpacing.y;
                    max_line_width = std::max(max_line_width, size.x);
                }
                
                message_height = accumulated_height;
                cache.content_width = std::clamp(max_line_width + padding.x * 2.0f + 24.0f, min_width, max_width);
            }
            
            cache.text_width = cache.content_width - padding.x * 2.0f;
            
            // Calculate total bubble height
            cache.bubble_height = line_height + separator_height + message_height + 
                                padding.y * 2.0f + ImGui::GetStyle().ItemSpacing.y + (is_user ? 0.0f : 6.0f);
            
            // Generate unique child ID using message index for better stability
            cache.child_id = "msg_bubble_" + std::to_string(i);
            
            cache.needs_recalc = false;
        }
    }

    std::vector<card::card_performance_metric> ai_chat::get_performance_measurements() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(chat_history_mutex_));
        metric_msg_count_str_ = std::format("{} messages", chat_history_.size());
        metric_latency_str_ = last_response_latency_ms_.load() > 0.0 ? std::format("{:.1f} ms", last_response_latency_ms_.load()) : "N/A";
        metric_queries_str_ = std::format("{}", total_queries_.load());

        return {
            {"Chat history length", metric_msg_count_str_},
            {"Last response latency", metric_latency_str_},
            {"Total queries", metric_queries_str_}
        };
    }

} // namespace rouen::cards
