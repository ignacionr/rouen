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

namespace rouen::models::series {

struct data_point {
    std::string label;
    float value{0.0f};

    struct glaze {
        using T = data_point;
        static constexpr auto value = glz::object(
            "label", &T::label,
            "value", &T::value
        );
    };
};

struct series_record {
    int id{0};
    std::string name;        // Unique identifier / slug (e.g. "sales", "temps", "cpu")
    std::string title;       // Display title
    std::string unit;        // Unit label (e.g. "$", "C", "%")
    std::vector<data_point> points;
    bool is_bar_chart{true};
    int color_index{0};
    std::string created_at{};
    std::string updated_at{};

    struct glaze {
        using T = series_record;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "title", &T::title,
            "unit", &T::unit,
            "points", &T::points,
            "is_bar_chart", &T::is_bar_chart,
            "color_index", &T::color_index
        );
    };
};

class series_repository {
public:
    explicit series_repository(const std::string& db_path = rouen::platform::get_user_data_path("series.db").string())
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

        return slug.empty() ? "series" : slug;
    }

    std::optional<series_record> get_series_by_name(const std::string& name) {
        const std::string norm_name = trim(name);
        if (norm_name.empty()) return std::nullopt;

        std::optional<series_record> result;
        db_.exec(
            "SELECT id, name, title, unit, is_bar_chart, color_index, created_at, updated_at "
            "FROM series WHERE name = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = series_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    {},
                    sqlite3_column_int(stmt, 4) != 0,
                    sqlite3_column_int(stmt, 5),
                    column_text(stmt, 6),
                    column_text(stmt, 7)
                };
            },
            norm_name
        );

        if (result.has_value()) {
            result->points = load_points_for_series(result->id);
        }

        return result;
    }

    std::optional<series_record> get_series_by_id(int id) {
        std::optional<series_record> result;
        db_.exec(
            "SELECT id, name, title, unit, is_bar_chart, color_index, created_at, updated_at "
            "FROM series WHERE id = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = series_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    {},
                    sqlite3_column_int(stmt, 4) != 0,
                    sqlite3_column_int(stmt, 5),
                    column_text(stmt, 6),
                    column_text(stmt, 7)
                };
            },
            id
        );

        if (result.has_value()) {
            result->points = load_points_for_series(result->id);
        }

        return result;
    }

    std::vector<series_record> list_series(const std::string& search = {}) {
        std::vector<series_record> list;
        const std::string norm_search = trim(search);

        if (!norm_search.empty()) {
            const std::string like_search = std::format("%{}%", norm_search);
            db_.exec(
                "SELECT id, name, title, unit, is_bar_chart, color_index, created_at, updated_at "
                "FROM series WHERE name LIKE ? OR title LIKE ? "
                "ORDER BY updated_at DESC, title ASC",
                [&list](sqlite3_stmt* stmt) {
                    list.emplace_back(series_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        {},
                        sqlite3_column_int(stmt, 4) != 0,
                        sqlite3_column_int(stmt, 5),
                        column_text(stmt, 6),
                        column_text(stmt, 7)
                    });
                },
                like_search, like_search
            );
        } else {
            db_.exec(
                "SELECT id, name, title, unit, is_bar_chart, color_index, created_at, updated_at "
                "FROM series ORDER BY updated_at DESC, title ASC",
                [&list](sqlite3_stmt* stmt) {
                    list.emplace_back(series_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        {},
                        sqlite3_column_int(stmt, 4) != 0,
                        sqlite3_column_int(stmt, 5),
                        column_text(stmt, 6),
                        column_text(stmt, 7)
                    });
                }
            );
        }

        for (auto& s : list) {
            s.points = load_points_for_series(s.id);
        }

        return list;
    }

    int save_series(series_record series) {
        if (series.name.empty()) {
            series.name = slugify(series.title);
        }
        series.name = trim(series.name);
        if (series.name.empty()) {
            series.name = "series";
        }
        if (series.title.empty()) {
            series.title = series.name;
        }

        const std::string timestamp = now_timestamp();
        db_.exec(
            "INSERT INTO series (name, title, unit, is_bar_chart, color_index, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(name) DO UPDATE SET "
            "title = excluded.title, unit = excluded.unit, is_bar_chart = excluded.is_bar_chart, "
            "color_index = excluded.color_index, updated_at = excluded.updated_at",
            {},
            series.name, series.title, series.unit, series.is_bar_chart ? 1 : 0, series.color_index, timestamp, timestamp
        );

        auto stored = get_series_by_name(series.name);
        if (!stored.has_value()) {
            throw std::runtime_error("Failed to load saved series");
        }

        int series_id = stored->id;

        // Replace points
        db_.exec("DELETE FROM series_points WHERE series_id = ?", {}, series_id);

        for (size_t i = 0; i < series.points.size(); ++i) {
            db_.exec(
                "INSERT INTO series_points (series_id, point_order, label, value) VALUES (?, ?, ?, ?)",
                {},
                series_id, static_cast<int>(i), series.points[i].label, static_cast<double>(series.points[i].value)
            );
        }

        return series_id;
    }

    bool delete_series(const std::string& name) {
        auto existing = get_series_by_name(name);
        if (!existing.has_value()) {
            return false;
        }

        return delete_series_by_id(existing->id);
    }

    bool delete_series_by_id(int id) {
        db_.exec("DELETE FROM series_points WHERE series_id = ?", {}, id);
        db_.exec("DELETE FROM series WHERE id = ?", {}, id);
        return true;
    }

    void export_to_directory(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);

        auto list = list_series();
        for (const auto& series : list) {
            const auto filename = std::format("{}.json", slugify(series.name));
            const auto path = directory / filename;

            std::ofstream output(path);
            if (!output) {
                throw std::runtime_error(std::format("Unable to write series file: {}", path.string()));
            }

            std::string json_str;
            auto write_res = glz::write_json(series, json_str);
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

            series_record imported{};
            auto parse_result = glz::read_json(imported, content);
            if (!parse_result && !imported.title.empty()) {
                if (imported.name.empty()) {
                    imported.name = trim(entry.path().stem().string());
                }
                save_series(imported);
            }
        }
    }

