#pragma once

#include <format>
#include <functional>
#include <string>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>

#include <sqlite3.h>
#include "debug.hpp"

namespace hosting::db
{
    struct sqlite
    {
        sqlite(const std::string &path) : db_path_(path)
        {
            DB_INFO_FMT("Opening SQLite database: {}", path);
            
            // Use a more robust connection mode that allows better concurrency
            int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
            
            if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK)
            {
                DB_WARN_FMT("Failed to open DB, creating it: {}", path);
                // create the database then
                createDatabase(path.c_str());
            }
            
            // Configure SQLite for better performance and concurrency
            try {
                DB_DEBUG_FMT("Setting SQLite PRAGMA settings for: {}", path);
                exec("PRAGMA journal_mode = WAL");       // Write-ahead logging for better concurrency
                exec("PRAGMA synchronous = NORMAL");     // Balance between safety and performance
                exec("PRAGMA cache_size = 10000");       // Larger cache (in pages)
                exec("PRAGMA temp_store = MEMORY");      // Store temp tables in memory
                exec("PRAGMA mmap_size = 30000000");     // Memory-mapped I/O (30MB)
                DB_DEBUG_FMT("SQLite PRAGMA settings complete for: {}", path);
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error setting PRAGMA for {}: {}", path, e.what());
            }
        }

        ~sqlite() {
            DB_INFO_FMT("Closing SQLite database: {}", db_path_);
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
            int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
            auto rc = sqlite3_open_v2(dbName, &db_, flags, nullptr);
            if (rc)
            {
                throw std::runtime_error(std::format("Can't open database: {} - {}",
                    sqlite3_errmsg(db_), dbName));
            }
        }

        void close()
        {
            DB_DEBUG_FMT("Acquiring lock to close DB: {}", db_path_);
            std::lock_guard<std::mutex> lock(mutex_);
            DB_DEBUG_FMT("Lock acquired, closing DB: {}", db_path_);
            if (db_)
            {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            DB_DEBUG_FMT("DB closed: {}", db_path_);
        }

        void exec(const std::string &sql) {
            DB_TRACE_FMT("Acquiring lock for exec SQL on DB {}: {}...", db_path_, sql.substr(0, 40));
            std::lock_guard<std::mutex> lock(mutex_);
            DB_TRACE_FMT("Lock acquired, executing SQL on {}", db_path_);
            
            // Retry logic for busy database (5 retries with increasing backoff)
            int retries = 0;
            int rc;
            while (retries < 5) {
                rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr);
                if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
                
                DB_WARN_FMT("Database {} busy/locked, retry attempt {}", db_path_, retries+1);
                // Exponential backoff
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * (1 << retries)));
                retries++;
            }
            
            if (rc != SQLITE_OK)
            {
                std::string error_msg = std::format("SQL error on {}: {}", db_path_, sqlite3_errmsg(db_));
                DB_ERROR(error_msg);
                throw std::runtime_error(error_msg);
            }
            DB_TRACE_FMT("SQL execution complete on {}", db_path_);
        }

        using stmt_callback_t = std::function<void(sqlite3_stmt *)>;

        // a variadic version of exec with callback
        template <typename... Args>
        void exec(const std::string &sql, stmt_callback_t callback, Args... args) {
            DB_TRACE_FMT("Acquiring lock for exec SQL with callback on DB {}: {}...", db_path_, sql.substr(0, 40));
            std::lock_guard<std::mutex> lock(mutex_);
            DB_TRACE_FMT("Lock acquired, preparing statement for SQL on {}", db_path_);
            
            sqlite3_stmt *stmt = nullptr;
            int prepare_retries = 0;
            int prepare_rc;
            
            // Retry prepare if database is busy
            while (prepare_retries < 5) {
                prepare_rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
                if (prepare_rc != SQLITE_BUSY && prepare_rc != SQLITE_LOCKED) break;
                
                DB_WARN_FMT("Database {} busy/locked during prepare, retry attempt {}", db_path_, prepare_retries+1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * (1 << prepare_retries)));
                prepare_retries++;
            }
            
            if (prepare_rc != SQLITE_OK) {
                std::string error_msg = std::format("Failed to prepare statement on {}: {}", db_path_, sqlite3_errmsg(db_));
                DB_ERROR(error_msg);
                throw std::runtime_error(error_msg);
            }

            try {
                int index = 1;
                (bind_param(stmt, index++, args), ...);

                int step_retries = 0;
                int step_rc;
                
                while (true) {
                    step_retries = 0;
                    while (step_retries < 5) {
                        step_rc = sqlite3_step(stmt);
                        if (step_rc != SQLITE_BUSY && step_rc != SQLITE_LOCKED) break;
                        
                        DB_WARN_FMT("Database {} busy/locked during step, retry attempt {}", db_path_, step_retries+1);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50 * (1 << step_retries)));
                        step_retries++;
                    }
                    
                    if (step_rc == SQLITE_ROW && callback) {
                        callback(stmt);
                    } else {
                        break;
                    }
                }
                
                if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW && step_rc != SQLITE_OK) {
                    std::string error_msg = std::format("SQL error during step on {}: {}", db_path_, sqlite3_errmsg(db_));
                    DB_ERROR(error_msg);
                    throw std::runtime_error(error_msg);
                }
            } catch (...) {
                sqlite3_finalize(stmt);
                throw;
            }

            if (sqlite3_finalize(stmt) != SQLITE_OK) {
                std::string error_msg = std::format("SQL error during finalize on {}: {}", db_path_, sqlite3_errmsg(db_));
                DB_ERROR(error_msg);
                throw std::runtime_error(error_msg);
            }
            DB_TRACE_FMT("SQL execution with callback complete on {}", db_path_);
        }

        long long last_insert_rowid() const {
            std::lock_guard<std::mutex> lock(mutex_);
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
                // Handle null or empty string_view specially
                if (value.data() == nullptr || value.empty()) {
                    sqlite3_bind_null(stmt, index);
                } else {
                    sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
                }
            } else {
                // Fix always_false implementation to properly trigger a compile-time error
                static_assert(sizeof(T) != sizeof(T), "Unsupported parameter type");
            }
        }

        sqlite3 *db_ {nullptr};
        mutable std::mutex mutex_;  // Protect concurrent access
        std::string db_path_;       // Keep path for debug info
    };
}