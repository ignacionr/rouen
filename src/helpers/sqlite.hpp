#pragma once

#include <format>
#include <functional>
#include <string>
#include <stdexcept>

#include <sqlite3.h>

namespace hosting::db
{
    struct sqlite
    {
        sqlite(const std::string &path)
        {
            if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
            {
                // create the database then
                createDatabase(path.c_str());
            }
        }

        ~sqlite() {
            close();
        }

        void ensure_table(std::string_view table, std::string_view fields)
        {
            std::string sql = std::format("CREATE TABLE IF NOT EXISTS {} ({})", table, fields);
            exec(sql);
        }

        void drop_table(std::string_view table)
        {
            std::string sql = std::format("DROP TABLE IF EXISTS {}", table);
            exec(sql);
        }

        void createDatabase(const char *dbName)
        {
            auto rc = sqlite3_open(dbName, &db_);
            if (rc)
            {
                throw std::runtime_error(std::format("Can't open database: {} - {}",
                    sqlite3_errmsg(db_), dbName));
            }
        }

        void close()
        {
            if (db_)
            {
                sqlite3_close(db_);
                db_ = nullptr;
            }
        }

        void exec(const std::string &sql) {
            auto rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr);
            if (rc != SQLITE_OK)
            {
                throw std::runtime_error(std::format("SQL error: {}", sqlite3_errmsg(db_)));
            }
        }

        using stmt_callback_t = std::function<void(sqlite3_stmt *)>;

        // a variadic version of exec with callback
        template <typename... Args>
        void exec(const std::string &sql, stmt_callback_t callback, Args... args) {
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                throw std::runtime_error(std::format("Failed to prepare statement: {}", sqlite3_errmsg(db_)));
            }

            int index = 1;
            (bind_param(stmt, index++, args), ...);

            while (sqlite3_step(stmt) == SQLITE_ROW && callback) {
                callback(stmt);
            }

            if (sqlite3_finalize(stmt) != SQLITE_OK) {
                throw std::runtime_error(std::format("SQL error: {}", sqlite3_errmsg(db_)));
            }
        }

        long long last_insert_rowid() const {
            return sqlite3_last_insert_rowid(db_);
        }
        
    private:
        template <typename T>
        void bind_param(sqlite3_stmt *stmt, int index, T value) {
            if constexpr (std::is_integral_v<T>) {
                if constexpr (sizeof(T) <= sizeof(int)) {
                    sqlite3_bind_int(stmt, index, value);
                } else {
                    sqlite3_bind_int64(stmt, index, value);
                }
            } else if constexpr (std::is_floating_point_v<T>) {
                sqlite3_bind_double(stmt, index, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, index, value.data(), value.size(), SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
            } else {
                static_assert(always_false<T>::value, "Unsupported parameter type");
            }
        }

        template <typename T>
        struct always_false : std::false_type {};
        sqlite3 *db_ {nullptr};
    };
}