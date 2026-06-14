#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "../../helpers/platform_utils.hpp"
#include "../../helpers/sqlite.hpp"

namespace rouen::models::notes {

struct note_record {
    int id{0};
    std::string title;
    std::string content;
    std::string tags;
    std::string created_at;
    std::string updated_at;
};

class notes_repository {
public:
    explicit notes_repository(const std::string& db_path = rouen::platform::get_user_data_path("notes.db").string())
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

    static std::vector<std::string> parse_wiki_links(std::string_view content) {
        std::vector<std::string> links;
        static const std::regex wiki_link_pattern(R"(\[\[([^\[\]]+)\]\])");

        std::string text(content);
        for (std::sregex_iterator it{text.begin(), text.end(), wiki_link_pattern}; it != std::sregex_iterator(); ++it) {
            std::string link = trim((*it)[1].str());
            if (!link.empty()) {
                links.push_back(link);
            }
        }

        std::sort(links.begin(), links.end());
        links.erase(std::unique(links.begin(), links.end()), links.end());
        return links;
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

        return slug.empty() ? "note" : slug;
    }

    static std::string stable_hash(std::string_view text) {
        // FNV-1a 64-bit hash used only for lightweight change detection in sync metadata.
        // This is intentionally non-cryptographic and optimized for speed.
        constexpr uint64_t offset_basis = 1469598103934665603ULL;
        constexpr uint64_t prime = 1099511628211ULL;

        uint64_t hash = offset_basis;
        for (unsigned char c : text) {
            hash ^= c;
            hash *= prime;
        }

        return std::format("{:016x}", hash);
    }

    std::optional<note_record> get_note_by_title(const std::string& title) {
        std::optional<note_record> result;
        db_.exec(
            "SELECT id, title, content, tags, created_at, updated_at FROM notes WHERE title = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = note_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    column_text(stmt, 4),
                    column_text(stmt, 5)
                };
            },
            title
        );

