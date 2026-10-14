#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "persona_manager.hpp"

namespace rouen::helpers {

    struct CompactionResult {
        std::vector<std::pair<std::string, std::string>> compacted_conversation;
        std::string summary;
        size_t compacted_turns{0};
        bool was_compacted{false};
    };

    class ContextCompactor {
    public:
        static constexpr std::string_view kSummaryHeaderPrefix = "[Conversation Context & Earlier Memory Summary]:";

        using SummarizerFunc = std::function<std::string(const std::string& prompt, const std::string& conversation_text)>;

        /**
         * Formats conversation turns into a clean readable dialogue string for summarization.
         */
        static std::string format_conversation_for_summary(
            const std::vector<std::pair<std::string, std::string>>& turns,
            size_t start_idx = 0,
            size_t end_idx = static_cast<size_t>(-1)
        ) {
            if (turns.empty()) return "";
            if (end_idx > turns.size()) end_idx = turns.size();

            std::string out;
            out.reserve(2048);

            for (size_t i = start_idx; i < end_idx; ++i) {
                const auto& [role, content] = turns[i];
                if (role == "user") {
                    out += "User: ";
                } else if (role == "assistant") {
                    out += "Assistant: ";
                } else if (role == "system") {
                    out += "System/Context: ";
                } else {
                    out += role + ": ";
                }
                out += content;
                out += "\n\n";
            }
            return out;
        }

        /**
         * Generates an intelligent, deterministic structured fallback summary when an LLM
         * instance is unavailable, unconfigured, or fails over network timeout.
         */
        static std::string generate_fallback_summary(
            const std::vector<std::pair<std::string, std::string>>& older_turns
        ) {
            if (older_turns.empty()) return "";

            std::vector<std::string> user_goals;
            std::vector<std::string> technical_facts;
            std::vector<std::string> files_referenced;

            auto trim_line = [](std::string_view s) -> std::string {
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
                return std::string(s);
            };

            for (const auto& [role, content] : older_turns) {
                if (content.empty()) continue;

                // Extract referenced paths (e.g. paths with / or file extensions)
                std::istringstream stream(content);
                std::string token;
                while (stream >> token) {
                    while (!token.empty() && (token.front() == '`' || token.front() == '\'' || token.front() == '"' || token.front() == '(')) {
                        token.erase(0, 1);
                    }
                    while (!token.empty() && (token.back() == '`' || token.back() == '\'' || token.back() == '"' || token.back() == ')' || token.back() == '.' || token.back() == ',' || token.back() == ':')) {
                        token.pop_back();
                    }
                    if ((token.find('/') != std::string::npos || 
                         token.ends_with(".cpp") || token.ends_with(".hpp") || token.ends_with(".h") || 
                         token.ends_with(".json") || token.ends_with(".md") || token.ends_with(".py") || 
                         token.ends_with(".sh")) && token.size() > 3) {
                        if (std::find(files_referenced.begin(), files_referenced.end(), token) == files_referenced.end()) {
                            files_referenced.push_back(token);
                        }
                    }
                }

                if (role == "user") {
                    std::string first_line = content;
                    size_t newline = first_line.find('\n');
                    if (newline != std::string::npos) first_line = first_line.substr(0, newline);
                    first_line = trim_line(first_line);
                    if (!first_line.empty() && first_line.size() > 4) {
                        if (first_line.size() > 140) {
                            first_line = first_line.substr(0, 137) + "...";
                        }
                        if (std::find(user_goals.begin(), user_goals.end(), first_line) == user_goals.end()) {
                            user_goals.push_back(first_line);
                        }
                    }
                } else if (role == "assistant") {
                    // Extract technical decision or outcome keywords
                    if (content.find("pass") != std::string::npos || content.find("fix") != std::string::npos ||
                        content.find("implement") != std::string::npos || content.find("refactor") != std::string::npos ||
                        content.find("port") != std::string::npos || content.find("decid") != std::string::npos) {
                        std::string first_line = content;
                        size_t newline = first_line.find('\n');
                        if (newline != std::string::npos) first_line = first_line.substr(0, newline);
                        first_line = trim_line(first_line);
                        if (!first_line.empty() && first_line.size() > 4) {
                            if (first_line.size() > 140) {
                                first_line = first_line.substr(0, 137) + "...";
                            }
                            if (std::find(technical_facts.begin(), technical_facts.end(), first_line) == technical_facts.end()) {
                                technical_facts.push_back(first_line);
                            }
                        }
                    }
                }
            }

            std::string summary;
            summary.reserve(1024);

            if (!user_goals.empty()) {
                summary += "- Core user goals & topics discussed:\n";
                size_t count = 0;
                for (const auto& g : user_goals) {
                    summary += "  • " + g + "\n";
                    if (++count >= 5) break;
                }
            }

            if (!technical_facts.empty()) {
                summary += "- Technical decisions & confirmed outcomes:\n";
                size_t count = 0;
                for (const auto& f : technical_facts) {
                    summary += "  • " + f + "\n";
                    if (++count >= 5) break;
                }
            }

            if (!files_referenced.empty()) {
                summary += "- Files & components referenced: ";
                for (size_t i = 0; i < files_referenced.size() && i < 6; ++i) {
                    if (i > 0) summary += ", ";
                    summary += files_referenced[i];
                }
                summary += "\n";
            }

            if (summary.empty()) {
                summary = std::format("- {} earlier conversation turns archived into context memory.\n", older_turns.size());
            }

            return summary;
        }

