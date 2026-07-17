#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <chrono>
#include <optional>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <set>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/glaze_include.hpp"
#include "rss_item_repo.hpp"
#include "smart_list_filter.hpp"

namespace media::rss
{
    namespace {
        inline std::optional<std::chrono::system_clock::time_point> parse_relative_time(std::string_view val) {
            std::string s(val);
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            
            if (s == "today") {
                auto now = std::chrono::system_clock::now();
                return std::chrono::floor<std::chrono::days>(now);
            }
            
            if (s.starts_with("last ")) {
                std::string num_part = s.substr(5);
                size_t unit_idx = num_part.find_first_not_of("0123456789 \t");
                if (unit_idx != std::string::npos) {
                    std::string num_str = num_part.substr(0, unit_idx);
                    std::string unit_str = num_part.substr(unit_idx);
                    unit_str.erase(0, unit_str.find_first_not_of(" \t"));
                    
                    try {
                        int num = std::stoi(num_str);
                        auto now = std::chrono::system_clock::now();
                        
                        if (unit_str == "h" || unit_str.starts_with("hour")) {
                            return now - std::chrono::hours(num);
                        } else if (unit_str == "d" || unit_str.starts_with("day")) {
                            return now - std::chrono::days(num);
                        } else if (unit_str == "m" || unit_str.starts_with("min")) {
                            return now - std::chrono::minutes(num);
                        }
                    } catch (...) {}
                }
            }
            return std::nullopt;
        }

        inline std::string filter_field_to_column(const std::string& field) {
            if (field == "title") return "item.title";
            if (field == "description" || field == "summary") return "item.description";
            if (field == "pub_date" || field == "published_date") return "item.pub_date";
            if (field == "media_duration_seconds" || field == "media_duration") return "item.media_duration_seconds";
            if (field == "feed_tag" || field == "tags") return "feed_tag.tag";
            return "item.title";
        }

        inline std::string filter_op_to_sql(const std::string& op) {
            if (op == ">") return " > ?";
            if (op == "<") return " < ?";
            if (op == ">=") return " >= ?";
            if (op == "<=") return " <= ?";
            if (op == "==" || op == "=") return " = ?";
            if (op == "!=") return " != ?";
            if (op == "CONTAINS") return " LIKE ?";
            if (op == "EXCLUDES") return " NOT LIKE ?";
            if (op == "MATCHES") return " GLOB ?";
            if (op == "IN") return " IN ";
            if (op == "NOT IN") return " NOT IN ";
            return " = ?";
        }
    }

    struct feed_subscription_dto
    {
        std::string url;
        std::string title;
        std::string image_url;
        std::string language;
        std::vector<std::string> tags;

        struct glaze {
            using T = feed_subscription_dto;
            static constexpr auto value = glz::object(
                "url", &T::url,
                "title", &T::title,
                "image_url", &T::image_url,
                "language", &T::language,
                "tags", &T::tags
            );
        };
    };

    struct rss_sync_dto
    {
        std::vector<feed_subscription_dto> subscriptions;