private:
    static std::string column_text(sqlite3_stmt* stmt, int index) {
        const auto* text = sqlite3_column_text(stmt, index);
        return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
    }

    std::vector<data_point> load_points_for_series(int series_id) {
        std::vector<data_point> points;
        db_.exec(
            "SELECT label, value FROM series_points WHERE series_id = ? ORDER BY point_order ASC",
            [&points](sqlite3_stmt* stmt) {
                points.push_back(data_point{
                    column_text(stmt, 0),
                    static_cast<float>(sqlite3_column_double(stmt, 1))
                });
            },
            series_id
        );
        return points;
    }

    void ensure_schema() {
        db_.ensure_table(
            "series",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL UNIQUE, "
            "title TEXT NOT NULL, "
            "unit TEXT NOT NULL DEFAULT '', "
            "is_bar_chart INTEGER NOT NULL DEFAULT 1, "
            "color_index INTEGER NOT NULL DEFAULT 0, "
            "created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL"
        );

        db_.ensure_table(
            "series_points",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "series_id INTEGER NOT NULL, "
            "point_order INTEGER NOT NULL, "
            "label TEXT NOT NULL, "
            "value REAL NOT NULL, "
            "FOREIGN KEY(series_id) REFERENCES series(id) ON DELETE CASCADE"
        );

        // Check if database is empty; if so, seed defaults!
        bool empty = true;
        db_.exec("SELECT COUNT(*) FROM series", [&empty](sqlite3_stmt* stmt) {
            if (sqlite3_column_int(stmt, 0) > 0) {
                empty = false;
            }
        });

        if (empty) {
            seed_defaults();
        }
    }

    void seed_defaults() {
        save_series({
            .id = 0,
            .name = "sales",
            .title = "Monthly Sales Revenue",
            .unit = "$",
            .points = {
                {"Jan", 12000.0f},
                {"Feb", 15000.0f},
                {"Mar", 14000.0f},
                {"Apr", 18000.0f},
                {"May", 22000.0f},
                {"Jun", 25000.0f},
                {"Jul", 23000.0f},
                {"Aug", 21000.0f},
                {"Sep", 26000.0f},
                {"Oct", 30000.0f},
                {"Nov", 35000.0f},
                {"Dec", 42000.0f}
            },
            .is_bar_chart = true,
            .color_index = 0,
            .created_at = "",
            .updated_at = ""
        });

        save_series({
            .id = 0,
            .name = "temps",
            .title = "Weekly Temperature Forecast",
            .unit = "C",
            .points = {
                {"Mon", 18.5f},
                {"Tue", 19.0f},
                {"Wed", 21.0f},
                {"Thu", 20.5f},
                {"Fri", 23.0f},
                {"Sat", 25.5f},
                {"Sun", 24.0f}
            },
            .is_bar_chart = false,
            .color_index = 8,
            .created_at = "",
            .updated_at = ""
        });

        save_series({
            .id = 0,
            .name = "cpu",
            .title = "System CPU Load",
            .unit = "%",
            .points = {
                {"10s ago", 12.0f},
                {"9s ago", 18.5f},
                {"8s ago", 25.0f},
                {"7s ago", 15.0f},
                {"6s ago", 30.0f},
                {"5s ago", 45.5f},
                {"4s ago", 60.0f},
                {"3s ago", 35.0f},
                {"2s ago", 20.0f},
                {"1s ago", 10.0f},
                {"now", 5.0f}
            },
            .is_bar_chart = false,
            .color_index = 6,
            .created_at = "",
            .updated_at = ""
        });
    }

    hosting::db::sqlite db_;
};

} // namespace rouen::models::series
