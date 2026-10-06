#pragma once

#include <string>
#include <vector>
#include <regex>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <optional>
#include <algorithm>
#include "sqlite.hpp"
#include "glaze_include.hpp"
#include "process_helper.hpp"

namespace rouen::helpers {

struct SymbolInfo {
    std::string name;
    std::string kind; // "class", "struct", "function", "enum", "namespace", "macro", "alias"
    std::string filepath;
    int line_number{1};
    std::string signature;

    struct glaze {
        using T = SymbolInfo;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "kind", &T::kind,
            "filepath", &T::filepath,
            "line_number", &T::line_number,
            "signature", &T::signature
        );
    };
};

struct CodeSearchMatch {
    std::string filepath;
    int line_number{1};
    std::string line_content;
    std::string snippet;

    struct glaze {
        using T = CodeSearchMatch;
        static constexpr auto value = glz::object(
            "filepath", &T::filepath,
            "line_number", &T::line_number,
            "line_content", &T::line_content,
            "snippet", &T::snippet
        );
    };
};

struct IndexWorkspaceStats {
    size_t total_files_scanned{0};
    size_t files_indexed{0};
    size_t files_skipped{0};
    size_t symbols_indexed{0};
    long long duration_ms{0};

    struct glaze {
        using T = IndexWorkspaceStats;
        static constexpr auto value = glz::object(
            "total_files_scanned", &T::total_files_scanned,
            "files_indexed", &T::files_indexed,
            "files_skipped", &T::files_skipped,
            "symbols_indexed", &T::symbols_indexed,
            "duration_ms", &T::duration_ms
        );
    };
};

class CodeIndexer {
public:
    static CodeIndexer& instance() {
        static CodeIndexer s_instance;
        return s_instance;
    }

    explicit CodeIndexer(const std::string& db_path = "") {
        init_db(db_path);
    }

    void init_db(const std::string& custom_db_path = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string path = custom_db_path;
        if (path.empty()) {
            std::filesystem::path current_dir = std::filesystem::current_path();
            std::filesystem::path rouen_dir = current_dir / ".rouen";
            std::error_code ec;
            std::filesystem::create_directories(rouen_dir, ec);
            path = (rouen_dir / "code_index.db").string();
        }

        try {
            db_ = std::make_unique<hosting::db::sqlite>(path);
            setup_schema();
        } catch (const std::exception& e) {
            std::cerr << "[CodeIndexer] Failed to initialize SQLite database: " << e.what() << "\n";
        }
    }

