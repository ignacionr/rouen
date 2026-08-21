#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rouen::helpers::adaptive_cards {

enum class span_kind { normal, bold, italic, code, link };

struct text_span {
    span_kind kind{span_kind::normal};
    std::string text;
    std::string url; // only populated for span_kind::link
};

// Removes backslash escape characters, turning e.g. "\*" into "*" and "\_" into "_".
[[nodiscard]] inline std::string unescape_markdown(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            result += input[i + 1];
            ++i;
        } else {
            result += input[i];
        }
    }
    return result;
}

// Finds the next unescaped occurrence of target starting from start_pos.
[[nodiscard]] inline std::size_t find_unescaped(std::string_view input, std::string_view target, std::size_t start_pos = 0) {
    if (target.empty() || start_pos >= input.size()) {
        return std::string_view::npos;
    }
    std::size_t i = start_pos;
    while (i < input.size()) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            i += 2;
            continue;
        }
        if (input.substr(i).starts_with(target)) {
            return i;
        }
        ++i;
    }
    return std::string_view::npos;
}

[[nodiscard]] inline std::size_t find_unescaped(std::string_view input, char target, std::size_t start_pos = 0) {
    if (start_pos >= input.size()) {
        return std::string_view::npos;
    }
    std::size_t i = start_pos;
    while (i < input.size()) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            i += 2;
            continue;
        }
        if (input[i] == target) {
            return i;
        }
        ++i;
    }
    return std::string_view::npos;
}

// Parses basic inline Markdown into a flat list of styled spans.
//
// Supported markers:
//   **text**       -> bold
//   *text*         -> italic
//   _text_         -> italic (note: may conflict with underscores in identifiers)
//   `text`         -> inline code
//   [label](url)   -> link
//   \char          -> escaped literal character (e.g. \* \_ \` \[ \\)
//
// Markers must be terminated; unterminated markers are treated as literal text.
// Bold (**) is checked before italic (*) to avoid greedy mis-matches.
[[nodiscard]] inline std::vector<text_span> parse_inline_markdown(std::string_view input) {
    std::vector<text_span> result;
    std::string current_normal;

    auto flush_normal = [&]() {
        if (!current_normal.empty()) {
            result.push_back({span_kind::normal, std::move(current_normal), {}});
            current_normal.clear();
        }
    };

    std::size_t i = 0;
    while (i < input.size()) {
        // Escaped character: consume next char literally.
        if (input[i] == '\\' && i + 1 < input.size()) {
            current_normal += input[i + 1];
            i += 2;
            continue;
        }

        // Bold: **text** — must be checked before single *.
        if (input.substr(i).starts_with("**")) {
            const auto close = find_unescaped(input, "**", i + 2);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::bold, unescape_markdown(input.substr(i + 2, close - i - 2)), {}});
                i = close + 2;
                continue;
            }
        }

        // Italic: *text*
        if (input[i] == '*') {
            const auto close = find_unescaped(input, '*', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::italic, unescape_markdown(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Italic: _text_
        if (input[i] == '_') {
            const auto close = find_unescaped(input, '_', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::italic, unescape_markdown(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Inline code: `text`
        if (input[i] == '`') {
            const auto close = find_unescaped(input, '`', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::code, std::string(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Link: [label](url)
        if (input[i] == '[') {
            const auto label_end = find_unescaped(input, ']', i + 1);
            if (label_end != std::string_view::npos
                && label_end + 1 < input.size()
                && input[label_end + 1] == '(') {
                const auto url_end = find_unescaped(input, ')', label_end + 2);
                if (url_end != std::string_view::npos) {
                    flush_normal();
                    result.push_back({
                        span_kind::link,
                        unescape_markdown(input.substr(i + 1, label_end - i - 1)),
                        unescape_markdown(input.substr(label_end + 2, url_end - label_end - 2))
                    });
                    i = url_end + 1;
                    continue;
                }
            }
        }

        current_normal += input[i++];
    }

    flush_normal();
    return result;
}

// Returns the plain text content of a Markdown string with all formatting markers stripped.
// Useful for semantic text extraction (e.g. search, accessibility, collect_lines).
[[nodiscard]] inline std::string strip_markdown(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const auto& span : parse_inline_markdown(input)) {
        result += span.text;
    }
    return result;
}

} // namespace rouen::helpers::adaptive_cards
