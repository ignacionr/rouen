#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <regex>
#include <algorithm>
#include <format>
#include <sstream>
#include <memory>
#include <unordered_set>
#include <chrono>

#include "glaze_include.hpp"
#include "llm_config.hpp"
#include "fetch.hpp"
#include "process_helper.hpp"
#include "config_service.hpp"

namespace rouen::helpers {

struct ParsedConventionalCommit {
    std::string type;         // "feat", "fix", "docs", "style", "refactor", "perf", "test", "build", "ci", "chore", "revert"
    std::string scope;        // e.g. "cmake", "editor", "mesh"
    std::string description;  // imperative summary
    std::string body;         // optional detailed body
    std::vector<std::string> footers;
    bool is_breaking{false};  // true if ! or BREAKING CHANGE footer
    bool is_valid{false};     // true if conforms to Conventional Commits 1.0.0
    std::string raw_message;  // complete raw text

    struct glaze {
        using T = ParsedConventionalCommit;
        static constexpr auto value = glz::object(
            "type", &T::type,
            "scope", &T::scope,
            "description", &T::description,
            "body", &T::body,
            "footers", &T::footers,
            "is_breaking", &T::is_breaking,
            "is_valid", &T::is_valid,
            "raw_message", &T::raw_message
        );
    };
};

class ConventionalCommitGenerator {
public:
    /**
     * Clean raw LLM response by stripping markdown code fences, wrapping quotes, and extraneous whitespace.
     */
    static std::string clean_commit_message(std::string raw) {
        // Trim leading whitespace
        while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' || raw.front() == '\n')) {
            raw.erase(raw.begin());
        }
        // Trim trailing whitespace
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
            raw.pop_back();
        }

