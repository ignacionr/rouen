#pragma once

#include <string>
#include <vector>
#include <optional>
#include <map>
#include <format>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <functional>
#include "debug.hpp"
#include "cppgpt.hpp"
#include "fetch.hpp"
#include "glaze_include.hpp"

namespace rouen::helpers {

    // Gemini API response structures for glaze parsing
    // Note: Using glz::opts{.error_on_unknown_keys=false} so we only need to define fields we use
    struct GeminiFunctionCall {
        std::string name;
        glz::json_t args; // Use glz::json_t to dynamically parse any JSON structure
        std::string id;
        std::string thoughtSignature;
    };

    struct GeminiPart {
        std::string text;
        std::optional<GeminiFunctionCall> functionCall;
        std::string thoughtSignature;
    };

    struct GeminiContent {
        std::vector<GeminiPart> parts;
        std::string role;
    };

    struct GeminiCandidate {
        GeminiContent content;
        std::string finishReason;
        int index;
    };

    struct GeminiResponse {
        std::vector<GeminiCandidate> candidates;
        // Don't include optional fields - we'll handle unknown_key errors differently
    };

} // namespace rouen::helpers

// Glaze metadata for Gemini API response structures
template <>
struct glz::meta<rouen::helpers::GeminiPart> {
    using T = rouen::helpers::GeminiPart;
    static constexpr auto value = object(
        "text", &T::text,
        "functionCall", &T::functionCall,
        "thoughtSignature", &T::thoughtSignature,
        "thought_signature", &T::thoughtSignature
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiContent> {
    using T = rouen::helpers::GeminiContent;
    static constexpr auto value = object(
        "parts", &T::parts,
        "role", &T::role
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiFunctionCall> {
    using T = rouen::helpers::GeminiFunctionCall;
    static constexpr auto value = object(
        "name", &T::name,
        "args", &T::args,
        "id", &T::id,
        "thoughtSignature", &T::thoughtSignature,
        "thought_signature", &T::thoughtSignature
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiCandidate> {
    using T = rouen::helpers::GeminiCandidate;
    static constexpr auto value = object(
        "content", &T::content,
        "finishReason", &T::finishReason,
        "index", &T::index
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiResponse> {
    using T = rouen::helpers::GeminiResponse;
    static constexpr auto value = object(
        "candidates", &T::candidates
    );
};

namespace rouen::helpers {

    /**
     * Gemini API Adapter
     * Template-compatible adapter for Google's Gemini API
     * Provides the same interface as cppgpt for seamless template usage
     */
    class GeminiAdapter {
    public:
        struct Message {
            struct FunctionCall {
                std::string name;
                std::string args; // JSON string of arguments
                std::string id;
                std::string thought_signature;
            };

            struct FunctionResponse {
                std::string name;
                std::string response; // JSON string of response
            };

            std::string role;
            std::string content;
            std::vector<FunctionCall> function_calls{};
            std::vector<FunctionResponse> function_responses{};

            // Default constructor
            Message() = default;

            // Constructor for standard text messages
            Message(std::string r, std::string c)
                : role(std::move(r)), content(std::move(c)) {}
        };

        // Use the same types as cppgpt for perfect compatibility
        using ChatCompletion = ignacionr::ChatCompletion;
        using ChatCompletionChoice = ignacionr::ChatCompletionChoice;
        using ChatCompletionMessage = ignacionr::ChatCompletionMessage;

    private:
        std::string api_key_;
        std::string model_;
        std::string thinking_level_{};
        int max_output_tokens_{4096};
        std::vector<Message> conversation_;
        static inline std::mutex global_rate_limit_mutex_;
        static inline std::chrono::steady_clock::time_point global_last_request_time_{};
        static constexpr auto min_request_interval_ = std::chrono::milliseconds(1500);

    public:
        struct GeminiKeyVerificationResult {
            bool is_valid{false};
            bool is_paid_tier{false};
            std::string project_container{};
            std::string status_message{};
        };

        struct GeminiQuotaTelemetry {
            bool is_quota_exhausted{false};
            bool is_free_tier{false};
            std::string quota_metric{};
            std::string quota_id{};
            std::string quota_value{};
        };

        static GeminiQuotaTelemetry parse_quota_failure(const std::string& error_json) {
            GeminiQuotaTelemetry telemetry{};
            if (error_json.find("RESOURCE_EXHAUSTED") != std::string::npos ||
                error_json.find("429") != std::string::npos ||
                error_json.find("quota") != std::string::npos ||
                error_json.find("QuotaFailure") != std::string::npos) {
                telemetry.is_quota_exhausted = true;
            }
            if (error_json.find("free_tier") != std::string::npos ||
                error_json.find("FreeTier") != std::string::npos) {
                telemetry.is_free_tier = true;
            }
            glz::json_t doc;
            if (!glz::read_json(doc, error_json)) {
                if (doc.contains("error") && doc["error"].contains("details")) {
                    auto details = doc["error"]["details"];
                    if (details.is_array()) {
                        for (auto& item : details.get<std::vector<glz::json_t>>()) {
                            if (item.contains("violations") && item["violations"].is_array()) {
                                for (auto& v : item["violations"].get<std::vector<glz::json_t>>()) {
                                    if (v.contains("quotaMetric")) {
                                        telemetry.quota_metric = v["quotaMetric"].get<std::string>();
                                    }
                                    if (v.contains("quotaId")) {
                                        telemetry.quota_id = v["quotaId"].get<std::string>();
                                    }
                                    if (v.contains("quotaValue")) {
                                        telemetry.quota_value = v["quotaValue"].get<std::string>();
                                    }
                                    if (telemetry.quota_metric.find("free_tier") != std::string::npos ||
                                        telemetry.quota_id.find("FreeTier") != std::string::npos) {
                                        telemetry.is_free_tier = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return telemetry;
        }

        static GeminiKeyVerificationResult verify_key_tier(const std::string& api_key, std::function<std::string(const std::string&)> custom_fetcher = nullptr) {
            GeminiKeyVerificationResult res{};
            if (api_key.empty()) {
                res.status_message = "API key is empty";
                return res;
            }
            std::string probe_url = std::format("https://translate.googleapis.com/language/translate/v2?key={}", api_key);
            std::string response;
            try {
                if (custom_fetcher) {
                    response = custom_fetcher(probe_url);
                } else {
                    http::fetch fetcher{10};
                    response = fetcher(probe_url);
                }
            } catch (const std::exception& e) {
                response = e.what();
            }

            if (response.find("INVALID_ARGUMENT") != std::string::npos ||
                response.find("API_KEY_INVALID") != std::string::npos ||
                response.find("API key not valid") != std::string::npos ||
                response.find("badRequest") != std::string::npos) {
                res.is_valid = false;
                res.status_message = "Invalid or unrecognized Google API key";
                return res;
            }

            if (response.find("API_KEY_SERVICE_BLOCKED") != std::string::npos) {
                res.is_valid = true;
                res.is_paid_tier = false;
                res.status_message = "AI Studio Free Tier API key (daily quotas apply)";
                return res;
            }

            if (response.find("SERVICE_DISABLED") != std::string::npos) {
                res.is_valid = true;
                res.is_paid_tier = true;
                res.status_message = "Verified Paid Tier 1 (Pay-As-You-Go) key";
                glz::json_t doc;
                if (!glz::read_json(doc, response)) {
                    if (doc.contains("error") && doc["error"].contains("details")) {
                        auto details = doc["error"]["details"];
                        if (details.is_array()) {
                            for (auto& item : details.get<std::vector<glz::json_t>>()) {
                                if (item.contains("metadata") && item["metadata"].contains("consumer")) {
                                    res.project_container = item["metadata"]["consumer"].get<std::string>();
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!res.project_container.empty()) {
                    res.status_message += " linked to " + res.project_container;
                }
                return res;
            }

            if (!response.empty() && response.find("\"error\":") == std::string::npos) {
                res.is_valid = true;
                res.is_paid_tier = true;
                res.status_message = "Verified Tier 1 Google Cloud Project API key";
                return res;
            }

            res.is_valid = true;
            res.is_paid_tier = false;
            res.status_message = "Active Google Gemini API key";
            return res;
        }

    private:
        static inline std::mutex model_exhaustion_mutex_;
        static inline std::map<std::string, std::chrono::steady_clock::time_point> model_exhaustion_cache_;

    public:
        static void mark_model_exhausted(const std::string& model, std::chrono::seconds cooldown = std::chrono::seconds(1800)) {
            std::lock_guard<std::mutex> lock(model_exhaustion_mutex_);
            model_exhaustion_cache_[model] = std::chrono::steady_clock::now() + cooldown;
        }

        static bool is_model_exhausted(const std::string& model) {
            std::lock_guard<std::mutex> lock(model_exhaustion_mutex_);
            auto it = model_exhaustion_cache_.find(model);
            if (it == model_exhaustion_cache_.end()) return false;
            if (std::chrono::steady_clock::now() >= it->second) {
                model_exhaustion_cache_.erase(it);
                return false;
            }
            return true;
        }

        static void clear_model_exhaustion_cache() {
            std::lock_guard<std::mutex> lock(model_exhaustion_mutex_);
            model_exhaustion_cache_.clear();
        }

        void set_max_output_tokens(int tokens) { max_output_tokens_ = tokens; }
        [[nodiscard]] int get_max_output_tokens() const { return max_output_tokens_; }

    private:

        void wait_min_time() {
            std::lock_guard<std::mutex> lock(global_rate_limit_mutex_);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - global_last_request_time_;
            if (elapsed < min_request_interval_) {
                std::this_thread::sleep_for(min_request_interval_ - elapsed);
            }
            global_last_request_time_ = std::chrono::steady_clock::now();
        }

        std::string escape_json(const std::string& str) const {
            std::string escaped;
            escaped.reserve(str.size() + str.size() / 10 + 1); // Reserve extra space for escaping
            
            for (char c : str) {
                switch (c) {
                    case '"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            escaped += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                        } else {
                            escaped += c;
                        }
                        break;
                }
            }
            return escaped;
        }

        std::string build_gemini_request(
            const std::vector<Message>& conversation,
            [[maybe_unused]] float temperature = 0.45f,
            bool enable_search = false,
            std::string_view thinking_level = "",
            int max_output_tokens = 0
        ) const {
            return build_gemini_request(conversation, temperature, {}, enable_search, thinking_level, max_output_tokens);
        }

        // Enhanced method with function calling support
        std::string build_gemini_request(
            const std::vector<Message>& conversation,
            [[maybe_unused]] float temperature,
            const std::vector<std::string>& function_schemas,
            bool enable_search = false,
            std::string_view thinking_level = "",
            int max_output_tokens = 0
        ) const {
            std::string json = "{\"contents\":[";
            
            // Merge system messages and convert to Gemini format
            std::string system_instructions;
            std::vector<Message> user_assistant_messages;
            
            for (const auto& msg : conversation) {
                if (msg.role == "system") {
                    if (!system_instructions.empty()) {
                        system_instructions += "\n\n";
                    }
                    system_instructions += msg.content;
                } else {
                    user_assistant_messages.push_back(msg);
                }
            }
            
            // Ensure first non-system message is from role 'user' (Gemini API requirement) if a user/function message exists
            bool has_user_msg = std::any_of(user_assistant_messages.begin(), user_assistant_messages.end(), [](const Message& m) {
                return m.role == "user" || m.role == "human" || m.role == "function";
            });
            if (has_user_msg) {
                while (!user_assistant_messages.empty() && 
                       user_assistant_messages[0].role != "user" && 
                       user_assistant_messages[0].role != "human" &&
                       user_assistant_messages[0].role != "function") {
                    user_assistant_messages.erase(user_assistant_messages.begin());
                }
            }

            // Prepend system instructions to first user message (or insert as first user message)
            if (!system_instructions.empty()) {
                if (!user_assistant_messages.empty() && (user_assistant_messages[0].role == "user" || user_assistant_messages[0].role == "human")) {
                    user_assistant_messages[0].content = system_instructions + "\n\n" + user_assistant_messages[0].content;
                } else {
                    user_assistant_messages.insert(user_assistant_messages.begin(), Message{"user", system_instructions});
                }
            }
            
            // Build contents array
            for (size_t i = 0; i < user_assistant_messages.size(); ++i) {
                if (i > 0) json += ",";
                const auto& msg = user_assistant_messages[i];
                
                std::string gemini_role;
                if (msg.role == "assistant" || msg.role == "model") {
                    gemini_role = "model";
                } else {
                    gemini_role = "user";
                }
                
                json += std::format("{{\"role\":\"{}\",\"parts\":[", gemini_role);
                
                bool first_part = true;
                if (!msg.content.empty() || (msg.function_calls.empty() && msg.function_responses.empty())) {
                    json += std::format("{{\"text\":\"{}\"}}", escape_json(msg.content));
                    first_part = false;
                }
                
                // Add function calls
                for (const auto& fc : msg.function_calls) {
                    if (!first_part) json += ",";
                    std::string args_json = fc.args.empty() ? "{}" : fc.args;
                    json += "{\"functionCall\":{";
                    json += std::format("\"name\":\"{}\",\"args\":{}", fc.name, args_json);
                    if (!fc.id.empty()) {
                        json += std::format(",\"id\":\"{}\"", fc.id);
                    }
                    json += "}";
                    if (!fc.thought_signature.empty()) {
                        json += std::format(",\"thoughtSignature\":\"{}\"", escape_json(fc.thought_signature));
                    }
                    json += "}";
                    first_part = false;
                }
                
                // Add function responses
                for (const auto& fr : msg.function_responses) {
                    if (!first_part) json += ",";
                    std::string resp_json;
                    std::string_view resp_view(fr.response);
                    auto first = resp_view.find_first_not_of(" \t\r\n");
                    auto last = resp_view.find_last_not_of(" \t\r\n");
                    std::string_view trimmed = (first != std::string_view::npos) 
                        ? resp_view.substr(first, last - first + 1) 
                        : std::string_view{};

                    if (!trimmed.empty() && trimmed.front() == '{' && !glz::validate_json(trimmed)) {
                        resp_json = std::format("{{\"name\":\"{}\",\"response\":{}}}", escape_json(fr.name), trimmed);
                    } else if (!trimmed.empty() && trimmed.front() == '[' && !glz::validate_json(trimmed)) {
                        resp_json = std::format("{{\"name\":\"{}\",\"response\":{{\"result\":{}}}}}", escape_json(fr.name), trimmed);
                    } else {
                        resp_json = std::format("{{\"name\":\"{}\",\"response\":{{\"result\":\"{}\"}}}}", 
                                               escape_json(fr.name), escape_json(fr.response));
                    }
                    json += std::format("{{\"functionResponse\":{}}}", resp_json);
                    first_part = false;
                }
                
                json += "]}";
            }
            
            int const eff_tokens = max_output_tokens > 0 ? max_output_tokens : max_output_tokens_;
            std::string const eff_thinking = !thinking_level.empty() ? std::string(thinking_level) : thinking_level_;
            json += std::format("],\"generationConfig\":{{\"maxOutputTokens\":{}", eff_tokens);
            if (!eff_thinking.empty() && eff_thinking != "off") {
                int budget = -1;
                if (eff_thinking == "minimal") budget = 512;
                else if (eff_thinking == "low") budget = 1024;
                else if (eff_thinking == "medium") budget = 2048;
                else if (eff_thinking == "high") budget = 4096;
                else {
                    try {
                        budget = std::stoi(eff_thinking);
                    } catch (...) {
                        budget = -1;
                    }
                }
                if (budget != 0) {
                    json += std::format(",\"thinkingConfig\":{{\"thinkingBudget\":{}}}", budget);
                }
            }
            json += "}";
            
            // Add tools if search or function calling is enabled
            if (enable_search || !function_schemas.empty()) {
                json += ",\"tools\":[";
                bool has_previous_tool = false;
                
                if (enable_search) {
                    json += "{\"google_search\":{}}";
                    has_previous_tool = true;
                }
                
                if (!function_schemas.empty()) {
                    if (has_previous_tool) json += ",";
                    json += "{\"functionDeclarations\":[";
                    for (size_t i = 0; i < function_schemas.size(); ++i) {
                        if (i > 0) json += ",";
                        json += function_schemas[i];
                    }
                    json += "]}";
                }
                
                json += "]";
            }
            
            json += "}";  // Close the main JSON object
            
            CONFIG_DEBUG_FMT("Built Gemini JSON request: {}", json);
            return json;
        }

        // Backward compatibility method that uses local conversation_
        std::string build_gemini_request(
            [[maybe_unused]] float temperature = 0.45f,
            bool enable_search = false,
            std::string_view thinking_level = ""
        ) const {
            return build_gemini_request(conversation_, temperature, enable_search, thinking_level);
        }

        // Parse Gemini response and return the full response structure for function call handling
        GeminiResponse parse_gemini_response_full(const std::string& response) const {
            CONFIG_DEBUG_FMT("Parsing Gemini response: {}", response);
            
            // Check for top-level API error JSON structure
            glz::json_t err_doc;
            if (!glz::read_json(err_doc, response)) {
                if (err_doc.contains("error")) {
                    auto err_obj = err_doc["error"];
                    int code = 0;
                    std::string status;
                    std::string message;
                    if (err_obj.contains("code") && err_obj["code"].holds<double>()) {
                        code = static_cast<int>(err_obj["code"].get<double>());
                    }
                    if (err_obj.contains("status") && err_obj["status"].holds<std::string>()) {
                        status = err_obj["status"].get<std::string>();
                    }
                    if (err_obj.contains("message") && err_obj["message"].holds<std::string>()) {
                        message = err_obj["message"].get<std::string>();
                    }
                    if (code == 429 || status == "RESOURCE_EXHAUSTED" || message.find("quota") != std::string::npos || message.find("RESOURCE_EXHAUSTED") != std::string::npos) {
                        throw std::runtime_error("HTTP error 429: Resource exhausted / rate limit");
                    }
                    throw std::runtime_error(std::format("Gemini API Error ({}): {}", status.empty() ? std::to_string(code) : status, message.empty() ? response : message));
                }
            } else if (response.starts_with("HTTP error 429") || response.starts_with("Error: HTTP error 429")) {
                throw std::runtime_error("HTTP error 429: Resource exhausted / rate limit");
            }
            
            try {
                CONFIG_DEBUG("Creating GeminiResponse object");
                GeminiResponse gemini_response;
                
                // 1. Direct parsing with Glaze (fast and handles arbitrary valid JSON with unknown keys ignored)
                auto direct_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(gemini_response, response);
                if (!direct_err && !gemini_response.candidates.empty()) {
                    return gemini_response;
                }

                // 2. Fallback: manually clean JSON to only keep candidates
                std::string cleaned_response = "{\"candidates\":";
                
                // Find the candidates array
                auto candidates_start = response.find("\"candidates\":");
                if (candidates_start == std::string::npos) {
                    throw std::runtime_error("No candidates found in response");
                }
                
                // Find the start of the array
                auto array_start = response.find('[', candidates_start);
                if (array_start == std::string::npos) {
                    throw std::runtime_error("Malformed candidates array");
                }
                
                // Find the end of the candidates array
                auto array_end = array_start + 1;
                int bracket_count = 1;
                bool in_string = false;
                
                for (size_t i = array_start + 1; i < response.length() && bracket_count > 0; ++i) {
                    char c = response[i];
                    if (c == '"') {
                        size_t bs_count = 0;
                        for (size_t k = i; k > 0 && response[k - 1] == '\\'; --k) {
                            bs_count++;
                        }
                        if (bs_count % 2 == 0) {
                            in_string = !in_string;
                        }
                    } else if (!in_string) {
                        if (c == '[') {
                            bracket_count++;
                        } else if (c == ']') {
                            bracket_count--;
                            if (bracket_count == 0) {
                                array_end = i + 1;
                                break;
                            }
                        }
                    }
                }
                
                // Extract just the candidates array
                std::string candidates_array = response.substr(array_start, array_end - array_start);
                cleaned_response += candidates_array + "}";
                
                CONFIG_DEBUG_FMT("Cleaned JSON: {}", cleaned_response);
                
                CONFIG_DEBUG("Calling glz::read on cleaned response");
                auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(gemini_response, cleaned_response);
                
                if (error) {
                    CONFIG_ERROR_FMT("Failed to parse cleaned Gemini JSON response: {}", glz::format_error(error, cleaned_response));
                    throw std::runtime_error("Invalid Gemini response JSON format");
                }
                
                CONFIG_DEBUG("Checking candidates");
                if (gemini_response.candidates.empty()) {
                    CONFIG_ERROR("No candidates found in Gemini response");
                    throw std::runtime_error("Gemini response contains no candidates");
                }
                
                return gemini_response;
                
            } catch (const std::exception& e) {
                CONFIG_ERROR_FMT("Exception parsing Gemini response: {}", e.what());
                CONFIG_DEBUG_FMT("Full response for debugging: {}", response);
                throw;
            }
        }

        std::string parse_gemini_response(const std::string& response) const {
            auto gemini_response = parse_gemini_response_full(response);
            
            CONFIG_DEBUG("Getting candidate reference");
            const auto& candidate = gemini_response.candidates[0];
            
            CONFIG_DEBUG("Checking parts");
            if (candidate.content.parts.empty()) {
                CONFIG_WARN("No parts found in Gemini candidate content");
                return "";
            }
            
            CONFIG_DEBUG("Extracting text");
            // Make a copy of the text to avoid dangling reference when gemini_response goes out of scope
            std::string text = candidate.content.parts[0].text;
            
            CONFIG_DEBUG_FMT("Successfully extracted text: {}", text.substr(0, 50) + "...");
            CONFIG_DEBUG_FMT("Text length: {}", text.length());
            
            CONFIG_DEBUG("About to return text");
            return text;
        }

        static std::string trim(std::string_view str) {
            auto first = str.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return "";
            auto last = str.find_last_not_of(" \t\r\n");
            return std::string(str.substr(first, last - first + 1));
        }

    public:
        GeminiAdapter(const std::string& api_key, [[maybe_unused]] const std::string& base_url = "", std::string thinking_level = "") 
            : api_key_(trim(api_key)), model_("gemini-3.8-flash"), thinking_level_(std::move(thinking_level)) {
            CONFIG_DEBUG_FMT("Created Gemini adapter with API key: {}...", api_key_.empty() ? "" : api_key_.substr(0, std::min(size_t(8), api_key_.length())));
        }

        void set_thinking_level(std::string_view level) {
            thinking_level_ = std::string(level);
        }

        [[nodiscard]] const std::string& get_thinking_level() const noexcept {
            return thinking_level_;
        }

        GeminiAdapter new_conversation() const {
            auto adapter = GeminiAdapter(api_key_, "", thinking_level_);
            adapter.model_ = model_;
            return adapter;
        }

        void add_instructions(std::string_view instructions, std::string_view role = "system") {
            conversation_.push_back({std::string(role), std::string(instructions)});
        }
        
        void clear() {
            conversation_.clear();
        }

        // Template-compatible sendMessage method that returns a cppgpt-compatible response structure
        template<typename DoPostFunc>
        ChatCompletion sendMessage(
            std::string_view message, 
            DoPostFunc do_post, 
            std::string_view role = "user", 
            std::string_view model = "gemini-3.8-flash", 
            std::string_view search_mode = {},
            [[maybe_unused]] float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr,
            const std::vector<std::string>* function_schemas = nullptr,
            std::string_view thinking_level = ""
        ) {
            wait_min_time();
            
            // Build conversation either from full_conversation or local history
            std::vector<Message> current_conversation;
            
            if (full_conversation) {
                // Use the provided full conversation history
                current_conversation.reserve(conversation_.size() + full_conversation->size() + 1);
                // Copy any system instructions from the local conversation_
                for (const auto& local_msg : conversation_) {
                    if (local_msg.role == "system") {
                        current_conversation.push_back(local_msg);
                    }
                }
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    current_conversation.emplace_back(msg_role, msg_content);
                }
                // Add the new message only if it's not already the last message in the history
                if (current_conversation.empty() || current_conversation.back().content != message) {
                    current_conversation.emplace_back(std::string(role), std::string(message));
                }
            } else {
                // Fallback to local conversation + new message
                current_conversation = conversation_;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            bool enable_search = (search_mode == "on");
            // Build Gemini API request using current_conversation and function schemas
            std::string request_body = function_schemas ? 
                build_gemini_request(current_conversation, temperature, *function_schemas, enable_search, thinking_level) :
                build_gemini_request(current_conversation, temperature, enable_search, thinking_level);

            // Use the provided model or default
            std::string model_name = model.empty() ? model_ : std::string(model);
            
            // Build URL for Gemini API
            auto url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                 model_name, api_key_);

            CONFIG_DEBUG_FMT("Sending Gemini request to: {}", url);

            // Make the HTTP request with candidate model loop
            std::vector<std::string> candidates = {model_name};
            auto add_candidate = [&](const std::string& c) {
                if (std::find(candidates.begin(), candidates.end(), c) == candidates.end()) {
                    candidates.push_back(c);
                }
            };
            add_candidate("gemini-3.8-flash");
            add_candidate("gemini-3.1-flash-lite");
            add_candidate("gemini-flash-lite-latest");
            add_candidate("gemini-2.5-flash-lite");
            add_candidate("gemini-3.6-flash");
            add_candidate("gemini-flash-latest");
            add_candidate("gemini-3-flash-preview");

            std::string response;
            bool request_ok = false;
            std::exception_ptr last_err;

            std::vector<std::string> active_candidates;
            for (const auto& c : candidates) {
                if (!is_model_exhausted(c)) {
                    active_candidates.push_back(c);
                }
            }
            if (active_candidates.empty()) {
                active_candidates = candidates;
            }

            std::string result;
            for (const auto& try_model : active_candidates) {
                auto try_url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                           try_model, api_key_);
                for (int attempt = 0; attempt < 3; ++attempt) {
                    try {
                        wait_min_time();
                        response = do_post(try_url, request_body, [](auto header_setter) {
                            header_setter("Content-Type: application/json");
                        });
                        result = parse_gemini_response(response);
                        request_ok = true;
                        model_name = try_model;
                        url = try_url;
                        break;
                    } catch (const std::exception& e) {
                        last_err = std::current_exception();
                        std::string const err_str = e.what();
                        if (err_str.find("RESOURCE_EXHAUSTED") != std::string::npos || 
                            err_str.find("quota") != std::string::npos || 
                            err_str.find("429") != std::string::npos) {
                            mark_model_exhausted(try_model);
                            CONFIG_WARN_FMT("Model {} rate limited or quota exhausted (429/RESOURCE_EXHAUSTED), marked in cooldown cache. Trying next candidate...", try_model);
                            break;
                        }
                        bool const is_retryable = (err_str.find("503") != std::string::npos ||
                                                   err_str.find("404") != std::string::npos ||
                                                   err_str.find("400") != std::string::npos ||
                                                   err_str.find("NOT_FOUND") != std::string::npos);
                        if (!is_retryable) {
                            std::rethrow_exception(last_err);
                        }
                        CONFIG_WARN_FMT("Model {} failed with '{}', trying next candidate...", try_model, err_str);
                        break;
                    }
                }
                if (request_ok) break;
            }
            if (!request_ok && last_err) {
                std::rethrow_exception(last_err);
            }
            
            // Only add to local conversation if not using external conversation management
            if (!full_conversation) {
                conversation_.push_back({"assistant", result});
            }
            
            // Return cppgpt-compatible response structure
            ChatCompletion chat_completion{};
            chat_completion.choices.resize(1);
            chat_completion.choices[0].index = 0;
            chat_completion.choices[0].message.role = "assistant";
            chat_completion.choices[0].message.content = result;
            chat_completion.choices[0].finish_reason = "stop";
            
            return chat_completion;
        }

        // Extended sendMessage that handles function calling responses
        template<typename DoPostFunc>
        ChatCompletion sendMessageWithFunctionCalling(
            std::string_view message, 
            DoPostFunc do_post, 
            std::function<std::string(const std::string&, const std::string&)> function_executor,
            std::string_view role = "user", 
            std::string_view model = "gemini-3.8-flash", 
            std::string_view search_mode = {},
            [[maybe_unused]] float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr,
            const std::vector<std::string>* function_schemas = nullptr,
            std::string_view thinking_level = "",
            int max_iterations = 25,
            int max_output_tokens = 8192
        ) {
            wait_min_time();
            
            // Build conversation either from full_conversation or local history
            std::vector<Message> current_conversation;
            
            if (full_conversation) {
                // Use the provided full conversation history
                current_conversation.reserve(conversation_.size() + full_conversation->size() + 1);
                // Copy any system instructions from the local conversation_
                for (const auto& local_msg : conversation_) {
                    if (local_msg.role == "system") {
                        current_conversation.push_back(local_msg);
                    }
                }
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    current_conversation.emplace_back(msg_role, msg_content);
                }
                // Add the new message only if it's not already the last message in the history
                if (current_conversation.empty() || current_conversation.back().content != message) {
                    current_conversation.emplace_back(std::string(role), std::string(message));
                }
            } else {
                // Fallback to local conversation + new message
                current_conversation = conversation_;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            bool enable_search = (search_mode == "on");
            
            // Use the provided model or default
            std::string model_name = model.empty() ? model_ : std::string(model);
            
            // Build URL for Gemini API
            auto url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                 model_name, api_key_);

            std::string final_text;
            bool keep_calling = true;
            int iterations = 0;
            const int eff_max_iterations = max_iterations > 0 ? max_iterations : 25;
            
            // Make the candidate model loop list
            std::vector<std::string> candidates = {model_name};
            auto add_candidate = [&](const std::string& c) {
                if (std::find(candidates.begin(), candidates.end(), c) == candidates.end()) {
                    candidates.push_back(c);
                }
            };
            add_candidate("gemini-3.8-flash");
            add_candidate("gemini-3.1-flash-lite");
            add_candidate("gemini-flash-lite-latest");
            add_candidate("gemini-2.5-flash-lite");
            add_candidate("gemini-3.6-flash");
            add_candidate("gemini-flash-latest");
            add_candidate("gemini-3-flash-preview");

            while (keep_calling && iterations < eff_max_iterations) {
                iterations++;
                wait_min_time();
                
                // Build Gemini API request using current_conversation and function schemas
                std::string request_body = function_schemas ? 
                    build_gemini_request(current_conversation, temperature, *function_schemas, enable_search, thinking_level, max_output_tokens) :
                    build_gemini_request(current_conversation, temperature, enable_search, thinking_level, max_output_tokens);

                CONFIG_DEBUG_FMT("Sending Gemini request (iteration {}) to: {}", iterations, url);

                std::string response;
                bool request_ok = false;
                std::exception_ptr last_err;

                std::vector<std::string> active_candidates;
                for (const auto& c : candidates) {
                    if (!is_model_exhausted(c)) {
                        active_candidates.push_back(c);
                    }
                }
                if (active_candidates.empty()) {
                    active_candidates = candidates;
                }

                GeminiResponse gemini_response;
                for (const auto& try_model : active_candidates) {
                    auto try_url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                               try_model, api_key_);
                    for (int attempt = 0; attempt < 3; ++attempt) {
                        try {
                            wait_min_time();
                            response = do_post(try_url, request_body, [](auto header_setter) {
                                header_setter("Content-Type: application/json");
                            });
                            gemini_response = parse_gemini_response_full(response);
                            request_ok = true;
                            model_name = try_model;
                            url = try_url;
                            break;
                        } catch (const std::exception& e) {
                            last_err = std::current_exception();
                            std::string const err_str = e.what();
                            if (err_str.find("RESOURCE_EXHAUSTED") != std::string::npos || 
                                err_str.find("quota") != std::string::npos || 
                                err_str.find("429") != std::string::npos) {
                                mark_model_exhausted(try_model);
                                CONFIG_WARN_FMT("Model {} rate limited or quota exhausted (429/RESOURCE_EXHAUSTED), marked in cooldown cache. Trying next candidate...", try_model);
                                break;
                            }
                            bool const is_retryable = (err_str.find("503") != std::string::npos ||
                                                       err_str.find("404") != std::string::npos ||
                                                       err_str.find("400") != std::string::npos ||
                                                       err_str.find("NOT_FOUND") != std::string::npos);
                            if (!is_retryable) {
                                std::rethrow_exception(last_err);
                            }
                            CONFIG_WARN_FMT("Model {} failed with '{}', trying next candidate...", try_model, err_str);
                            break;
                        }
                    }
                    if (request_ok) break;
                }
                if (!request_ok && last_err) {
                    std::rethrow_exception(last_err);
                }

                if (gemini_response.candidates.empty()) {
                    throw std::runtime_error("Gemini response contains no candidates");
                }
                
                const auto& candidate = gemini_response.candidates[0];
                
                // Check if candidate content has function calls
                std::vector<Message::FunctionCall> current_calls;
                std::string turn_text;
                
                for (const auto& part : candidate.content.parts) {
                    if (part.functionCall.has_value()) {
                        const auto& fc = part.functionCall.value();
                        // Serialize arguments to JSON string
                        std::string args_json;
                        auto write_err = glz::write_json(fc.args, args_json);
                        if (write_err) {
                            CONFIG_ERROR_FMT("Failed to serialize function arguments: {}", glz::format_error(write_err));
                            args_json = "{}";
                        }
                        
                        std::string sig = !part.thoughtSignature.empty() ? part.thoughtSignature : fc.thoughtSignature;
                        current_calls.push_back({fc.name, args_json, fc.id, sig});
                    }
                    if (!part.text.empty()) {
                        turn_text += part.text;
                    }
                }
                
                if (!current_calls.empty()) {
                    // 1. Add model turn containing functionCall to history
                    Message model_msg;
                    model_msg.role = "model";
                    model_msg.content = turn_text;
                    model_msg.function_calls = current_calls;
                    current_conversation.push_back(model_msg);
                    
                    // 2. Execute each function call and collect responses
                    Message response_msg;
                    response_msg.role = "function"; // Maps to role: "user" in build_gemini_request
                    
                    for (const auto& call : current_calls) {
                        CONFIG_DEBUG_FMT("Executing function: {} with args: {}", call.name, call.args);
                        std::string result;
                        try {
                            result = function_executor(call.name, call.args);
                        } catch (const std::exception& e) {
                            result = std::format("{{\"error\":\"{}\"}}", e.what());
                        }
                        response_msg.function_responses.push_back({call.name, result});
                    }
                    
                    // 3. Add function response turn to history
                    current_conversation.push_back(response_msg);
                    
                    // Continue the loop to send the response back to Gemini
                    keep_calling = true;
                } else {
                    // No more function calls, we have the final text!
                    final_text = turn_text;
                    keep_calling = false;
                }
            }
            
            // If the loop finished due to hitting the iteration limit while still calling tools,
            // execute a final synthesis turn without tools so the model synthesizes its findings.
            if (keep_calling && iterations >= eff_max_iterations && final_text.empty()) {
                CONFIG_WARN_FMT("Reached max_iterations ({}), requesting final synthesis without tools...", eff_max_iterations);
                
                // 1. Determine original user request to remind the model
                std::string orig_query = std::string(message);
                if (orig_query.empty()) {
                    for (const auto& m : current_conversation) {
                        if ((m.role == "user" || m.role == "human") && !m.content.empty()) {
                            orig_query = m.content;
                            break;
                        }
                    }
                }

                // 2. Append explicit synthesis prompt so the model focuses on answering the original user request
                std::vector<Message> synth_conversation = current_conversation;
                synth_conversation.emplace_back("user", std::format(
                    "You have reached the tool execution limit for this turn. "
                    "Based on all the steps and tool outputs gathered above, provide a comprehensive final response directly addressing the original request: \"{}\". "
                    "Do NOT request any further tool calls. Report the concrete actions executed, current state, and exact findings. "
                    "Do NOT ask the user for confirmation, permission, or assistance on whether to continue, and do NOT offer speculative planning documentation about what should be next.",
                    orig_query
                ));

                try {
                    std::string synth_req = build_gemini_request(synth_conversation, temperature, enable_search, thinking_level, max_output_tokens);
                    bool synth_ok = false;
                    for (const auto& try_model : candidates) {
                        auto try_url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                                   try_model, api_key_);
                        for (int attempt = 0; attempt < 3; ++attempt) {
                            try {
                                wait_min_time();
                                std::string synth_res = do_post(try_url, synth_req, [](auto header_setter) {
                                    header_setter("Content-Type: application/json");
                                });
                                auto synth_data = parse_gemini_response_full(synth_res);
                                if (!synth_data.candidates.empty()) {
                                    for (const auto& part : synth_data.candidates[0].content.parts) {
                                        if (!part.text.empty()) {
                                            final_text += part.text;
                                        }
                                    }
                                }
                                if (!final_text.empty()) {
                                    synth_ok = true;
                                    break;
                                }
                            } catch (const std::exception& e) {
                                std::string const err_str = e.what();
                                if (err_str.find("RESOURCE_EXHAUSTED") != std::string::npos || 
                                    err_str.find("quota") != std::string::npos || 
                                    err_str.find("429") != std::string::npos) {
                                    mark_model_exhausted(try_model);
                                    CONFIG_WARN_FMT("Synthesis attempt on model {} rate limited, marked in cooldown cache. Trying next candidate...", try_model);
                                    break;
                                }
                                bool const is_retryable = (err_str.find("503") != std::string::npos ||
                                                           err_str.find("404") != std::string::npos ||
                                                           err_str.find("400") != std::string::npos ||
                                                           err_str.find("NOT_FOUND") != std::string::npos);
                                if (!is_retryable) {
                                    break;
                                }
                                CONFIG_WARN_FMT("Synthesis attempt on model {} failed: {}, trying next candidate...", try_model, err_str);
                                break;
                            }
                        }
                        if (synth_ok) break;
                    }
                } catch (const std::exception& e) {
                    CONFIG_WARN_FMT("Final synthesis turn error: {}", e.what());
                }
            }

            // Fallback if final_text is empty after function calling
            if (final_text.empty()) {
                // 1. Try to extract the last non-empty model text from conversation history
                for (auto it = current_conversation.rbegin(); it != current_conversation.rend(); ++it) {
                    if (it->role == "model" && !it->content.empty()) {
                        final_text = it->content;
                        break;
                    }
                }

                // 2. If still empty, inspect the collected tool execution responses to construct
                // an informative, factual response instead of a generic canned placeholder.
                if (final_text.empty()) {
                    std::vector<std::pair<std::string, std::string>> collected_responses;
                    for (const auto& msg : current_conversation) {
                        if (msg.role == "function") {
                            for (const auto& fr : msg.function_responses) {
                                if (!fr.response.empty()) {
                                    collected_responses.push_back({fr.name, fr.response});
                                }
                            }
                        }
                    }
                    if (!collected_responses.empty()) {
                        // Extract cleanly formatted messages/outputs from tool responses
                        const auto& [last_name, last_resp] = collected_responses.back();
                        std::string clean_resp = last_resp;
                        try {
                            glz::json_t doc;
                            if (!glz::read_json(doc, last_resp)) {
                                if (doc.contains("message") && doc["message"].is_string()) {
                                    clean_resp = doc["message"].get<std::string>();
                                } else if (doc.contains("output") && doc["output"].is_string()) {
                                    clean_resp = doc["output"].get<std::string>();
                                } else if (doc.contains("result") && doc["result"].is_string()) {
                                    clean_resp = doc["result"].get<std::string>();
                                }
                            }
                        } catch (...) {}

                        if (collected_responses.size() == 1) {
                            final_text = clean_resp;
                        } else {
                            final_text = std::format("Executed {} tool operations (last tool: `{}`). Output:\n\n{}", 
                                                     collected_responses.size(), last_name, clean_resp);
                        }
                    }
                }
            }

            // Only add to local conversation if not using external conversation management
            if (!full_conversation) {
                conversation_.push_back({"assistant", final_text});
            }
            
            // Return cppgpt-compatible response structure
            ChatCompletion chat_completion{};
            chat_completion.choices.resize(1);
            chat_completion.choices[0].index = 0;
            chat_completion.choices[0].message.role = "assistant";
            chat_completion.choices[0].message.content = final_text;
            chat_completion.choices[0].finish_reason = "stop";
            
            return chat_completion;
        }
    };

} // namespace rouen::helpers
