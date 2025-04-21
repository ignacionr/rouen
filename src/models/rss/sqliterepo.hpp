#pragma once

#include <string>

#include "../../helpers/sqlite.hpp"

namespace media::rss
{
    struct sqliterepo
    {
        sqliterepo(const std::string &path) : db_{path}
        {
            db_.ensure_table("feed", "id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT UNIQUE, title TEXT, image_url TEXT, last_updated TEXT");
            db_.ensure_table("item", "link TEXT PRIMARY KEY, enclosure TEXT, feed_id INTEGER, title TEXT, description TEXT, pub_date TEXT, image_url TEXT");
        }

        long long upsert_feed(std::string_view url, std::string_view title, std::string_view image_url)
        {
            std::string sql = "INSERT INTO feed (url, title, image_url, last_updated) "
                              "VALUES (?, ?, ?, datetime('now')) "
                              "ON CONFLICT(url) DO "
                              "UPDATE SET title=excluded.title, image_url=excluded.image_url, last_updated=datetime('now') "
                              "RETURNING id";
            long long result {-1};
            db_.exec(sql, [&result](sqlite3_stmt *stmt) {
                result = sqlite3_column_int64(stmt, 0);
            }, url, title, image_url);
            if (result == -1) {
                sql = "SELECT id FROM feed WHERE url = ?";
                db_.exec(sql, [&result](sqlite3_stmt *stmt) {
                    result = sqlite3_column_int64(stmt, 0);
                }, url);
            }
            return result;
        }

        void upsert_item(long long feed_id, std::string_view title, std::string_view enclosure, std::string_view link, std::string_view description, std::string_view pub_date, std::string_view image_url)
        {
            std::string sql = "INSERT OR REPLACE INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) VALUES (?, ?, ?, ?, ?, ?, ?)";
            db_.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url);
        }

        void update_feed(std::string_view url, std::string_view title, std::string_view image_url)
        {
            std::string sql = "UPDATE feed SET title = ?, image_url = ?, last_updated = datetime('now') WHERE url = ?";
            db_.exec(sql, {}, title, image_url, url);
        }

        void delete_feed(std::string_view url)
        {
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
        }

        void scan_feeds(auto sink)
        {
            std::string sql = "SELECT id, url, title, image_url FROM feed";
            db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                sink(sqlite3_column_int64(stmt, 0), 
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)), 
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)), 
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
            });
        }

        void scan_items(long long feed_id, auto sink)
        {
            std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url FROM item WHERE feed_id = ? ORDER BY pub_date DESC";
            db_.exec(sql, [&sink](sqlite3_stmt *stmt) {
                sink(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)), 
                     reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)), 
                     reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)), 
                     reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)), 
                     reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4)),
                     reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5))
                     );
                return 0;
            }, feed_id);
        }

    private:
        hosting::db::sqlite db_;
    };
}