    /**
     * Check if a file extension belongs to supported C++ / CMake source formats.
     */
    static bool is_supported_source_file(const std::filesystem::path& path) {
        std::string filename = path.filename().string();
        if (filename == "CMakeLists.txt") return true;

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" ||
                ext == ".cc" || ext == ".cxx" || ext == ".cppm" || ext == ".ixx" ||
                ext == ".cmake");
    }

    /**
     * Parse symbols from C/C++ source code line-by-line.
     */
    static std::vector<SymbolInfo> parse_symbols(const std::string& content, const std::string& filepath) {
        std::vector<SymbolInfo> symbols;
        if (content.empty()) return symbols;

        std::istringstream stream(content);
        std::string line;
        int line_no = 0;

        // Regex patterns for C++ symbols
        static const std::regex class_struct_regex(
            R"(^\s*(?:template\s*<[^>]*>\s*)?(?:export\s+)?(class|struct)\s+(?:alignas\([^)]*\)\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*(?:final|base)?)?(?:\s*:\s*[^{;]*)?(?:\{|;|$))",
            std::regex::optimize
        );
        static const std::regex enum_regex(
            R"(^\s*(?:export\s+)?enum\s+(?:class\s+|struct\s+)?([A-Za-z_][A-Za-z0-9_]*))",
            std::regex::optimize
        );
        static const std::regex namespace_regex(
            R"(^\s*(?:inline\s+)?namespace\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\{|\s*::))",
            std::regex::optimize
        );
        static const std::regex alias_regex(
            R"(^\s*(?:export\s+)?using\s+([A-Za-z_][A-Za-z0-9_]*)\s*=)",
            std::regex::optimize
        );
        static const std::regex macro_regex(
            R"(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)(?:\(|\s|$))",
            std::regex::optimize
        );
        // Function declaration/definition regex
        static const std::regex func_regex(
            R"(^\s*(?:(?:virtual|static|inline|explicit|constexpr|consteval|export|extern)\s+)*(?:(?:[A-Za-z_][A-Za-z0-9_:<>\s\*&]+)\s+)?([A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)?)\s*\(([^)]*)\)\s*(?:const|noexcept|override|final|=0)?\s*(?:\{|;|$))",
            std::regex::optimize
        );

        while (std::getline(stream, line)) {
            line_no++;
            if (line.empty()) continue;

            // Trim leading spaces for inspection
            size_t first_char = line.find_first_not_of(" \t\r");
            if (first_char == std::string::npos) continue;
            if (line[first_char] == '/' && first_char + 1 < line.size() && (line[first_char+1] == '/' || line[first_char+1] == '*')) {
                continue; // Skip full-line comments
            }

            std::smatch match;

            // Match class or struct
            if (std::regex_search(line, match, class_struct_regex)) {
                SymbolInfo sym;
                sym.kind = match[1].str();
                sym.name = match[2].str();
                sym.filepath = filepath;
                sym.line_number = line_no;
                sym.signature = line.substr(first_char);
                symbols.push_back(sym);
                continue;
            }

            // Match enum
            if (std::regex_search(line, match, enum_regex)) {
                SymbolInfo sym;
                sym.kind = "enum";
                sym.name = match[1].str();
                sym.filepath = filepath;
                sym.line_number = line_no;
                sym.signature = line.substr(first_char);
                symbols.push_back(sym);
                continue;
            }

            // Match namespace
            if (std::regex_search(line, match, namespace_regex)) {
                SymbolInfo sym;
                sym.kind = "namespace";
                sym.name = match[1].str();
                sym.filepath = filepath;
                sym.line_number = line_no;
                sym.signature = line.substr(first_char);
                symbols.push_back(sym);
                continue;
            }

            // Match using alias
            if (std::regex_search(line, match, alias_regex)) {
                SymbolInfo sym;
                sym.kind = "alias";
                sym.name = match[1].str();
                sym.filepath = filepath;
                sym.line_number = line_no;
                sym.signature = line.substr(first_char);
                symbols.push_back(sym);
                continue;
            }

            // Match macro
            if (std::regex_search(line, match, macro_regex)) {
                SymbolInfo sym;
                sym.kind = "macro";
                sym.name = match[1].str();
                sym.filepath = filepath;
                sym.line_number = line_no;
                sym.signature = line.substr(first_char);
                symbols.push_back(sym);
                continue;
            }

            // Match function / method
            if (line.find('(') != std::string::npos && std::regex_search(line, match, func_regex)) {
                std::string fname = match[1].str();
                if (fname != "if" && fname != "for" && fname != "while" && fname != "switch" && fname != "catch") {
                    SymbolInfo sym;
                    sym.kind = "function";
                    sym.name = fname;
                    sym.filepath = filepath;
                    sym.line_number = line_no;
                    sym.signature = line.substr(first_char);
                    symbols.push_back(sym);
                }
            }
        }

        return symbols;
    }

    /**
     * Index a single source file into SQLite FTS5 and symbols table.
     */
    bool index_file(const std::string& filepath, bool force = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) return false;

        auto last_write = std::filesystem::last_write_time(filepath, ec);
        auto s_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(last_write.time_since_epoch()).count();
        auto fsize = static_cast<long long>(std::filesystem::file_size(filepath, ec));

        if (!force) {
            bool up_to_date = false;
            std::string check_sql = "SELECT mtime, filesize FROM indexed_files WHERE filepath = ?;";
            db_->exec_dynamic(check_sql, [&](sqlite3_stmt* stmt) {
                long long stored_mtime = sqlite3_column_int64(stmt, 0);
                long long stored_size = sqlite3_column_int64(stmt, 1);
                if (stored_mtime == s_since_epoch && stored_size == fsize) {
                    up_to_date = true;
                }
            }, {filepath});

            if (up_to_date) {
                return false; // Skipped, already indexed
            }
        }

        // Read file content
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Remove old entries
        db_->exec_dynamic("DELETE FROM code_fts WHERE filepath = ?;", [](sqlite3_stmt*){}, {filepath});
        db_->exec_dynamic("DELETE FROM symbols WHERE filepath = ?;", [](sqlite3_stmt*){}, {filepath});
        db_->exec_dynamic("DELETE FROM indexed_files WHERE filepath = ?;", [](sqlite3_stmt*){}, {filepath});

        // Insert into FTS5
        db_->exec_dynamic("INSERT INTO code_fts(filepath, content) VALUES(?, ?);", [](sqlite3_stmt*){}, {filepath, content});

        // Extract symbols
        auto symbols = parse_symbols(content, filepath);
        for (const auto& sym : symbols) {
            db_->exec_dynamic(
                "INSERT INTO symbols(name, kind, filepath, line_number, signature) VALUES(?, ?, ?, ?, ?);",
                [](sqlite3_stmt*){},
                {sym.name, sym.kind, sym.filepath, std::to_string(sym.line_number), sym.signature}
            );
        }

        // Update indexed_files
        db_->exec_dynamic(
            "INSERT INTO indexed_files(filepath, mtime, filesize, symbol_count) VALUES(?, ?, ?, ?);",
            [](sqlite3_stmt*){},
            {filepath, std::to_string(s_since_epoch), std::to_string(fsize), std::to_string(symbols.size())}
        );

        return true;
    }

    /**
     * Recursively index an entire workspace directory.
     */
    IndexWorkspaceStats index_workspace(const std::string& workspace_dir = "", bool force = false) {
        IndexWorkspaceStats stats;
        auto start_time = std::chrono::steady_clock::now();

        std::string ws = workspace_dir.empty() ? std::filesystem::current_path().string() : workspace_dir;
        ws = ProcessHelper::expandTilde(ws);

        std::error_code ec;
        if (!std::filesystem::exists(ws, ec)) return stats;

        std::vector<std::string> candidate_files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(ws, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory()) {
                std::string dirname = entry.path().filename().string();
                if (dirname == ".git" || dirname == ".rouen" || dirname == "build" || dirname == "out" ||
                    dirname == "node_modules" || dirname == "target" || dirname == ".cache") {
                    // Skip descending into ignored directories
                    // Note: std::filesystem::recursive_directory_iterator requires manual skip or path check
                    continue;
                }
            } else if (entry.is_regular_file()) {
                std::string path_str = entry.path().string();
                if (path_str.find("/.git/") != std::string::npos ||
                    path_str.find("/build/") != std::string::npos ||
                    path_str.find("/.rouen/") != std::string::npos ||
                    path_str.find("/out/") != std::string::npos) {
                    continue;
                }

                if (is_supported_source_file(entry.path())) {
                    candidate_files.push_back(path_str);
                }
            }
        }

        stats.total_files_scanned = candidate_files.size();

        for (const auto& f : candidate_files) {
            bool indexed = index_file(f, force);
            if (indexed) {
                stats.files_indexed++;
            } else {
                stats.files_skipped++;
            }
        }

        // Count total symbols
        if (db_) {
            db_->exec("SELECT COUNT(*) FROM symbols;", [&](sqlite3_stmt* stmt) {
                stats.symbols_indexed = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
            });
        }

        auto end_time = std::chrono::steady_clock::now();
        stats.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return stats;
    }

    /**
     * Search the indexed codebase using FTS5 trigrams.
     */
    std::vector<CodeSearchMatch> search(const std::string& query, size_t max_results = 50) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CodeSearchMatch> results;
        if (!db_ || query.empty()) return results;

        std::vector<std::pair<std::string, std::string>> fts_rows;
        std::string sql = "SELECT filepath, snippet(code_fts, 1, '«', '»', '...', 20) FROM code_fts WHERE code_fts MATCH ? LIMIT ?;";

        // Wrap and escape query as an FTS5 phrase query so punctuation (e.g. '.', '::', '->') is matched literally
        std::string fts_query = "\"";
        for (char c : query) {
            if (c == '"') {
                fts_query += "\"\"";
            } else {
                fts_query += c;
            }
        }
        fts_query += "\"";

        try {
            db_->exec_dynamic(sql, [&](sqlite3_stmt* stmt) {
                const char* fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const char* sn = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                fts_rows.emplace_back(fp ? fp : "", sn ? sn : "");
            }, {fts_query, std::to_string(max_results)});
        } catch (const std::exception& e) {
            std::cerr << "[CodeIndexer] FTS5 search error for '" << query << "': " << e.what() << "\n";
            return results;
        }

        // For each matching file, locate the precise lines where the match occurs
        for (const auto& [filepath, snippet] : fts_rows) {
            if (filepath.empty()) continue;
            std::ifstream file(filepath);
            if (!file.is_open()) {
                CodeSearchMatch m;
                m.filepath = filepath;
                m.snippet = snippet;
                results.push_back(m);
                continue;
            }

            std::string line;
            int line_no = 0;
            bool matched_line = false;
            std::string lower_query = query;
            std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

            while (std::getline(file, line)) {
                line_no++;
                std::string lower_line = line;
                std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

                if (lower_line.find(lower_query) != std::string::npos) {
                    CodeSearchMatch m;
                    m.filepath = filepath;
                    m.line_number = line_no;
                    m.line_content = line;
                    m.snippet = snippet;
                    results.push_back(m);
                    matched_line = true;
                    if (results.size() >= max_results) break;
                }
            }

            if (!matched_line) {
                CodeSearchMatch m;
                m.filepath = filepath;
                m.line_number = 1;
                m.snippet = snippet;
                results.push_back(m);
            }

            if (results.size() >= max_results) break;
        }

        return results;
    }

    /**
     * Find symbols matching a name (and optional kind filter).
     */
    std::vector<SymbolInfo> find_symbol(const std::string& name, const std::string& kind = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SymbolInfo> results;
        if (!db_ || name.empty()) return results;

        std::string sql;
        std::vector<std::string> params;

        if (name.find('*') != std::string::npos || name.find('%') != std::string::npos) {
            // Wildcard search
            std::string pattern = name;
            std::replace(pattern.begin(), pattern.end(), '*', '%');
            if (kind.empty()) {
                sql = "SELECT name, kind, filepath, line_number, signature FROM symbols WHERE name LIKE ? ORDER BY name LIMIT 100;";
                params = {pattern};
            } else {
                sql = "SELECT name, kind, filepath, line_number, signature FROM symbols WHERE name LIKE ? AND kind = ? ORDER BY name LIMIT 100;";
                params = {pattern, kind};
            }
        } else {
            // Exact search or exact substring match
            if (kind.empty()) {
                sql = "SELECT name, kind, filepath, line_number, signature FROM symbols WHERE name = ? OR name LIKE ? ORDER BY (name = ?) DESC, name LIMIT 100;";
                params = {name, "%" + name + "%", name};
            } else {
                sql = "SELECT name, kind, filepath, line_number, signature FROM symbols WHERE (name = ? OR name LIKE ?) AND kind = ? ORDER BY (name = ?) DESC, name LIMIT 100;";
                params = {name, "%" + name + "%", kind, name};
            }
        }

        try {
            db_->exec_dynamic(sql, [&](sqlite3_stmt* stmt) {
                SymbolInfo sym;
                const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const char* k = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* f = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                int l = sqlite3_column_int(stmt, 3);
                const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

                sym.name = n ? n : "";
                sym.kind = k ? k : "";
                sym.filepath = f ? f : "";
                sym.line_number = std::max(1, l);
                sym.signature = s ? s : "";
                results.push_back(sym);
            }, params);
        } catch (const std::exception& e) {
            std::cerr << "[CodeIndexer] find_symbol error for '" << name << "': " << e.what() << "\n";
        }

        return results;
    }

private:
    void setup_schema() {
        if (!db_) return;

        db_->exec(R"(
            CREATE TABLE IF NOT EXISTS indexed_files (
                filepath TEXT PRIMARY KEY,
                mtime INTEGER NOT NULL,
                filesize INTEGER NOT NULL,
                symbol_count INTEGER NOT NULL DEFAULT 0
            );
        )");

        db_->exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS code_fts USING fts5(
                filepath UNINDEXED,
                content,
                tokenize = 'trigram'
            );
        )");

        db_->exec(R"(
            CREATE TABLE IF NOT EXISTS symbols (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                kind TEXT NOT NULL,
                filepath TEXT NOT NULL,
                line_number INTEGER NOT NULL,
                signature TEXT
            );
        )");

        db_->exec("CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(name);");
        db_->exec("CREATE INDEX IF NOT EXISTS idx_symbols_file ON symbols(filepath);");
    }

    std::mutex mutex_;
    std::unique_ptr<hosting::db::sqlite> db_;
};

} // namespace rouen::helpers
