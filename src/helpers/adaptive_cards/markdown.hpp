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
            const auto close = input.find("**", i + 2);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::bold, std::string(input.substr(i + 2, close - i - 2)), {}});
                i = close + 2;
                continue;
            }
        }

        // Italic: *text*
        if (input[i] == '*') {
            const auto close = input.find('*', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::italic, std::string(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Italic: _text_
        if (input[i] == '_') {
            const auto close = input.find('_', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::italic, std::string(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Inline code: `text`
        if (input[i] == '`') {
            const auto close = input.find('`', i + 1);
            if (close != std::string_view::npos) {
                flush_normal();
                result.push_back({span_kind::code, std::string(input.substr(i + 1, close - i - 1)), {}});
                i = close + 1;
                continue;
            }
        }

        // Link: [label](url)
        if (input[i] == '[') {
            const auto label_end = input.find(']', i + 1);
            if (label_end != std::string_view::npos
                && label_end + 1 < input.size()
                && input[label_end + 1] == '(') {
                const auto url_end = input.find(')', label_end + 2);
                if (url_end != std::string_view::npos) {
                    flush_normal();
                    result.push_back({
                        span_kind::link,
                        std::string(input.substr(i + 1, label_end - i - 1)),
                        std::string(input.substr(label_end + 2, url_end - label_end - 2))
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