        struct glaze {
            using T = rss_sync_dto;
            static constexpr auto value = glz::object(
                "subscriptions", &T::subscriptions
            );
        };
    };
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
                    "('Italy', 150),"
                    "('Russia', 160)"
                );

                // Migration: check if 'media_duration_seconds' column exists, and add it if not
                try {
                    bool has_duration = false;
                    db_.exec("PRAGMA table_info(item)", [&](sqlite3_stmt* stmt) {
                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        if (name && std::string_view(name) == "media_duration_seconds") {
                            has_duration = true;
                        }
                    });
                    if (!has_duration) {
                        RSS_INFO("Adding 'media_duration_seconds' column to 'item' table...");
                        db_.exec("ALTER TABLE item ADD COLUMN media_duration_seconds REAL");
                    }
                } catch (const std::exception& e) {
                    RSS_WARN_FMT("Failed to migrate 'item' table media_duration_seconds column: {}", e.what());
                }

                RSS_DEBUG("Creating smart_list table...");
                db_.ensure_table("smart_list",
                    "title TEXT PRIMARY KEY, "
                    "filter_json TEXT"
                );

                // Create indexes for faster lookups
                RSS_DEBUG("Creating indexes...");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_item_feed_id ON item(feed_id)");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_item_pub_date ON item(pub_date)");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_item_duration ON item(media_duration_seconds)");
                db_.exec("CREATE INDEX IF NOT EXISTS idx_feed_tag_tag ON feed_tag(tag)");
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
        void batch_upsert_items(long long feed_id, const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::optional<double>>>& items) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            
            RSS_DEBUG_FMT("batch_upsert_items starting for feed_id={}, item count={}", feed_id, items.size());
            if (items.empty()) return;
            
            try {
                // Begin transaction for batch operations
                db_.exec("BEGIN TRANSACTION");
                
                for (const auto& [title, enclosure, link, description, pub_date, image_url, media_duration_seconds] : items) {
                    std::string sql = "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url, watermark, media_duration_seconds) "
                                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                                    "ON CONFLICT(feed_id, link, title) DO "
                                    "UPDATE SET enclosure=excluded.enclosure, "
                                    "description=excluded.description, pub_date=excluded.pub_date, image_url=excluded.image_url, "
                                    "watermark=COALESCE(item.watermark, excluded.watermark), "
                                    "media_duration_seconds=COALESCE(item.media_duration_seconds, excluded.media_duration_seconds)";
                    
                    bool has_media = !enclosure.empty();
                    std::optional<double> watermark = has_media ? std::nullopt : std::optional<double>(0.0);
                    db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url, watermark, media_duration_seconds);
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
                                 std::string_view image_url,
                                 std::optional<double> media_duration_seconds = std::nullopt) {
            std::lock_guard<std::mutex> lock(mutex_); // Thread safety
            try {
                std::optional<double> watermark = enclosure.empty() ? std::optional<double>{0.0} : std::nullopt;
                db_.exec("BEGIN TRANSACTION");
                db_.exec("DELETE FROM item WHERE feed_id = ? AND link = ?", {}, feed_id, link);
                db_.exec(
                    "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url, watermark, media_duration_seconds) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    {},
                    link,
                    enclosure,
                    feed_id,
                    title,
                    description,
                    pub_date,
                    image_url,
                    watermark,
                    media_duration_seconds
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
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url, watermark, media_duration_seconds FROM item WHERE feed_id = ? ORDER BY pub_date DESC";
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
                    
                    bool is_dur_null = (sqlite3_column_type(stmt, 7) == SQLITE_NULL);
                    double dur_val = sqlite3_column_double(stmt, 7);
                    std::optional<double> media_duration_seconds = is_dur_null ? std::nullopt : std::optional<double>(dur_val);
                    
                    sink(
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark,
                        media_duration_seconds
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
                std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url, watermark, media_duration_seconds FROM item WHERE feed_id = ? ORDER BY pub_date DESC LIMIT ?";
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
                    
                    bool is_dur_null = (sqlite3_column_type(stmt, 7) == SQLITE_NULL);
                    double dur_val = sqlite3_column_double(stmt, 7);
                    std::optional<double> media_duration_seconds = is_dur_null ? std::nullopt : std::optional<double>(dur_val);
                    
                    sink(
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark,
                        media_duration_seconds
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
                std::string sql = "SELECT item.feed_id, feed.title, item.link, item.enclosure, item.title, item.description, item.pub_date, item.image_url, item.watermark, item.media_duration_seconds "
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
                    
                    bool is_dur_null = (sqlite3_column_type(stmt, 9) == SQLITE_NULL);
                    double dur_val = sqlite3_column_double(stmt, 9);
                    std::optional<double> media_duration_seconds = is_dur_null ? std::nullopt : std::optional<double>(dur_val);
                    
                    sink(
                        feed_id,
                        feed_title ? feed_title : "",
                        link ? link : "", 
                        enclosure ? enclosure : "", 
                        title ? title : "", 
                        description ? description : "", 
                        pub_date ? pub_date : "", 
                        image_url ? image_url : "",
                        watermark,
                        media_duration_seconds
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

        void update_item_duration(const std::string& link, double duration_seconds) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("UPDATE item SET media_duration_seconds = ? WHERE link = ? AND media_duration_seconds IS NULL",
                         {}, duration_seconds, link);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in update_item_duration: {}", e.what());
            }
        }

        // Yields (link, enclosure) for items whose duration is still unknown and
        // whose enclosure URL looks like a YouTube link. Callers use these to
        // run yt-dlp and then call update_item_duration().
        template<typename Sink>
        void scan_items_missing_youtube_duration(int limit, Sink sink) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec(
                    "SELECT link, enclosure FROM item "
                    "WHERE media_duration_seconds IS NULL "
                    "AND (enclosure LIKE '%youtube.com%' OR enclosure LIKE '%youtu.be%') "
                    "LIMIT ?",
                    [&sink](sqlite3_stmt* stmt) {
                        const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        const char* enc  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        sink(link ? link : "", enc ? enc : "");
                    },
                    limit
                );
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_items_missing_youtube_duration: {}", e.what());
            }
        }

        void save_smart_list(const std::string& title, const std::string& filter_json) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("INSERT OR REPLACE INTO smart_list (title, filter_json) VALUES (?, ?)", {}, title, filter_json);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error saving smart list: {}", e.what());
            }
        }
        
        void delete_smart_list(const std::string& title) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("DELETE FROM smart_list WHERE title = ?", {}, title);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error deleting smart list: {}", e.what());
            }
        }
        
        template<typename Sink>
        void scan_smart_lists(Sink sink) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec("SELECT title, filter_json FROM smart_list ORDER BY title ASC", [&sink](sqlite3_stmt* stmt) {
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* filter_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    sink(title ? title : "", filter_json ? filter_json : "");
                });
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error scanning smart lists: {}", e.what());
            }
        }

        template<typename Sink>
        void scan_filtered_items(const filter_group& filter, Sink sink) {
            std::string sql = "SELECT DISTINCT item.feed_id, feed.title, item.link, item.enclosure, item.title, item.description, item.pub_date, item.image_url, item.watermark, item.media_duration_seconds "
                              "FROM item "
                              "JOIN feed ON item.feed_id = feed.id "
                              "LEFT JOIN feed_tag ON feed.id = feed_tag.feed_id";
            
            std::vector<std::string> params;
            if (!filter.conditions.empty()) {
                sql += " WHERE ";
                for (size_t i = 0; i < filter.conditions.size(); ++i) {
                    const auto& cond = filter.conditions[i];
                    if (i > 0) {
                        sql += " " + filter.op + " ";
                    }
                    
                    std::string col = filter_field_to_column(cond.field);
                    // Treat missing media duration as 0 so short-duration filters include fresh
                    // items whose duration metadata has not been extracted yet.
                    if ((cond.field == "media_duration_seconds" || cond.field == "media_duration") &&
                        cond.op != "IN" && cond.op != "NOT IN" &&
                        cond.op != "CONTAINS" && cond.op != "EXCLUDES" && cond.op != "MATCHES") {
                        col = "COALESCE(item.media_duration_seconds, 0)";
                    }
                    std::string op_sql = filter_op_to_sql(cond.op);
                    
                    if (cond.op == "CONTAINS" || cond.op == "EXCLUDES") {
                        sql += col + op_sql;
                        params.push_back("%" + cond.value + "%");
                    } else if (cond.op == "IN" || cond.op == "NOT IN") {
                        std::vector<std::string> tags;
                        std::stringstream ss(cond.value);
                        std::string tag;
                        while (std::getline(ss, tag, ',')) {
                            tag.erase(0, tag.find_first_not_of(" \t"));
                            tag.erase(tag.find_last_not_of(" \t") + 1);
                            if (!tag.empty()) {
                                tags.push_back(tag);
                            }
                        }
                        
                        sql += col + (cond.op == "IN" ? " IN (" : " NOT IN (");
                        for (size_t j = 0; j < tags.size(); ++j) {
                            if (j > 0) sql += ", ";
                            sql += "?";
                            params.push_back(tags[j]);
                        }
                        sql += ")";
                    } else {
                        const bool is_numeric_field = (cond.field == "media_duration_seconds" || cond.field == "media_duration");
                        if (is_numeric_field) {
                            // All params are bound as TEXT via sqlite3_bind_text. SQLite's type ordering
                            // places REAL < TEXT, so a bare ? would never compare correctly against a
                            // numeric column. Force the parameter to REAL with an explicit cast.
                            std::string numeric_op_sql = op_sql;
                            if (auto pos = numeric_op_sql.rfind('?'); pos != std::string::npos) {
                                numeric_op_sql.replace(pos, 1, "CAST(? AS REAL)");
                            }
                            sql += col + numeric_op_sql;
                        } else {
                            sql += col + op_sql;
                        }

                        if (cond.field == "pub_date" || cond.field == "published_date") {
                            auto rel_time = parse_relative_time(cond.value);
                            if (rel_time.has_value()) {
                                std::string val_lower = cond.value;
                                std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(), ::tolower);
                                if (val_lower == "today") {
                                    params.push_back(std::format("{:%F} 00:00:00", rel_time.value()));
                                } else {
                                    params.push_back(std::format("{:%F %T}", rel_time.value()));
                                }
                            } else {
                                params.push_back(cond.value);
                            }
                        } else {
                            params.push_back(cond.value);
                        }
                    }
                }
            }
            
            sql += " ORDER BY item.pub_date DESC LIMIT 200";
            
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                db_.exec_dynamic(sql, [&sink](sqlite3_stmt* stmt) {
                    auto feed_id = sqlite3_column_int64(stmt, 0);
                    const char* feed_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* link = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* enclosure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    const char* pub_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                    const char* image_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                    
                    bool is_watermark_null = (sqlite3_column_type(stmt, 8) == SQLITE_NULL);
                    double watermark_val = sqlite3_column_double(stmt, 8);
                    std::optional<double> watermark = is_watermark_null ? std::nullopt : std::optional<double>(watermark_val);
                    
                    bool is_dur_null = (sqlite3_column_type(stmt, 9) == SQLITE_NULL);
                    double dur_val = sqlite3_column_double(stmt, 9);
                    std::optional<double> duration = is_dur_null ? std::nullopt : std::optional<double>(dur_val);
                    
                    sink(
                        feed_id,
                        feed_title ? feed_title : "",
                        link ? link : "",
                        enclosure ? enclosure : "",
                        title ? title : "",
                        description ? description : "",
                        pub_date ? pub_date : "",
                        image_url ? image_url : "",
                        watermark,
                        duration
                    );
                }, params);
            } catch (const std::exception& e) {
                RSS_ERROR_FMT("Error in scan_filtered_items: {}", e.what());
            }
        }

        // Export subscriptions to a directory of JSON files
        void export_to_directory(const std::filesystem::path& directory) {
            std::filesystem::create_directories(directory);

            rss_sync_dto dto;
            scan_feeds([this, &dto](long long id, const char* url, const char* title, const char* image_url, const char* language) {
                feed_subscription_dto sub;
                sub.url = url ? url : "";
                sub.title = title ? title : "";
                sub.image_url = image_url ? image_url : "";
                sub.language = language ? language : "";

                auto tag_set = get_feed_tags(id);
                for (const auto& tag : tag_set) {
                    sub.tags.push_back(tag);
                }
                // Sort tags for consistency
                std::sort(sub.tags.begin(), sub.tags.end());

                dto.subscriptions.push_back(sub);
            });

            // Sort subscriptions by URL for consistent Git diffs
            std::sort(dto.subscriptions.begin(), dto.subscriptions.end(), [](const feed_subscription_dto& a, const feed_subscription_dto& b) {
                return a.url < b.url;
            });

            auto path = directory / "feeds.json";
            std::string json_content = glz::write<glz::opts{.prettify = true}>(dto).value_or("");
            if (!json_content.empty()) {
                std::ofstream f(path);
                if (f) {
                    f << json_content;
                }
            }

            // Export Smart Lists
            rss_smart_lists_sync_dto sl_dto;
            scan_smart_lists([&sl_dto](const std::string& title, const std::string& filter_json) {
                smart_list_dto sub;
                sub.title = title;
                auto err = glz::read_json(sub.filter, filter_json);
                if (!err) {
                    sl_dto.smart_lists.push_back(sub);
                } else {
                    RSS_ERROR_FMT("Failed to parse filter JSON for smart list export: {}", title);
                }
            });

            // Sort smart lists by title for consistent Git diffs
            std::sort(sl_dto.smart_lists.begin(), sl_dto.smart_lists.end(), [](const smart_list_dto& a, const smart_list_dto& b) {
                return a.title < b.title;
            });

            auto sl_path = directory / "smart_lists.json";
            std::string sl_json_content = glz::write<glz::opts{.prettify = true}>(sl_dto).value_or("");
            if (!sl_json_content.empty()) {
                std::ofstream f(sl_path);
                if (f) {
                    f << sl_json_content;
                }
            }
        }

        // Import subscriptions from a directory of JSON files
        void import_from_directory(const std::filesystem::path& directory) {
            auto path = directory / "feeds.json";
            if (std::filesystem::exists(path)) {
                std::ifstream f(path);
                if (f) {
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    rss_sync_dto dto;
                    auto err = glz::read_json(dto, content);
                    if (!err) {
                        // Find current feeds in the local database to check for existence/deletions
                        std::map<std::string, long long> existing_feeds; // url -> id
                        scan_feeds([&existing_feeds](long long id, const char* url, const char* /*title*/, const char* /*image_url*/, const char* /*language*/) {
                            if (url) {
                                existing_feeds[url] = id;
                            }
                        });

                        std::vector<std::string> imported_urls;

                        for (const auto& sub : dto.subscriptions) {
                            imported_urls.push_back(sub.url);

                            // Upsert the feed
                            long long feed_id = upsert_feed(sub.url, sub.title, sub.image_url);
                            if (feed_id != -1) {
                                update_feed_language(feed_id, sub.language);

                                // Sync tags: clear existing and insert imported
                                auto existing_tags = get_feed_tags(feed_id);
                                for (const auto& tag : existing_tags) {
                                    remove_feed_tag(feed_id, tag);
                                }
                                for (const auto& tag : sub.tags) {
                                    add_feed_tag(feed_id, tag);
                                }
                            }
                        }

                        // Sync deletion: if a feed is in existing_feeds but not in imported_urls, it was deleted on another device
                        for (const auto& [url, id] : existing_feeds) {
                            if (std::find(imported_urls.begin(), imported_urls.end(), url) == imported_urls.end()) {
                                delete_feed(url);
                            }
                        }
                    }
                }
            }

            // Import Smart Lists
            auto sl_path = directory / "smart_lists.json";
            if (std::filesystem::exists(sl_path)) {
                std::ifstream f(sl_path);
                if (f) {
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    rss_smart_lists_sync_dto sl_dto;
                    auto err = glz::read_json(sl_dto, content);
                    if (!err) {
                        // Get current local smart lists
                        std::set<std::string> existing_lists;
                        scan_smart_lists([&existing_lists](const std::string& title, const std::string& /*filter_json*/) {
                            existing_lists.insert(title);
                        });

                        std::vector<std::string> imported_titles;
                        for (const auto& sl : sl_dto.smart_lists) {
                            imported_titles.push_back(sl.title);
                            std::string filter_json = glz::write_json(sl.filter).value_or("");
                            if (!filter_json.empty()) {
                                save_smart_list(sl.title, filter_json);
                            }
                        }

                        // Deletions
                        for (const auto& title : existing_lists) {
                            if (std::find(imported_titles.begin(), imported_titles.end(), title) == imported_titles.end()) {
                                delete_smart_list(title);
                            }
                        }
                    }
                }
            }
        }

    private:
        hosting::db::sqlite db_;
        std::mutex mutex_;  // For thread-safe operations
    };
}