        return result;
    }

    std::optional<note_record> get_note_by_id(int id) {
        std::optional<note_record> result;
        db_.exec(
            "SELECT id, title, content, tags, created_at, updated_at FROM notes WHERE id = ? LIMIT 1",
            [&result](sqlite3_stmt* stmt) {
                result = note_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    column_text(stmt, 4),
                    column_text(stmt, 5)
                };
            },
            id
        );

        return result;
    }

    std::vector<note_record> list_notes(const std::string& search = {}, const std::string& tag_filter = {}) {
        std::vector<note_record> notes;

        if (!search.empty() || !tag_filter.empty()) {
            const std::string like_search = std::format("%{}%", search);
            const std::string like_tag = std::format("%{}%", tag_filter);
            db_.exec(
                "SELECT id, title, content, tags, created_at, updated_at "
                "FROM notes "
                "WHERE (? = '' OR title LIKE ? OR content LIKE ? OR tags LIKE ?) "
                "AND (? = '' OR tags LIKE ?) "
                "ORDER BY updated_at DESC, title ASC",
                [&notes](sqlite3_stmt* stmt) {
                    notes.emplace_back(note_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        column_text(stmt, 4),
                        column_text(stmt, 5)
                    });
                },
                search, like_search, like_search, like_search, tag_filter, like_tag
            );
        } else {
            db_.exec(
                "SELECT id, title, content, tags, created_at, updated_at FROM notes ORDER BY updated_at DESC, title ASC",
                [&notes](sqlite3_stmt* stmt) {
                    notes.emplace_back(note_record{
                        sqlite3_column_int(stmt, 0),
                        column_text(stmt, 1),
                        column_text(stmt, 2),
                        column_text(stmt, 3),
                        column_text(stmt, 4),
                        column_text(stmt, 5)
                    });
                }
            );
        }

        return notes;
    }

    int save_note(const std::string& title, const std::string& content, const std::string& tags) {
        const std::string normalized_title = trim(title);
        if (normalized_title.empty()) {
            throw std::runtime_error("Note title cannot be empty");
        }

        const std::string timestamp = now_timestamp();
        db_.exec(
            "INSERT INTO notes (title, content, tags, created_at, updated_at) VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(title) DO UPDATE SET content = excluded.content, tags = excluded.tags, updated_at = excluded.updated_at",
            {},
            normalized_title, content, tags, timestamp, timestamp
        );

        auto stored_note = get_note_by_title(normalized_title);
        if (!stored_note.has_value()) {
            throw std::runtime_error("Failed to load saved note");
        }

        rebuild_links();
        return stored_note->id;
    }

    bool delete_note(int id) {
        auto existing = get_note_by_id(id);
        if (!existing.has_value()) {
            return false;
        }

        db_.exec("DELETE FROM notes WHERE id = ?", {}, id);
        db_.exec("DELETE FROM note_file_hashes WHERE note_title = ?", {}, existing->title);
        rebuild_links();
        return true;
    }

    std::vector<note_record> backlinks_for_title(const std::string& title) {
        auto target = get_note_by_title(title);
        if (!target.has_value()) {
            return {};
        }

        return backlinks_for_id(target->id);
    }

    std::vector<note_record> backlinks_for_id(int note_id) {
        std::vector<note_record> backlinks;
        db_.exec(
            "SELECT n.id, n.title, n.content, n.tags, n.created_at, n.updated_at "
            "FROM note_links l "
            "JOIN notes n ON n.id = l.note_id "
            "WHERE l.linked_note_id = ? "
            "ORDER BY n.updated_at DESC",
            [&backlinks](sqlite3_stmt* stmt) {
                backlinks.emplace_back(note_record{
                    sqlite3_column_int(stmt, 0),
                    column_text(stmt, 1),
                    column_text(stmt, 2),
                    column_text(stmt, 3),
                    column_text(stmt, 4),
                    column_text(stmt, 5)
                });
            },
            note_id
        );

        return backlinks;
    }

    std::vector<std::pair<std::string, std::string>> relationships() {
        std::vector<std::pair<std::string, std::string>> edges;
        db_.exec(
            "SELECT n1.title, n2.title "
            "FROM note_links l "
            "JOIN notes n1 ON n1.id = l.note_id "
            "JOIN notes n2 ON n2.id = l.linked_note_id "
            "ORDER BY n1.title, n2.title",
            [&edges](sqlite3_stmt* stmt) {
                edges.emplace_back(column_text(stmt, 0), column_text(stmt, 1));
            }
        );

        return edges;
    }

    void rebuild_links() {
        db_.exec("DELETE FROM note_links");

        struct compact_note {
            int id{0};
            std::string title;
            std::string content;
        };

        std::vector<compact_note> notes;
        db_.exec(
            "SELECT id, title, content FROM notes",
            [&notes](sqlite3_stmt* stmt) {
                notes.push_back(compact_note{sqlite3_column_int(stmt, 0), column_text(stmt, 1), column_text(stmt, 2)});
            }
        );

        std::unordered_map<std::string, int> ids_by_title;
        ids_by_title.reserve(notes.size());
        for (const auto& note : notes) {
            ids_by_title[note.title] = note.id;
        }

        for (const auto& note : notes) {
            for (const auto& link_title : parse_wiki_links(note.content)) {
                auto linked = ids_by_title.find(link_title);
                if (linked == ids_by_title.end() || linked->second == note.id) {
                    continue;
                }

                db_.exec(
                    "INSERT OR IGNORE INTO note_links (note_id, linked_note_id) VALUES (?, ?)",
                    {},
                    note.id,
                    linked->second
                );
            }
        }
    }

    std::string get_sync_meta(const std::string& key, const std::string& default_value = {}) {
        std::string value = default_value;
        db_.exec(
            "SELECT value FROM sync_metadata WHERE key = ? LIMIT 1",
            [&value](sqlite3_stmt* stmt) {
                value = column_text(stmt, 0);
            },
            key
        );

        return value;
    }

    void set_sync_meta(const std::string& key, const std::string& value) {
        db_.exec(
            "INSERT INTO sync_metadata (key, value) VALUES (?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            {},
            key,
            value
        );
    }

    std::string get_note_hash(const std::string& note_title) {
        std::string hash;
        db_.exec(
            "SELECT last_hash FROM note_file_hashes WHERE note_title = ? LIMIT 1",
            [&hash](sqlite3_stmt* stmt) {
                hash = column_text(stmt, 0);
            },
            note_title
        );

        return hash;
    }

    void set_note_hash(const std::string& note_title, const std::string& file_path, const std::string& hash) {
        db_.exec(
            "INSERT INTO note_file_hashes (note_title, file_path, last_hash, last_synced_at) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(note_title) DO UPDATE SET file_path = excluded.file_path, last_hash = excluded.last_hash, last_synced_at = excluded.last_synced_at",
            {},
            note_title,
            file_path,
            hash,
            now_timestamp()
        );
    }

    void export_to_directory(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);

        auto notes = list_notes();
        for (const auto& note : notes) {
            const auto filename = std::format("{}.md", slugify(note.title));
            const auto path = directory / filename;

            std::ofstream output(path);
            if (!output) {
                throw std::runtime_error(std::format("Unable to write note file: {}", path.string()));
            }

            output << "# " << note.title << "\n\n";
            if (!note.tags.empty()) {
                output << "<!-- tags:" << note.tags << " -->\n\n";
            }
            output << note.content;

            const std::string serialized = std::format("{}\n{}", note.title, note.content);
            set_note_hash(note.title, path.string(), stable_hash(serialized));
        }
    }

    void import_from_directory(const std::filesystem::path& directory) {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
            return;
        }

        const std::string last_sync = get_sync_meta("notes_last_sync");

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") {
                continue;
            }

            std::ifstream input(entry.path());
            if (!input) {
                continue;
            }

            std::string file_content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            std::string note_title = trim(entry.path().stem().string());
            std::string tags;
            std::string markdown = file_content;

            {
                std::istringstream lines(file_content);
                std::string first_line;
                if (std::getline(lines, first_line) && first_line.rfind("# ", 0) == 0) {
                    note_title = trim(first_line.substr(2));
                    std::string remainder((std::istreambuf_iterator<char>(lines)), std::istreambuf_iterator<char>());
                    markdown = remainder;
                    if (!markdown.empty() && markdown.front() == '\n') {
                        markdown.erase(markdown.begin());
                    }
                }
            }

            if (markdown.rfind("<!-- tags:", 0) == 0) {
                const auto marker_end = markdown.find("-->");
                if (marker_end != std::string::npos) {
                    tags = trim(markdown.substr(10, marker_end - 10));
                    markdown.erase(0, marker_end + 3);
                    while (!markdown.empty() && (markdown.front() == '\n' || markdown.front() == '\r')) {
                        markdown.erase(markdown.begin());
                    }
                }
            }

            const std::string source_hash = stable_hash(std::format("{}\n{}", note_title, markdown));
            const std::string known_hash = get_note_hash(note_title);
            auto local_note = get_note_by_title(note_title);

            bool local_conflict = false;
            if (local_note.has_value() && local_note->content != markdown && !last_sync.empty()) {
                local_conflict = is_timestamp_newer(local_note->updated_at, last_sync) && source_hash != known_hash;
            }

            if (local_conflict) {
                const std::string conflict_title = std::format("{} (conflict {})", note_title, now_timestamp());
                save_note(conflict_title, markdown, tags);
            } else {
                save_note(note_title, markdown, tags);
            }

            set_note_hash(note_title, entry.path().string(), source_hash);
        }

        rebuild_links();
    }

