#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/platform_utils.hpp"

namespace rouen::models::productivity {

    struct process_definition {
        int64_t id{-1};
        std::string name;
        std::string executable_path;
        std::string arguments;
        std::string working_directory;
        std::string icon_source;   // empty = auto-detect icon from executable_path; otherwise a user-picked image path
        std::string created;
        std::string updated;
    };

    class process_definition_repository {
    public:
        process_definition_repository()
            : process_definition_repository(rouen::platform::get_user_data_path("processes.db").string()) {}

        explicit process_definition_repository(const std::string& db_path) : db_{db_path} {
            db_.ensure_table("process_definitions",
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT, "
                "executable_path TEXT, "
                "arguments TEXT, "
                "working_directory TEXT, "
                "icon_source TEXT, "
                "created TEXT, "
                "updated TEXT");
        }

        static std::string current_time_str() {
            auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            return ss.str();
        }

        static std::string safe_column_text(sqlite3_stmt* stmt, int col) {
            const unsigned char* text = sqlite3_column_text(stmt, col);
            return text ? reinterpret_cast<const char*>(text) : "";
        }

        static process_definition read_row(sqlite3_stmt* stmt) {
            process_definition def;
            def.id = sqlite3_column_int64(stmt, 0);
            def.name = safe_column_text(stmt, 1);
            def.executable_path = safe_column_text(stmt, 2);
            def.arguments = safe_column_text(stmt, 3);
            def.working_directory = safe_column_text(stmt, 4);
            def.icon_source = safe_column_text(stmt, 5);
            def.created = safe_column_text(stmt, 6);
            def.updated = safe_column_text(stmt, 7);
            return def;
        }

        std::vector<process_definition> get_all() {
            std::vector<process_definition> list;
            std::string sql = "SELECT id, name, executable_path, arguments, working_directory, icon_source, created, updated "
                              "FROM process_definitions ORDER BY name ASC";
            db_.exec(sql, [&list](sqlite3_stmt* stmt) {
                list.push_back(read_row(stmt));
            });
            return list;
        }

        std::optional<process_definition> get_by_id(int64_t id) {
            std::optional<process_definition> result;
            std::string sql = "SELECT id, name, executable_path, arguments, working_directory, icon_source, created, updated "
                              "FROM process_definitions WHERE id = ?";
            db_.exec(sql, [&result](sqlite3_stmt* stmt) {
                result = read_row(stmt);
            }, id);
            return result;
        }

        int64_t upsert(process_definition& def) {
            std::string now = current_time_str();
            if (def.id <= 0) {
                def.created = now;
                def.updated = now;
                std::string sql = "INSERT INTO process_definitions "
                                  "(name, executable_path, arguments, working_directory, icon_source, created, updated) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?)";
                db_.exec(sql, {}, def.name, def.executable_path, def.arguments, def.working_directory,
                         def.icon_source, def.created, def.updated);
                def.id = db_.last_insert_rowid();
            } else {
                def.updated = now;
                std::string sql = "UPDATE process_definitions SET name=?, executable_path=?, arguments=?, "
                                  "working_directory=?, icon_source=?, updated=? WHERE id=?";
                db_.exec(sql, {}, def.name, def.executable_path, def.arguments, def.working_directory,
                         def.icon_source, def.updated, def.id);
            }
            return def.id;
        }

        bool remove(int64_t id) {
            db_.exec("DELETE FROM process_definitions WHERE id = ?", {}, id);
            return true;
        }

    private:
        hosting::db::sqlite db_;
    };

} // namespace rouen::models::productivity
