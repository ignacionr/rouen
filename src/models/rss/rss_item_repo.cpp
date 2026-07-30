#include "rss_item_repo.hpp"
#include "../../helpers/sqlite.hpp"
#include "../../helpers/debug.hpp"
#include <exception>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace media::rss {
    rss_item_repo::rss_item_repo(void* db_ptr, std::mutex* mtx) 
        : db_ptr_(db_ptr), mutex_(mtx) {}
        
    void rss_item_repo::batch_upsert_items(long long feed_id, const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>>& items) {
        std::lock_guard<std::mutex> lock(*mutex_); // Thread safety
        
        hosting::db::sqlite& db = *static_cast<hosting::db::sqlite*>(db_ptr_);
        
        RSS_DEBUG_FMT("batch_upsert_items starting for feed_id={}, item count={}", feed_id, items.size());
        if (items.empty()) return;
        
        try {
            // Begin transaction for batch operations
            db.exec("BEGIN TRANSACTION");
            
            for (const auto& [title, enclosure, link, description, pub_date, image_url] : items) {
                std::string sql = "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?) "
                                "ON CONFLICT(feed_id, link, title) DO "
                                "UPDATE SET enclosure=excluded.enclosure, "
                                "description=excluded.description, pub_date=excluded.pub_date, image_url=excluded.image_url";
                
                db.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url);
            }
            
            // Commit the transaction
            db.exec("COMMIT");
            RSS_DEBUG_FMT("batch_upsert_items completed for feed_id={}", feed_id);
            
        } catch (const std::exception& e) {
            // Rollback on error
            db.exec("ROLLBACK");
            RSS_ERROR_FMT("Error in batch_upsert_items: {}", e.what());
        }
    }

    void rss_item_repo::upsert_item(long long feed_id, std::string_view title, std::string_view enclosure, 
                         std::string_view link, std::string_view description, 
                         std::string_view pub_date, std::string_view image_url) {
        std::lock_guard<std::mutex> lock(*mutex_); // Thread safety
        
        hosting::db::sqlite& db = *static_cast<hosting::db::sqlite*>(db_ptr_);
        
        RSS_DEBUG_FMT("upsert_item starting for feed_id={}, link={}", feed_id, link);
        
        try {
            std::string sql = "INSERT INTO item (link, enclosure, feed_id, title, description, pub_date, image_url) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?) "
                          "ON CONFLICT(feed_id, link, title) DO "
                          "UPDATE SET enclosure=excluded.enclosure, "
                          "description=excluded.description, pub_date=excluded.pub_date, image_url=excluded.image_url";
            
            db.exec(sql, {}, link, enclosure, feed_id, title, description, pub_date, image_url);
            RSS_DEBUG_FMT("upsert_item completed for feed_id={}, link={}", feed_id, link);
        }
        catch (const std::exception& e) {
            RSS_ERROR_FMT("Error in upsert_item: {}", e.what());
            throw;
        }
    }

    template<typename Sink>
    void rss_item_repo::scan_items(long long feed_id, Sink sink) {
        std::lock_guard<std::mutex> lock(*mutex_); // Thread safety
        
        hosting::db::sqlite& db = *static_cast<hosting::db::sqlite*>(db_ptr_);
        
        try {
            std::string sql = "SELECT link, enclosure, title, description, pub_date, image_url FROM item WHERE feed_id = ? ORDER BY pub_date DESC";
            db.exec(sql, [&sink](sqlite3_stmt *stmt) {
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
}
