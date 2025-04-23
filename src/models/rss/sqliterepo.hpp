#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <chrono>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/debug.hpp"

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

        // New method to batch-insert items for better performance
        void batch_upsert_items(long long feed_id, const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>>& items)
        {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            RSS_DEBUG_FMT("batch_upsert_items starting for feed_id={}, item count={}", feed_id, items.size());
            if (items.empty()) return;
            
            try {
                // Begin transaction for batch operations
                db_.exec("BEGIN TRANSACTION");
                
                for (const auto& [title, enclosure, link, description, pub_date, image_url] : items) {
                    // Truncate description if it's too large (SQLite text limit is ~1GB but we'll be conservative)
                    std::string truncated_desc = description;
                    if (truncated_desc.length() > 8192) { // Limit to 8KB
                        truncated_desc = truncated_desc.substr(0, 8189) + "...";
                    }
                    
                    std::string sql = "INSERT OR REPLACE INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?)";
                    db_.exec(sql, {}, link, enclosure, feed_id, title, truncated_desc, pub_date, image_url);
                }
                
                // Commit the transaction
                db_.exec("COMMIT");
                RSS_DEBUG_FMT("batch_upsert_items complete for feed_id={}", feed_id);
            } catch (const std::exception& e) {
                // Rollback on error
                try {
                    db_.exec("ROLLBACK");
                } catch (...) {
                    // Ignore rollback errors
                }
                RSS_ERROR_FMT("Error in batch_upsert_items: {}", e.what());
                throw std::runtime_error(std::string("Error in batch_upsert_items: ") + e.what());
            }
        }

        void upsert_item(long long feed_id, std::string_view title, std::string_view enclosure, std::string_view link, std::string_view description, std::string_view pub_date, std::string_view image_url)
        {
            RSS_DEBUG_FMT("upsert_item starting for feed_id={}, link={}", feed_id, link);
            try {
                // Truncate description if it's too large
                std::string truncated_desc;
                if (description.length() > 8192) { // Limit to 8KB
                    truncated_desc = std::string(description.substr(0, 8189)) + "...";
                    description = truncated_desc;
                }
                
                std::string sql = "INSERT OR REPLACE INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) VALUES (?, ?, ?, ?, ?, ?, ?)";
                db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url);
                RSS_DEBUG_FMT("upsert_item complete for feed_id={}, link={}", feed_id, link);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in upsert_item: {}", e.what());
                throw std::runtime_error(std::string("Error in upsert_item: ") + e.what());
            }
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

        void scan_items(long long feed_id, auto sink)
        {
            RSS_DEBUG_FMT("scan_items starting for feed_id={}...", feed_id);
            try {
                // Use a limit clause to avoid potential memory issues with large feeds
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url FROM item WHERE feed_id = ? ORDER BY pub_date DESC LIMIT 500";
                int item_count = 0;
                
                // Set a timeout for this operation to prevent hanging
                auto start_time = std::chrono::steady_clock::now();
                auto timeout = std::chrono::seconds(10); // 10-second timeout
                
                db_.exec(sql, [&sink, &item_count, &start_time, timeout](sqlite3_stmt *stmt) {
                    // Check for timeout periodically
                    if (item_count % 10 == 0) {
                        auto now = std::chrono::steady_clock::now();
                        if (now - start_time > timeout) {
                            RSS_WARN_FMT("scan_items timeout reached, processed {} items", item_count);
                            return 1; // Signal to stop processing
                        }
                    }
                    
                    item_count++;
                    try {
                        // Handle potentially NULL columns safely
                        const char* link = (sqlite3_column_type(stmt, 0) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "";
                        const char* enclosure = (sqlite3_column_type(stmt, 1) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
                        const char* title = (sqlite3_column_type(stmt, 2) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
                        const char* description = (sqlite3_column_type(stmt, 3) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) : "";
                        const char* pub_date = (sqlite3_column_type(stmt, 4) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) : "";
                        const char* image_url = (sqlite3_column_type(stmt, 5) != SQLITE_NULL) ? 
                            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
                            
                        sink(link, enclosure, title, description, pub_date, image_url);
                    } catch (const std::exception& e) {
                        RSS_ERROR_FMT("Error processing RSS item at index {}: {}", item_count, e.what());
                    }
                    return 0;
                }, feed_id);
                
                RSS_DEBUG_FMT("scan_items complete for feed_id={}, found {} items", feed_id, item_count);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_items: {}", e.what());
                throw std::runtime_error(std::string("Error in scan_items: ") + e.what());
            }
        }

    private:
        hosting::db::sqlite db_;
        std::mutex mutex_;  // For thread-safe operations
    };
}