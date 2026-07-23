#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/sqlite.hpp"

namespace rouen::models::adaptive_cards {

struct adaptive_card_record {
    int id{0};
    std::string name;         // Unique identifier / slug (e.g. "round1-card", "order-status")
    std::string title;        // Display title
    std::string card_json;     // Raw Adaptive Card JSON template
    std::string context_json;  // Context JSON for template binding
    std::string created_at{};
    std::string updated_at{};

    struct glaze {
        using T = adaptive_card_record;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "title", &T::title,
            "card_json", &T::card_json,
            "context_json", &T::context_json
        );
    };
};

class adaptive_cards_repository {
public:
    explicit adaptive_cards_repository(const std::string& db_path = rouen::platform::get_user_data_path("adaptive_cards.db").string())
        : db_(db_path) {
        ensure_schema();
    }

    static std::string now_timestamp() {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }

    static std::string trim(std::string_view value) {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
            ++start;
        }

        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
        }

        return std::string{value.substr(start, end - start)};
    }

    static std::string slugify(std::string_view text) {
        std::string slug;
        slug.reserve(text.size());

        bool last_dash = false;
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                last_dash = false;
            } else if (!last_dash) {
                slug.push_back('-');
                last_dash = true;
            }
        }

        while (!slug.empty() && slug.front() == '-') {
            slug.erase(slug.begin());
        }
        while (!slug.empty() && slug.back() == '-') {
            slug.pop_back();
        }

        return slug.empty() ? "card" : slug;
    }

    std::optional<adaptive_card_record> get_card_by_name(const std::string& name) {
        const std::string norm_name = trim(name);
        if (norm_name.empty()) return std::nullopt;

        std::optional<adaptive_card_record> result;
        db_.exec(
            "SELECT id, name, title, card_json, context_json, created_at, updated_at "
            "FROM adaptive_cards WHERE name = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = adaptive_card_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    column_text(stmt, 4),
                    column_text(stmt, 5),
                    column_text(stmt, 6)
                };
            },
            norm_name
        );

        return result;
    }

    std::optional<adaptive_card_record> get_card_by_id(int id) {
        std::optional<adaptive_card_record> result;
        db_.exec(
            "SELECT id, name, title, card_json, context_json, created_at, updated_at "
            "FROM adaptive_cards WHERE id = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = adaptive_card_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    column_text(stmt, 4),
                    column_text(stmt, 5),
                    column_text(stmt, 6)
                };
            },
            id
        );

        return result;
    }

    std::vector<adaptive_card_record> list_cards(const std::string& search = {}) {
        std::vector<adaptive_card_record> list;
        const std::string norm_search = trim(search);

        if (!norm_search.empty()) {
            const std::string like_search = std::format("%{}%", norm_search);
            db_.exec(
                "SELECT id, name, title, card_json, context_json, created_at, updated_at "
                "FROM adaptive_cards WHERE name LIKE ? OR title LIKE ? "
                "ORDER BY updated_at DESC, title ASC",
                [&list](sqlite3_stmt* stmt) {
                    list.emplace_back(adaptive_card_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        column_text(stmt, 4),
                        column_text(stmt, 5),
                        column_text(stmt, 6)
                    });
                },
                like_search, like_search
            );
        } else {
            db_.exec(
                "SELECT id, name, title, card_json, context_json, created_at, updated_at "
                "FROM adaptive_cards ORDER BY updated_at DESC, title ASC",
                [&list](sqlite3_stmt* stmt) {
                    list.emplace_back(adaptive_card_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        column_text(stmt, 4),
                        column_text(stmt, 5),
                        column_text(stmt, 6)
                    });
                }
            );
        }

        return list;
    }

    int save_card(adaptive_card_record card) {
        if (card.name.empty()) {
            card.name = slugify(card.title);
        }
        card.name = trim(card.name);
        if (card.name.empty()) {
            card.name = "adaptive-card";
        }
        if (card.title.empty()) {
            card.title = card.name;
        }
        if (card.context_json.empty()) {
            card.context_json = "{}";
        }

        const std::string timestamp = now_timestamp();
        db_.exec(
            "INSERT INTO adaptive_cards (name, title, card_json, context_json, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(name) DO UPDATE SET "
            "title = excluded.title, card_json = excluded.card_json, context_json = excluded.context_json, "
            "updated_at = excluded.updated_at",
            {},
            card.name, card.title, card.card_json, card.context_json, timestamp, timestamp
        );

        auto stored = get_card_by_name(card.name);
        if (!stored.has_value()) {
            throw std::runtime_error("Failed to load saved adaptive card");
        }

        return stored->id;
    }

    bool delete_card(const std::string& name) {
        auto existing = get_card_by_name(name);
        if (!existing.has_value()) {
            return false;
        }
        return delete_card_by_id(existing->id);
    }

    bool delete_card_by_id(int id) {
        db_.exec("DELETE FROM adaptive_cards WHERE id = ?", {}, id);
        return true;
    }

    void export_to_directory(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);

        auto list = list_cards();
        for (const auto& card : list) {
            const auto filename = std::format("{}.json", slugify(card.name));
            const auto path = directory / filename;

            std::ofstream output(path);
            if (!output) {
                throw std::runtime_error(std::format("Unable to write adaptive card file: {}", path.string()));
            }

            std::string json_str;
            auto write_res = glz::write_json(card, json_str);
            if (!write_res) {
                output << json_str;
            }
        }
    }

    void import_from_directory(const std::filesystem::path& directory) {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            std::ifstream input(entry.path());
            if (!input) {
                continue;
            }

            std::ostringstream oss;
            oss << input.rdbuf();
            std::string content = oss.str();

            adaptive_card_record imported{};
            auto parse_result = glz::read_json(imported, content);
            if (!parse_result && !imported.title.empty()) {
                if (imported.name.empty()) {
                    imported.name = trim(entry.path().stem().string());
                }
                save_card(imported);
            }
        }
    }

