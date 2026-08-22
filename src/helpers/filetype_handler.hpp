#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glaze_include.hpp"

namespace rouen::helpers {

    // A single rule tying a filename/extension match list to a card URI format.
    // `match` is a comma-separated list of patterns: a pattern starting with '.'
    // is compared against the file extension, anything else against the exact
    // filename (both comparisons are case-sensitive, mirroring the historical
    // hardcoded checks this replaces).
    // `uri_format` must contain exactly one "{}" placeholder, replaced with the
    // full path when the rule matches, e.g. "pdf:{}".
    // `requires_ctrl` reproduces the CMakeLists.txt special case, where the card
    // is only opened when the user Ctrl-clicks; without it a plain click matches.
    struct FiletypeRule {
        std::string match;
        std::string uri_format;
        bool requires_ctrl{false};

        struct glaze {
            using T = FiletypeRule;
            static constexpr auto value = glz::object(
                "match", &T::match,
                "uri_format", &T::uri_format,
                "requires_ctrl", &T::requires_ctrl
            );
        };
    };

    struct FiletypeHandlerSaveModel {
        std::vector<FiletypeRule> rules;

        struct glaze {
            using T = FiletypeHandlerSaveModel;
            static constexpr auto value = glz::object(
                "rules", &T::rules
            );
        };
    };

    // Resolves files to the card URI used to open them, driven by a
    // user-editable, persisted list of rules. Rules are tried in order;
    // the first match wins. Callers should fall back to their own default
    // behavior (e.g. opening a plain text editor) when resolve() returns nullopt.
    class FiletypeHandler {
    public:
        static FiletypeHandler& instance();

        [[nodiscard]] std::optional<std::string> resolve(const std::filesystem::path& path, bool ctrl_held) const;

        [[nodiscard]] const std::vector<FiletypeRule>& get_rules() const { return rules_; }

        void add_rule(const FiletypeRule& rule);
        void update_rule(size_t index, const FiletypeRule& rule);
        void delete_rule(size_t index);
        void move_rule(size_t index, int direction); // direction: -1 up, +1 down
        void reset_to_defaults();

        // A valid template contains exactly one "{}" placeholder.
        [[nodiscard]] static bool validate_uri_format(const std::string& uri_format);

    private:
        FiletypeHandler();
        ~FiletypeHandler() = default;

        void setup_default_rules();
        void load_rules();
        void save_rules() const;

        static bool matches_pattern(const std::string& pattern, const std::string& filename, const std::string& extension);
        static std::vector<std::string> split_patterns(const std::string& match);
        static std::string format_uri(const std::string& uri_format, const std::string& path_str);

        std::vector<FiletypeRule> rules_;
    };

} // namespace rouen::helpers
