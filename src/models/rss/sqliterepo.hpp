#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <chrono>
#include <optional>
#include <tuple>

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
                
                // Migration: check if 'item' table schema needs update
                // If it has link as the sole primary key, we drop it and let ensure_table recreate it.
                try {
                    int pk_count = 0;
                    bool link_is_pk = false;
                    db_.exec("PRAGMA table_info(item)", [&](sqlite3_stmt* stmt) {
                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        int pk = sqlite3_column_int(stmt, 5);
                        if (pk > 0) {
                            pk_count++;
                            if (name && std::string_view(name) == "link") {
                                link_is_pk = true;
                            }
                        }
                    });
                    if (pk_count == 1 && link_is_pk) {
                        RSS_INFO("Old 'item' table schema detected. Dropping table for recreation...");
                        db_.drop_table("item");
                    }
                } catch (const std::exception& e) {
                    RSS_WARN_FMT("Failed to check or drop old 'item' table: {}", e.what());
                }

                RSS_DEBUG("Creating item table...");
                db_.ensure_table("item", 
                    "feed_id INTEGER NOT NULL, "
                    "link TEXT NOT NULL, "
                    "title TEXT NOT NULL, "
                    "enclosure TEXT, "
                    "description TEXT, "  // Consider limiting description length
                    "pub_date TEXT, "
                    "image_url TEXT, "
                    "watermark REAL, "
                    "PRIMARY KEY(feed_id, link, title), "
                    "FOREIGN KEY(feed_id) REFERENCES feed(id) ON DELETE CASCADE"
                );
                
                // Migration: check if 'watermark' column exists, and add it if not
                try {
                    bool has_watermark = false;
                    db_.exec("PRAGMA table_info(item)", [&](sqlite3_stmt* stmt) {
                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        if (name && std::string_view(name) == "watermark") {
                            has_watermark = true;
                        }
                    });
                    if (!has_watermark) {
                        RSS_INFO("Adding 'watermark' column to 'item' table...");
                        db_.exec("ALTER TABLE item ADD COLUMN watermark REAL");
                        // Initialize watermark to 0 for items without enclosure, NULL for items with enclosure
                        db_.exec("UPDATE item SET watermark = 0 WHERE enclosure IS NULL OR enclosure = ''");
                    }
                } catch (const std::exception& e) {
                    RSS_WARN_FMT("Failed to migrate 'item' table watermark column: {}", e.what());
                }

                // Migration: check if 'language' column exists in 'feed' table, and add it if not
                try {
                    bool has_language = false;
                    db_.exec("PRAGMA table_info(feed)", [&](sqlite3_stmt* stmt) {
                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        if (name && std::string_view(name) == "language") {
                            has_language = true;
                        }
                    });
                    if (!has_language) {
                        RSS_INFO("Adding 'language' column to 'feed' table...");
                        db_.exec("ALTER TABLE feed ADD COLUMN language TEXT DEFAULT ''");
                    }
                } catch (const std::exception& e) {
                    RSS_WARN_FMT("Failed to migrate 'feed' table language column: {}", e.what());
                }
                
                RSS_DEBUG("Creating settings table...");
                db_.ensure_table("settings",
                    "key TEXT PRIMARY KEY, "
                    "value TEXT"
                );

                RSS_DEBUG("Creating feed_tag table...");
                db_.ensure_table("feed_tag",
                    "feed_id INTEGER NOT NULL, "
                    "tag TEXT NOT NULL, "
                    "PRIMARY KEY(feed_id, tag), "
                    "FOREIGN KEY(feed_id) REFERENCES feed(id) ON DELETE CASCADE"
                );

                RSS_DEBUG("Creating rss_tag_definition table...");
                db_.ensure_table("rss_tag_definition",
                    "tag TEXT PRIMARY KEY, "
                    "sort_order INTEGER NOT NULL"
                );

                db_.exec(
                    "INSERT OR IGNORE INTO rss_tag_definition(tag, sort_order) VALUES "
                    "('News', 10), "
                    "('Tech / Dev', 20), "
                    "('Podcasts', 30), "
                    "('YouTube', 40), "
                    "('Music', 50), "
                    "('Science', 55), "
                    "('Comedy', 60), "
                    "('Documentary', 70), "
                    "('Social', 75), "
                    "('Other', 80),"
                    "('Argentina', 110),"
                    "('Uruguay', 120),"
                    "('Thailand', 130),"
                    "('USA', 140),"
                    "('Italy', 150)"
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

        void update_feed_language(long long feed_id, std::string_view language)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("UPDATE feed SET language = ? WHERE id = ?", [](sqlite3_stmt*){}, language, feed_id);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error updating feed language: {}", e.what());
            }
        }

        std::set<std::string> get_feed_tags(long long feed_id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::set<std::string> tags;
            try {
                db_.exec("SELECT tag FROM feed_tag WHERE feed_id = ?", [&](sqlite3_stmt* stmt) {
                    const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (tag) {
                        tags.insert(tag);
                    }
                }, feed_id);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error getting feed tags: {}", e.what());
            }
            return tags;
        }

        std::vector<std::string> get_available_tags()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<std::string> tags;
            try {
                db_.exec("SELECT tag FROM rss_tag_definition ORDER BY sort_order ASC, tag ASC", [&](sqlite3_stmt* stmt) {
                    const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (tag) {
                        tags.emplace_back(tag);
                    }
                });
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error getting available tags: {}", e.what());
            }
            return tags;
        }

        void add_feed_tag(long long feed_id, std::string_view tag)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("INSERT OR IGNORE INTO feed_tag (feed_id, tag) VALUES (?, ?)", [](sqlite3_stmt*){}, feed_id, tag);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error adding feed tag: {}", e.what());
            }
        }

        void remove_feed_tag(long long feed_id, std::string_view tag)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("DELETE FROM feed_tag WHERE feed_id = ? AND tag = ?", [](sqlite3_stmt*){}, feed_id, tag);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error removing feed tag: {}", e.what());
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

        void update_feed_url(std::string_view old_url, std::string_view new_url)
        {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            RSS_DEBUG_FMT("update_feed_url starting: {} -> {}", old_url, new_url);
            try {
                // Check if new_url already exists
                long long existing_id = -1;
                std::string check_sql = "SELECT id FROM feed WHERE url = ?";
                db_.exec(check_sql, [&existing_id](sqlite3_stmt *stmt) {
                    existing_id = sqlite3_column_int64(stmt, 0);
                }, new_url);
                
                if (existing_id != -1) {
                    RSS_INFO_FMT("Target URL {} already exists. Merging old feed {} into it...", new_url, old_url);
                    // Find old feed id
                    long long old_id = -1;
                    std::string get_old_sql = "SELECT id FROM feed WHERE url = ?";
                    db_.exec(get_old_sql, [&old_id](sqlite3_stmt *stmt) {
                        old_id = sqlite3_column_int64(stmt, 0);
                    }, old_url);
                    
                    if (old_id != -1 && old_id != existing_id) {
                        db_.exec("BEGIN TRANSACTION");
                        // Re-associate items of the old feed to the existing feed
                        db_.exec("UPDATE OR IGNORE item SET feed_id = ? WHERE feed_id = ?", {}, existing_id, old_id);
                        // Delete any remaining items for old feed that couldn't be updated due to duplicates
                        db_.exec("DELETE FROM item WHERE feed_id = ?", {}, old_id);
                        // Delete the old feed
                        db_.exec("DELETE FROM feed WHERE id = ?", {}, old_id);
                        db_.exec("COMMIT");
                    }
                } else {
                    std::string sql = "UPDATE feed SET url = ?, last_updated = datetime('now') WHERE url = ?";
                    db_.exec(sql, {}, new_url, old_url);
                }
                RSS_DEBUG("update_feed_url complete");
            } catch (const std::exception& e) {
                try { db_.exec("ROLLBACK"); } catch (...) {}
                RSS_ERROR_FMT("Error in update_feed_url: {}", e.what());
                throw std::runtime_error(std::string("Error in update_feed_url: ") + e.what());
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
                std::string sql = "SELECT id, url, title, image_url, language FROM feed";
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    auto id = sqlite3_column_int64(stmt, 0);
                    auto url = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    auto title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                    auto image_url = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                    auto language = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                    RSS_TRACE_FMT("scan_feeds found: id={}, url={}", id, (url ? url : "null"));
                    sink(id, url, title, image_url, language ? language : "");
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
                    std::string sql = "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url, watermark) "
                                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                                    "ON CONFLICT(feed_id, link, title) DO "
                                    "UPDATE SET enclosure=excluded.enclosure, "
                                    "description=excluded.description, pub_date=excluded.pub_date, image_url=excluded.image_url, "
                                    "watermark=COALESCE(item.watermark, excluded.watermark)";
                    
                    bool has_media = !enclosure.empty();
                    if (has_media) {
                        db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url, std::nullopt);
                    } else {
                        db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url, 0.0);
                    }
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

        void upsert_item_by_link(long long feed_id,
                                 std::string_view link,
                                 std::string_view title,
                                 std::string_view enclosure,
                                 std::string_view description,
                                 std::string_view pub_date,
                                 std::string_view image_url) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            try {
                std::optional<double> watermark = enclosure.empty() ? std::optional<double>{0.0} : std::nullopt;
                db_.exec("BEGIN TRANSACTION");
                db_.exec("DELETE FROM item WHERE feed_id = ? AND link = ?", {}, feed_id, link);
                db_.exec(
                    "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url, watermark) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    {},
                    link,
                    enclosure,
                    feed_id,
                    title,
                    description,
                    pub_date,
                    image_url,
                    watermark
                );
                db_.exec("COMMIT");
            } catch (const std::exception& e) {
                try { db_.exec("ROLLBACK"); } catch (...) {}
                RSS_ERROR_FMT("Error in upsert_item_by_link: {}", e.what());
                throw;
            }
        }

        // Forward scan_items to maintain compatibility with existing code
        template <typename Sink>
        void scan_items(long long feed_id, Sink sink) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            try {
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url, watermark FROM item WHERE feed_id = ? ORDER BY pub_date DESC";
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* enclosure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* pub_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    const char* image_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    
                    bool is_null = (sqlite3_column_type(stmt, 6) == SQLITE_NULL);
                    double watermark_val = sqlite3_column_double(stmt, 6);
                    std::optional<double> watermark = is_null ? std::nullopt : std::optional<double>(watermark_val);
                    
                    sink(
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark
                    );
                }, feed_id);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_items: {}", e.what());
            }
        }

        template <typename Sink>
        void scan_items_limit(long long feed_id, int limit, Sink sink)
        {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            try {
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url, watermark FROM item WHERE feed_id = ? ORDER BY pub_date DESC LIMIT ?";
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* enclosure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* pub_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    const char* image_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    
                    bool is_null = (sqlite3_column_type(stmt, 6) == SQLITE_NULL);
                    double watermark_val = sqlite3_column_double(stmt, 6);
                    std::optional<double> watermark = is_null ? std::nullopt : std::optional<double>(watermark_val);
                    
                    sink(
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark
                    );
                }, feed_id, limit);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_items_limit: {}", e.what());
            }
        }
        template <typename Sink>
        void search_items(std::string_view query, Sink sink) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            try {
                std::string sql = "SELECT item.feed_id, feed.title, item.link, item.enclosure, item.title, item.description, item.pub_date, item.image_url, item.watermark "
                                  "FROM item JOIN feed ON item.feed_id = feed.id "
                                  "WHERE item.title LIKE ? OR item.description LIKE ? "
                                  "ORDER BY item.pub_date DESC LIMIT 100";
                
                std::string like_query = "%" + std::string(query) + "%";
                
                db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                    auto feed_id = sqlite3_column_int64(stmt, 0);
                    const char* feed_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* enclosure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    const char* pub_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                    const char* image_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                    
                    bool is_null = (sqlite3_column_type(stmt, 8) == SQLITE_NULL);
                    double watermark_val = sqlite3_column_double(stmt, 8);
                    std::optional<double> watermark = is_null ? std::nullopt : std::optional<double>(watermark_val);
                    
                    sink(
                        feed_id,
                        feed_title ? feed_title : "",
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark
                    );
                }, like_query, like_query);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in search_items: {}", e.what());
            }
        }

        void set_setting(const std::string& key, const std::string& value) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)", {}, key, value);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in set_setting: {}", e.what());
            }
        }

        std::string get_setting(const std::string& key, const std::string& default_value = "") {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string result = default_value;
            try {
                db_.exec("SELECT value FROM settings WHERE key = ?", [&result](sqlite3_stmt* stmt) {
                    const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (val) result = val;
                }, key);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in get_setting: {}", e.what());
            }
            return result;
        }

        void update_watermark(long long feed_id, const std::string& link, const std::string& title, std::optional<double> watermark) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            try {
                db_.exec("UPDATE item SET watermark = ? WHERE feed_id = ? AND link = ? AND title = ?", {}, watermark, feed_id, link, title);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in update_watermark: {}", e.what());
            }
        }

        // Item-related methods are now in rss_item_repo
    private:
        hosting::db::sqlite db_;
        std::mutex mutex_;  // For thread-safe operations
    };
}
