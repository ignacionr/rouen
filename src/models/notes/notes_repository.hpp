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
#include <unordered_set>
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
        const std::regex wiki_link_pattern(R"(\[\[([^\[\]]+)\]\])");

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

    static std::string normalize_newlines(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    continue;
                }
                result.push_back('\n');
            } else {
                result.push_back(text[i]);
            }
        }
        return result;
    }

    static std::string normalize_content(std::string_view text) {
        std::string result = normalize_newlines(text);
        size_t start = 0;
        while (start < result.size() && (result[start] == '\n' || result[start] == '\r')) {
            ++start;
        }
        if (start > 0) {
            result.erase(0, start);
        }
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())) != 0) {
            result.pop_back();
        }
        return result;
    }

    static void extract_embedded_tags(std::string& content, std::string& tags) {
        content = normalize_content(content);
        while (content.rfind("<!-- tags:", 0) == 0) {
            const auto marker_end = content.find("-->");
            if (marker_end == std::string::npos) {
                break;
            }
            const std::string extracted = trim(content.substr(10, marker_end - 10));
            if (tags.empty() && !extracted.empty()) {
                tags = extracted;
            }
            content.erase(0, marker_end + 3);
            content = normalize_content(content);
        }
    }

    static std::string compute_sync_payload(std::string_view title, std::string_view tags, std::string_view content) {
        if (tags.empty()) {
            return std::format("{}\n{}", title, content);
        }
        return std::format("{}\n[tags:{}]\n{}", title, tags, content);
    }

    static bool is_conflict_marker(std::string_view title, const std::filesystem::path& path) {
        if (title.find(" (conflict ") != std::string_view::npos || title.find("(conflict") != std::string_view::npos) {
            return true;
        }
        const auto stem = path.stem().string();
        if (stem.find("-conflict-") != std::string_view::npos || stem.find("_conflict_") != std::string_view::npos) {
            return true;
        }
        return false;
    }

    static std::string stable_hash(std::string_view text) {
        // FNV-1a 64-bit hash used only for lightweight change detection in sync metadata.
        // This is intentionally non-cryptographic and optimized for speed.
        constexpr uint64_t offset_basis = 1469598103934665603ULL;
        constexpr uint64_t prime = 1099511628211ULL;

        uint64_t hash = offset_basis;
        for (char c : text) {
            hash ^= static_cast<unsigned char>(c);
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

    int save_note(const std::string& title, const std::string& content, const std::string& tags, bool auto_rebuild_links = true) {
        const std::string normalized_title = trim(title);
        if (normalized_title.empty()) {
            throw std::runtime_error("Note title cannot be empty");
        }

        std::string normalized_tags = trim(tags);
        std::string normalized_content = normalize_content(content);
        extract_embedded_tags(normalized_content, normalized_tags);

        auto existing = get_note_by_title(normalized_title);
        if (existing.has_value()) {
            if (normalize_content(existing->content) == normalized_content && trim(existing->tags) == normalized_tags) {
                return existing->id;
            }
            const std::string timestamp = now_timestamp();
            db_.exec(
                "UPDATE notes SET content = ?, tags = ?, updated_at = ? WHERE id = ?",
                {},
                normalized_content, normalized_tags, timestamp, existing->id
            );
            if (auto_rebuild_links) {
                rebuild_links();
            }
            return existing->id;
        }

        const std::string timestamp = now_timestamp();
        db_.exec(
            "INSERT INTO notes (title, content, tags, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
            {},
            normalized_title, normalized_content, normalized_tags, timestamp, timestamp
        );

        auto stored_note = get_note_by_title(normalized_title);
        if (!stored_note.has_value()) {
            throw std::runtime_error("Failed to load saved note");
        }

        if (auto_rebuild_links) {
            rebuild_links();
        }
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

    int purge_conflict_notes() {
        int removed = 0;
        db_.exec(
            "SELECT COUNT(*) FROM notes WHERE title LIKE '% (conflict %' OR title LIKE '%(conflict%'",
            [&removed](sqlite3_stmt* stmt) {
                removed = sqlite3_column_int(stmt, 0);
            }
        );
        if (removed > 0) {
            db_.exec("DELETE FROM notes WHERE title LIKE '% (conflict %' OR title LIKE '%(conflict%'");
            db_.exec("DELETE FROM note_file_hashes WHERE note_title LIKE '% (conflict %' OR note_title LIKE '%(conflict%'");
            rebuild_links();
        }
        return removed;
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
        std::unordered_set<std::string> expected_filenames;

        for (const auto& note : notes) {
            if (is_conflict_marker(note.title, {})) {
                continue;
            }

            const auto filename = std::format("{}.md", slugify(note.title));
            expected_filenames.insert(filename);
            const auto path = directory / filename;

            std::string norm_tags = trim(note.tags);
            std::string norm_content = normalize_content(note.content);
            extract_embedded_tags(norm_content, norm_tags);

            std::ostringstream rendered;
            rendered << "# " << note.title << "\n\n";
            if (!norm_tags.empty()) {
                rendered << "<!-- tags:" << norm_tags << " -->\n\n";
            }
            rendered << norm_content;
            const std::string rendered_str = rendered.str();

            bool needs_write = true;
            if (std::filesystem::exists(path)) {
                std::ifstream existing_in(path, std::ios::binary);
                if (existing_in) {
                    std::ostringstream existing_oss;
                    existing_oss << existing_in.rdbuf();
                    if (existing_oss.str() == rendered_str) {
                        needs_write = false;
                    }
                }
            }

            if (needs_write) {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                if (!output) {
                    throw std::runtime_error(std::format("Unable to write note file: {}", path.string()));
                }
                output.write(rendered_str.data(), static_cast<std::streamsize>(rendered_str.size()));
            }

            const std::string serialized = compute_sync_payload(note.title, norm_tags, norm_content);
            const std::string new_hash = stable_hash(serialized);
            if (get_note_hash(note.title) != new_hash) {
                set_note_hash(note.title, path.string(), new_hash);
            }
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") {
                continue;
            }
            const std::string fname = entry.path().filename().string();
            if (!expected_filenames.contains(fname)) {
                std::filesystem::remove(entry.path(), ec);
            }
        }

        set_sync_meta("notes_last_sync", now_timestamp());
    }

    void import_from_directory(const std::filesystem::path& directory) {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
            return;
        }

        const std::string last_sync = get_sync_meta("notes_last_sync");
        bool any_changed = false;

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") {
                continue;
            }

            std::ifstream input(entry.path(), std::ios::binary);
            if (!input) {
                continue;
            }

            std::ostringstream oss;
            oss << input.rdbuf();
            std::string file_content = normalize_newlines(oss.str());
            std::string note_title = trim(entry.path().stem().string());
            std::string tags;
            std::string markdown = file_content;

            if (markdown.rfind("# ", 0) == 0) {
                const auto first_nl = markdown.find('\n');
                if (first_nl != std::string::npos) {
                    note_title = trim(markdown.substr(2, first_nl - 2));
                    markdown = markdown.substr(first_nl + 1);
                } else {
                    note_title = trim(markdown.substr(2));
                    markdown.clear();
                }
            }

            extract_embedded_tags(markdown, tags);

            if (is_conflict_marker(note_title, entry.path())) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
                continue;
            }

            const std::string source_hash = stable_hash(compute_sync_payload(note_title, tags, markdown));
            const std::string legacy_source_hash = stable_hash(std::format("{}\n{}", note_title, markdown));
            const std::string known_hash = get_note_hash(note_title);
            auto local_note = get_note_by_title(note_title);

            if (local_note.has_value()) {
                std::string local_tags = trim(local_note->tags);
                std::string local_content = normalize_content(local_note->content);
                extract_embedded_tags(local_content, local_tags);

                if (local_content == markdown && local_tags == tags) {
                    if (local_note->content != local_content || local_note->tags != local_tags) {
                        save_note(note_title, markdown, tags, false);
                        any_changed = true;
                    }
                    if (known_hash != source_hash) {
                        set_note_hash(note_title, entry.path().string(), source_hash);
                    }
                    continue;
                }

                const std::string local_hash = stable_hash(compute_sync_payload(note_title, local_tags, local_content));
                const std::string legacy_local_hash = stable_hash(std::format("{}\n{}", note_title, local_content));

                const bool remote_matches_known = !known_hash.empty() && (source_hash == known_hash || legacy_source_hash == known_hash);
                const bool local_matches_known = !known_hash.empty() && (local_hash == known_hash || legacy_local_hash == known_hash);

                if (remote_matches_known && !local_matches_known) {
                    // Local note was edited since last sync while staging file is still at the old synced baseline
                    continue;
                }

                bool local_conflict = false;
                if (!known_hash.empty() && !remote_matches_known && !local_matches_known && !last_sync.empty()) {
                    local_conflict = is_timestamp_newer(local_note->updated_at, last_sync);
                }

                if (local_conflict) {
                    const std::string conflict_title = std::format("{} (conflict {})", note_title, now_timestamp());
                    save_note(conflict_title, markdown, tags, false);
                } else {
                    save_note(note_title, markdown, tags, false);
                }
                any_changed = true;
            } else {
                save_note(note_title, markdown, tags, false);
                any_changed = true;
            }

            set_note_hash(note_title, entry.path().string(), source_hash);
        }

        if (any_changed) {
            rebuild_links();
        }
        set_sync_meta("notes_last_sync", now_timestamp());
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