        // Strip markdown code fences if wrapped in ``` ... ```
        if (raw.starts_with("```")) {
            auto first_nl = raw.find('\n');
            if (first_nl != std::string::npos) {
                raw = raw.substr(first_nl + 1);
            }
            if (raw.ends_with("```")) {
                raw.resize(raw.size() - 3);
            }
            while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' || raw.front() == '\n')) {
                raw.erase(raw.begin());
            }
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
                raw.pop_back();
            }
        }

        // Strip wrapping quotes if entire message is in "..." or '...'
        if (raw.size() >= 2 && ((raw.front() == '"' && raw.back() == '"') || (raw.front() == '\'' && raw.back() == '\''))) {
            raw = raw.substr(1, raw.size() - 2);
        }

        // Final whitespace trim
        while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' || raw.front() == '\n')) {
            raw.erase(raw.begin());
        }
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
            raw.pop_back();
        }

        return raw;
    }

    /**
     * Parse structured conventional commit components according to Conventional Commits 1.0.0.
     */
    static ParsedConventionalCommit parse_conventional_commit(const std::string& raw) {
        ParsedConventionalCommit result;
        result.raw_message = clean_commit_message(raw);
        if (result.raw_message.empty()) {
            return result;
        }

        std::istringstream stream(result.raw_message);
        std::string header_line;
        if (!std::getline(stream, header_line)) {
            return result;
        }
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }

        // Regex for header: <type>(scope)[!]: <description>
        static const std::regex header_regex(
            R"(^([a-z]+)(?:\(([a-zA-Z0-9_\-\./]+)\))?(!)?:\s*(.+)$)",
            std::regex::optimize
        );

        std::smatch match;
        if (std::regex_match(header_line, match, header_regex)) {
            result.type = match[1].str();
            if (match[2].matched) {
                result.scope = match[2].str();
            }
            if (match[3].matched && match[3].str() == "!") {
                result.is_breaking = true;
            }
            result.description = match[4].str();

            static const std::unordered_set<std::string> valid_types = {
                "feat", "fix", "docs", "style", "refactor", "perf", "test", "build", "ci", "chore", "revert"
            };
            if (valid_types.contains(result.type)) {
                result.is_valid = true;
            }
        }

        // Parse body and footers
        std::string line;
        bool is_first_body_line = true;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.starts_with("BREAKING CHANGE:") || line.starts_with("BREAKING-CHANGE:")) {
                result.is_breaking = true;
                result.footers.push_back(line);
            } else if (line.find(":") != std::string::npos && !result.body.empty() && line.find(" ") < line.find(":")) {
                result.footers.push_back(line);
            } else {
                if (!is_first_body_line || !line.empty()) {
                    if (!result.body.empty()) {
                        result.body += "\n";
                    }
                    result.body += line;
                    is_first_body_line = false;
                }
            }
        }

        return result;
    }

    /**
     * Standard system instructions enforcing Conventional Commits 1.0.0.
     */
    static std::string build_system_instructions() {
        return 
            "You are an expert software engineer generating a git commit message adhering strictly to the Conventional Commits 1.0.0 specification.\n\n"
            "Format Specification:\n"
            "<type>[optional scope]: <description>\n\n"
            "[optional body]\n\n"
            "[optional footer(s)]\n\n"
            "Allowed types:\n"
            "- feat: A new feature\n"
            "- fix: A bug fix\n"
            "- docs: Documentation changes only\n"
            "- style: Formatting, whitespace, missing semi-colons, etc. (no logic change)\n"
            "- refactor: Refactoring code without adding features or fixing bugs\n"
            "- perf: Performance improvements\n"
            "- test: Adding or fixing tests\n"
            "- build: Build system or dependency changes (e.g. CMake, Nix, Ninja)\n"
            "- ci: CI/CD configuration files and scripts (e.g. GitHub Actions)\n"
            "- chore: Routine maintenance, repository chores\n"
            "- revert: Reverting a previous commit\n\n"
            "Strict Rules:\n"
            "1. Imperative mood for the description (\"add\", \"fix\", \"implement\", NOT \"added\", \"fixes\").\n"
            "2. Description MUST begin with a lowercase letter and must NOT end with a period.\n"
            "3. Header line must be <= 72 characters.\n"
            "4. If body is provided, separate it from header with a blank line. Body explains what and why, not how.\n"
            "5. Breaking changes MUST have an exclamation mark after type/scope (e.g. feat!:) or a 'BREAKING CHANGE: <desc>' footer.\n"
            "6. Output ONLY the raw commit message text. Do NOT wrap in quotes or markdown code blocks (no ```).\n";
    }

    /**
     * Build the user prompt containing git status and diff context.
     */
    static std::string build_prompt(
        const std::string& diff_context,
        const std::string& status_context = "",
        const std::string& repo_name = ""
    ) {
        std::string prompt;
        if (!repo_name.empty()) {
            prompt += std::format("Repository / Project: {}\n\n", repo_name);
        }
        if (!status_context.empty()) {
            prompt += std::format("Git Status:\n{}\n\n", status_context);
        }

        constexpr size_t max_diff_chars = 14000;
        std::string truncated_diff = diff_context;
        if (truncated_diff.size() > max_diff_chars) {
            truncated_diff.resize(max_diff_chars);
            truncated_diff += "\n\n... [diff truncated for length] ...\n";
        }

        prompt += std::format("Git Diff Context:\n{}\n\nGenerate the conventional commit message:", 
            truncated_diff.empty() ? "(empty diff / untracked files)" : truncated_diff);
        return prompt;
    }

    /**
     * Generate Conventional Commit message synchronously using the configured LLM backend.
     */
    static std::string generate_commit_message(
        const std::string& diff_context,
        const std::string& status_context = "",
        const std::string& repo_name = ""
    ) {
        if (!LLMConfig::is_configured()) {
            return "";
        }
        auto llm_instance = LLMConfig::create_llm_instance();
        if (!llm_instance) {
            return "";
        }

        auto settings = LLMConfig::get_current_config();
        std::string model_name = settings.model_name;

        // Query Gemini API models dynamically to ensure using an active model
        if (settings.provider == LLMConfig::Provider::GEMINI) {
            try {
                http::fetch fetcher(10);
                std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", settings.api_key);
                std::string resp_json = fetcher(url);
                if (resp_json.find("gemini-3.8-flash") != std::string::npos) {
                    model_name = "gemini-3.8-flash";
                } else if (resp_json.find("gemini-3.1-flash-lite") != std::string::npos) {
                    model_name = "gemini-3.1-flash-lite";
                } else if (resp_json.find("gemini-flash-lite-latest") != std::string::npos) {
                    model_name = "gemini-flash-lite-latest";
                } else if (resp_json.find("gemini-2.5-flash-lite") != std::string::npos) {
                    model_name = "gemini-2.5-flash-lite";
                }
            } catch (...) {}
        }

        llm_instance->add_instructions(build_system_instructions());

        auto fetcher = std::make_shared<http::fetch>(30);
        std::string prompt = build_prompt(diff_context, status_context, repo_name);

        auto response = llm_instance->sendMessage(
            prompt,
            [fetcher](const std::string& url, const std::string& data, auto header_client) {
                return fetcher->post(url, data, header_client);
            },
            "user",
            model_name,
            "",
            0.2f
        );

        if (response.choices.empty() || response.choices[0].message.content.empty()) {
            return "";
        }

        return clean_commit_message(response.choices[0].message.content);
    }
};

} // namespace rouen::helpers