        /**
         * Compacts conversation history if it exceeds the persona's max_context_turns threshold.
         */
        static CompactionResult compact(
            const std::vector<std::pair<std::string, std::string>>& conversation,
            const Persona& persona,
            const std::string& existing_summary = "",
            const SummarizerFunc& custom_summarizer = nullptr
        ) {
            CompactionResult result;
            result.compacted_conversation = conversation;

            if (persona.context_compaction_strategy == "none") {
                result.was_compacted = false;
                result.compacted_turns = 0;
                return result;
            }

            size_t max_turns = persona.max_context_turns > 0 ? static_cast<size_t>(persona.max_context_turns) : 30;
            size_t preserve = persona.preserve_recent_turns > 0 ? static_cast<size_t>(persona.preserve_recent_turns) : 10;

            if (conversation.size() <= max_turns) {
                result.was_compacted = false;
                result.compacted_turns = 0;
                return result;
            }

            if (preserve >= conversation.size()) {
                result.was_compacted = false;
                result.compacted_turns = 0;
                return result;
            }

            size_t older_count = conversation.size() - preserve;

            if (persona.context_compaction_strategy == "sliding_window") {
                result.was_compacted = true;
                result.compacted_turns = older_count;
                result.summary = "";
                result.compacted_conversation.assign(conversation.begin() + static_cast<std::ptrdiff_t>(older_count), conversation.end());
                return result;
            }

            // Strategy is "summarize" (or default)
            std::vector<std::pair<std::string, std::string>> older_turns(
                conversation.begin(), 
                conversation.begin() + static_cast<std::ptrdiff_t>(older_count)
            );
            std::vector<std::pair<std::string, std::string>> recent_turns(
                conversation.begin() + static_cast<std::ptrdiff_t>(older_count), 
                conversation.end()
            );

            std::string new_summary;
            if (custom_summarizer) {
                std::string formatted = format_conversation_for_summary(older_turns);
                if (!existing_summary.empty()) {
                    formatted = "Previous Context Summary:\n" + existing_summary + "\n\nNew Earlier Turns:\n" + formatted;
                }
                const std::string prompt = 
                    "Summarize the following conversation history between User and Assistant.\n"
                    "Extract and preserve:\n"
                    "- Core user goals, constraints, and preferences.\n"
                    "- Technical decisions made, file paths discussed, and established facts.\n"
                    "- Pending tasks or unanswered questions.\n"
                    "Keep the summary structured, factual, and under 300 words.";
                new_summary = custom_summarizer(prompt, formatted);
            }

            if (new_summary.empty()) {
                new_summary = generate_fallback_summary(older_turns);
                if (!existing_summary.empty() && new_summary.find(existing_summary) == std::string::npos) {
                    new_summary = existing_summary + "\n" + new_summary;
                }
            }

            result.was_compacted = true;
            result.compacted_turns = older_count;
            result.summary = new_summary;

            result.compacted_conversation.clear();
            result.compacted_conversation.reserve(1 + recent_turns.size());

            std::string summary_block = std::string(kSummaryHeaderPrefix) + "\n" + new_summary;
            result.compacted_conversation.emplace_back("system", std::move(summary_block));
            for (auto& turn : recent_turns) {
                result.compacted_conversation.push_back(std::move(turn));
            }

            return result;
        }
    };

} // namespace rouen::helpers