private:
    static std::string column_text(sqlite3_stmt* stmt, int index) {
        const auto* text = sqlite3_column_text(stmt, index);
        return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
    }

    static std::optional<std::time_t> parse_timestamp(std::string_view timestamp) {
        std::tm tm{};
        std::istringstream in{std::string(timestamp)};
        in >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (in.fail()) {
            return std::nullopt;
        }
        return std::mktime(&tm);
    }

    static bool is_timestamp_newer(std::string_view lhs, std::string_view rhs) {
        const auto left = parse_timestamp(lhs);
        const auto right = parse_timestamp(rhs);
        if (!left.has_value() || !right.has_value()) {
            return lhs > rhs;
        }
        return left.value() > right.value();
    }

    void ensure_schema() {
        db_.ensure_table(
            "notes",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT NOT NULL UNIQUE, "
            "content TEXT NOT NULL DEFAULT '', "
            "tags TEXT NOT NULL DEFAULT '', "
            "created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL"
        );

        db_.ensure_table(
            "note_links",
            "note_id INTEGER NOT NULL, "
            "linked_note_id INTEGER NOT NULL, "
            "PRIMARY KEY(note_id, linked_note_id), "
            "FOREIGN KEY(note_id) REFERENCES notes(id) ON DELETE CASCADE, "
            "FOREIGN KEY(linked_note_id) REFERENCES notes(id) ON DELETE CASCADE"
        );

        db_.ensure_table(
            "sync_metadata",
            "key TEXT PRIMARY KEY, "
            "value TEXT NOT NULL"
        );

        db_.ensure_table(
            "note_file_hashes",
            "note_title TEXT PRIMARY KEY, "
            "file_path TEXT NOT NULL, "
            "last_hash TEXT NOT NULL, "
            "last_synced_at TEXT NOT NULL"
        );
    }

    hosting::db::sqlite db_;
};

} // namespace rouen::models::notes