private:
    static std::string column_text(sqlite3_stmt* stmt, int index) {
        const auto* text = sqlite3_column_text(stmt, index);
        return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
    }

    void ensure_schema() {
        db_.ensure_table(
            "adaptive_cards",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL UNIQUE, "
            "title TEXT NOT NULL, "
            "card_json TEXT NOT NULL, "
            "context_json TEXT NOT NULL DEFAULT '{}', "
            "created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL"
        );

        bool empty = true;
        db_.exec("SELECT COUNT(*) FROM adaptive_cards", [&empty](sqlite3_stmt* stmt) {
            if (sqlite3_column_int(stmt, 0) > 0) {
                empty = false;
            }
        });

        if (empty) {
            seed_defaults();
        }
    }

    void seed_defaults() {
        save_card({
            .id = 0,
            .name = "welcome-card",
            .title = "Welcome to Rouen Adaptive Cards",
            .card_json = R"JSON({
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "TextBlock",
      "text": "Welcome to ${appName}!",
      "weight": "Bolder",
      "size": "ExtraLarge"
    },
    {
      "type": "TextBlock",
      "text": "Adaptive Cards present interactive, structured content natively inside Rouen.",
      "wrap": true
    },
    {
      "type": "FactSet",
      "facts": [
        { "title": "Version", "value": "${version}" },
        { "title": "Status", "value": "${status}" },
        { "title": "User", "value": "${username}" }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.OpenUrl",
      "title": "Learn More",
      "url": "https://adaptivecards.io"
    }
  ]
})JSON",
            .context_json = R"JSON({
  "appName": "Rouen",
  "version": "1.0.0",
  "status": "Active & Persisted",
  "username": "ignacionr"
})JSON",
            .created_at = "",
            .updated_at = ""
        });

        save_card({
            .id = 0,
            .name = "flight-itinerary",
            .title = "Flight Itinerary & Boarding Pass",
            .card_json = R"JSON({
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "TextBlock",
      "text": "✈ Flight ${flightNum} - ${origin} to ${destination}",
      "weight": "Bolder",
      "size": "Large"
    },
    {
      "type": "ColumnSet",
      "columns": [
        {
          "type": "Column",
          "width": "stretch",
          "items": [
            { "type": "TextBlock", "text": "Departure", "isSubtle": true },
            { "type": "TextBlock", "text": "${departureTime}", "weight": "Bolder" },
            { "type": "TextBlock", "text": "Gate ${gate}" }
          ]
        },
        {
          "type": "Column",
          "width": "stretch",
          "items": [
            { "type": "TextBlock", "text": "Seat", "isSubtle": true },
            { "type": "TextBlock", "text": "${seat}", "weight": "Bolder" },
            { "type": "TextBlock", "text": "Class: ${classType}" }
          ]
        }
      ]
    }
  ]
})JSON",
            .context_json = R"JSON({
  "flightNum": "RN-402",
  "origin": "SCL",
  "destination": "EZE",
  "departureTime": "08:45 AM",
  "gate": "B12",
  "seat": "4A",
  "classType": "Business"
})JSON",
            .created_at = "",
            .updated_at = ""
        });
    }

    hosting::db::sqlite db_;
};

} // namespace rouen::models::adaptive_cards
