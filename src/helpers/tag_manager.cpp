#include "tag_manager.hpp"
#include "platform_utils.hpp"
#include <sstream>

namespace rouen::helpers {

tag_manager& tag_manager::get() {
    static tag_manager instance;
    return instance;
}

tag_manager::tag_manager() {
    std::string db_path = rouen::platform::get_user_data_path("tags.db").string();
    db_ = std::make_unique<hosting::db::sqlite>(db_path);

    // Table 1: uri_tag - associates URIs with tags
    db_->ensure_table("uri_tag", 
        "uri TEXT NOT NULL, "
        "tag TEXT NOT NULL, "
        "PRIMARY KEY(uri, tag)");

    // Table 2: tag_definition - stores all unique tags and their order
    db_->ensure_table("tag_definition",
        "tag TEXT PRIMARY KEY, "
        "sort_order INTEGER DEFAULT 999");

    // Create index for performance
    db_->exec("CREATE INDEX IF NOT EXISTS idx_uri_tag_tag ON uri_tag(tag)");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_uri_tag_uri ON uri_tag(uri)");

    // Pre-populate default tags
    db_->exec(
        "INSERT OR IGNORE INTO tag_definition(tag, sort_order) VALUES "
        "('News', 10), "
        "('Tech / Dev', 20), "
        "('Podcasts', 30), "
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
}

void tag_manager::add_tag(const std::string& uri, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->exec("INSERT OR IGNORE INTO tag_definition (tag, sort_order) VALUES (?, ?)", [](sqlite3_stmt*){}, tag, 999);
    db_->exec("INSERT OR IGNORE INTO uri_tag (uri, tag) VALUES (?, ?)", [](sqlite3_stmt*){}, uri, tag);
}

void tag_manager::remove_tag(const std::string& uri, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->exec("DELETE FROM uri_tag WHERE uri = ? AND tag = ?", [](sqlite3_stmt*){}, uri, tag);
}

void tag_manager::remove_all_tags(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->exec("DELETE FROM uri_tag WHERE uri = ?", [](sqlite3_stmt*){}, uri);
}

std::set<std::string> tag_manager::get_tags(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> tags;
    db_->exec("SELECT tag FROM uri_tag WHERE uri = ?", [&](sqlite3_stmt* stmt) {
        const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (tag) {
            tags.insert(tag);
        }
    }, uri);
    return tags;
}

bool tag_manager::has_tag(const std::string& uri, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool found = false;
    db_->exec("SELECT 1 FROM uri_tag WHERE uri = ? AND tag = ? LIMIT 1", [&](sqlite3_stmt*) {
        found = true;
    }, uri, tag);
    return found;
}

std::vector<std::string> tag_manager::get_available_tags() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> tags;
    db_->exec("SELECT tag FROM tag_definition ORDER BY sort_order ASC, tag ASC", [&](sqlite3_stmt* stmt) {
        const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (tag) {
            tags.emplace_back(tag);
        }
    });
    return tags;
}

std::vector<std::string> tag_manager::get_uris_by_tag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> uris;
    db_->exec("SELECT uri FROM uri_tag WHERE tag = ?", [&](sqlite3_stmt* stmt) {
        const char* uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (uri) {
            uris.emplace_back(uri);
        }
    }, tag);
    return uris;
}

std::unordered_map<std::string, int> tag_manager::get_tag_counts() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, int> counts;
    db_->exec("SELECT tag, COUNT(uri) FROM uri_tag GROUP BY tag", [&](sqlite3_stmt* stmt) {
        const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int count = sqlite3_column_int(stmt, 1);
        if (tag) {
            counts[tag] = count;
        }
    });
    return counts;
}

void tag_manager::ensure_tag_defined(const std::string& tag, int sort_order) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->exec("INSERT OR IGNORE INTO tag_definition (tag, sort_order) VALUES (?, ?)", [](sqlite3_stmt*){}, tag, sort_order);
}

int tag_manager::delete_unused_tags() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Delete all tag definitions that do not have any URIs associated with them.
    int before = 0;
    db_->exec("SELECT COUNT(*) FROM tag_definition", [&](sqlite3_stmt* stmt) {
        before = sqlite3_column_int(stmt, 0);
    });

    db_->exec("DELETE FROM tag_definition WHERE tag NOT IN (SELECT DISTINCT tag FROM uri_tag)", [](sqlite3_stmt*){});

    int after = 0;
    db_->exec("SELECT COUNT(*) FROM tag_definition", [&](sqlite3_stmt* stmt) {
        after = sqlite3_column_int(stmt, 0);
    });

    return before - after;
}

} // namespace rouen::helpers
