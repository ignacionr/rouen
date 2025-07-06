#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <chrono>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/debug.hpp"
#include "rss_item_repo.hpp"

namespace media::rss
{
    struct sqliterepo
    {
        sqliterepo(const std::string &path) : db_{path}
        {
            RSS_INFO_FMT("Creating SQLite repo for: {}", path);
            
            // Create tables with optimized schema for better performance
            try {
                RSS_DEBUG("Creating feed table...");
                db_.ensure_table("feed", 
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "url TEXT UNIQUE, "
                    "title TEXT, "
                    "image_url TEXT, "
                    "last_updated TEXT"
                );
                
                RSS_DEBUG("Creating item table...");
                db_.ensure_table("item", 
                    "link TEXT PRIMARY KEY, "
                    "enclosure TEXT, "
                    "feed_id INTEGER, "
                    "title TEXT, "
                    "description TEXT, "  // Consider limiting description length
                    "pub_date TEXT, "
                    "image_url TEXT, "
                    "FOREIGN KEY(feed_id) REFERENCES feed(id) ON DELETE CASCADE"
                );
                
                // Create indexes for faster lookups
                RSS_DEBUG("Creating indexes...");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_item_feed_id ON item(feed_id)");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_item_pub_date ON item(pub_date)");
                RSS_DEBUG("SQLite repo setup complete");
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error setting up SQLite repo: {}", e.what());
                // Indexes already exist or another error occurred - continue anyway
            }
        }

        long long upsert_feed(std::string_view url, std::string_view title, std::string_view image_url)
        {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            RSS_DEBUG_FMT("upsert_feed starting for url={}", url);
            std::string sql = "INSERT INTO feed (url, title, image_url, last_updated) "
                              "VALUES (?, ?, ?, datetime('now')) "
                              "ON CONFLICT(url) DO "
                              "UPDATE SET title=excluded.title, image_url=excluded.image_url, last_updated=datetime('now') "
                              "RETURNING id";
            long long result {-1};
            
            try {
                db_.exec(sql, [&result](sqlite3_stmt *stmt) {
                    result = sqlite3_column_int64(stmt, 0);
                }, url, title, image_url);
                
                if (result == -1) {
                    sql = "SELECT id FROM feed WHERE url = ?";
                    db_.exec(sql, [&result](sqlite3_stmt *stmt) {
                        result = sqlite3_column_int64(stmt, 0);
                    }, url);
                }
                RSS_DEBUG_FMT("upsert_feed complete for url={}, id={}", url, result);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in upsert_feed: {}", e.what());
                // Log and rethrow
                throw std::runtime_error(std::string("Error in upsert_feed: ") + e.what());
            }
            
            return result;
        }

        void update_feed(std::string_view url, std::string_view title, std::string_view image_url)
        {
            RSS_DEBUG_FMT("update_feed starting for url={}", url);
            try {
                std::string sql = "UPDATE feed SET title = ?, image_url = ?, last_updated = datetime('now') WHERE url = ?";
                db_.exec(sql, {}, title, image_url, url);
                RSS_DEBUG_FMT("update_feed complete for url={}", url);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in update_feed: {}", e.what());
                throw std::runtime_error(std::string("Error in update_feed: ") + e.what());
            }
        }

        void delete_feed(std::string_view url)
        {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            RSS_DEBUG_FMT("delete_feed starting for url={}", url);
            try {
                // Begin transaction
                db_.exec("BEGIN TRANSACTION");
                
                long long feed_id = -1;
                std::string sql = "SELECT id FROM feed WHERE url = ?";
                db_.exec(sql, [&feed_id](sqlite3_stmt *stmt) {
                    feed_id = sqlite3_column_int64(stmt, 0);
                }, url);

                if (feed_id != -1) {
                    sql = "DELETE FROM item WHERE feed_id = ?";
                    db_.exec(sql, {}, feed_id);

                    sql = "DELETE FROM feed WHERE url = ?";
                    db_.exec(sql, {}, url);
                }
                
                // Commit transaction
                db_.exec("COMMIT");
                RSS_DEBUG_FMT("delete_feed complete for url={}", url);
            } catch (const std::exception& e) {
                // Rollback on error
                try {
                    db_.exec("ROLLBACK");
                } catch (...) {
                    // Ignore rollback errors
                }
                RSS_ERROR_FMT("Error in delete_feed: {}", e.what());
                throw std::runtime_error(std::string("Error in delete_feed: ") + e.what());
            }
        }

        void scan_feeds(auto sink)
        {
            RSS_DEBUG("scan_feeds starting...");
            try {
                std::string sql = "SELECT id, url, title, image_url FROM feed";
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    auto id = sqlite3_column_int64(stmt, 0);
                    auto url = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    auto title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                    auto image_url = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                    RSS_TRACE_FMT("scan_feeds found: id={}, url={}", id, (url ? url : "null"));
                    sink(id, url, title, image_url);
                });
                RSS_DEBUG("scan_feeds complete");
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_feeds: {}", e.what());
                throw std::runtime_error(std::string("Error in scan_feeds: ") + e.what());
            }
        }

        // Forward batch_upsert_items to maintain compatibility with existing code
        void batch_upsert_items(long long feed_id, const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>>& items) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            RSS_DEBUG_FMT("batch_upsert_items starting for feed_id={}, item count={}", feed_id, items.size());
            if (items.empty()) return;
            
            try {
                // Begin transaction for batch operations
                db_.exec("BEGIN TRANSACTION");
                
                for (const auto& [title, enclosure, link, description, pub_date, image_url] : items) {
                    std::string sql = "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) "
                                    "VALUES (?, ?, ?, ?, ?, ?, ?) "
                                    "ON CONFLICT(link) DO "
                                    "UPDATE SET enclosure=excluded.enclosure, title=excluded.title, "
                                    "description=excluded.description, pub_date=excluded.pub_date, image_url=excluded.image_url";
                    
                    db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url);
                }
                
                // Commit the transaction
                db_.exec("COMMIT");
                RSS_DEBUG_FMT("batch_upsert_items completed for feed_id={}", feed_id);
                
            } catch (const std::exception& e) {
                // Rollback on error
                db_.exec("ROLLBACK");
                RSS_ERROR_FMT("Error in batch_upsert_items: {}", e.what());
            }
        }

        // Forward scan_items to maintain compatibility with existing code
        template <typename Sink>
        void scan_items(long long feed_id, Sink sink) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            try {
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url FROM item WHERE feed_id = ? ORDER BY pub_date DESC";
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* enclosure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* pub_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    const char* image_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    
                    sink(
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : ""
                    );
                }, feed_id);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_items: {}", e.what());
            }
        }

        // Item-related methods are now in rss_item_repo
    private:
        hosting::db::sqlite db_;
        std::mutex mutex_;  // For thread-safe operations
    };
}
