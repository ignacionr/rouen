#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <format>
#include <optional>
#include <memory>
#include "../../helpers/sqlite.hpp"
#include "../../helpers/platform_utils.hpp"

namespace rouen::models::productivity {

    struct objective_record {
        int id{0};
        std::optional<int> parent_id;
        std::string period;            // "quarterly", "monthly", "weekly", "daily"
        std::string period_identifier; // e.g. "2026-Q3", "2026-07", "2026-W28", "2026-07-06"
        std::string title;
        std::string type;              // "binary", "volumetric", "constraint"
        double target_val{0.0};
        double current_val{0.0};
        std::string status;            // "pending", "committed", "completed", "failed", "dropped"
        std::string created_at;
        std::string updated_at;
    };

    struct date_context {
        std::string date;
        std::string week;
        std::string month;
        std::string quarter;
    };

    class objective_repository {
    public:
        explicit objective_repository(const std::string& db_path = rouen::platform::get_user_data_path("objectives.db").string())
            : db_(db_path) {
            ensure_schema();
        }

        static date_context get_date_context_for_time(std::time_t time) {
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            date_context ctx;
            
            // Format YYYY-MM-DD
            char buf_date[32];
            std::strftime(buf_date, sizeof(buf_date), "%Y-%m-%d", &tm);
            ctx.date = buf_date;

            // Format YYYY-Www (ISO week)
            char buf_week[32];
            std::strftime(buf_week, sizeof(buf_week), "%G-W%V", &tm);
            ctx.week = buf_week;

            // Format YYYY-MM
            char buf_month[32];
            std::strftime(buf_month, sizeof(buf_month), "%Y-%m", &tm);
            ctx.month = buf_month;

            // Format YYYY-Q#
            int quarter = (tm.tm_mon / 3) + 1;
            ctx.quarter = std::format("{}-Q{}", tm.tm_year + 1900, quarter);

            return ctx;
        }

        static date_context get_current_date_context() {
            auto now = std::chrono::system_clock::now();
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(now));
        }

        static date_context get_tomorrow_date_context() {
            auto now = std::chrono::system_clock::now();
            auto tomorrow = now + std::chrono::hours(24);
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(tomorrow));
        }

        static date_context get_yesterday_date_context() {
            auto now = std::chrono::system_clock::now();
            auto yesterday = now - std::chrono::hours(24);
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(yesterday));
        }

        std::vector<objective_record> get_objectives(const std::string& period, const std::string& identifier) {
            std::vector<objective_record> results;
            std::string sql = "SELECT id, parent_id, period, period_identifier, title, type, target_val, current_val, status, created_at, updated_at "
                              "FROM objective WHERE period = ? AND period_identifier = ?";
            
            db_.exec(sql, [&](sqlite3_stmt* stmt) {
                objective_record rec;
                rec.id = sqlite3_column_int(stmt, 0);
                if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                    rec.parent_id = sqlite3_column_int(stmt, 1);
                }
                rec.period = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                rec.period_identifier = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                rec.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                rec.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                rec.target_val = sqlite3_column_double(stmt, 6);
                rec.current_val = sqlite3_column_double(stmt, 7);
                rec.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                rec.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                rec.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
                results.push_back(rec);
            }, period, identifier);
            
            return results;
        }

        objective_record get_objective_by_id(int id) {
            objective_record rec;
            std::string sql = "SELECT id, parent_id, period, period_identifier, title, type, target_val, current_val, status, created_at, updated_at "
                              "FROM objective WHERE id = ?";
            
            bool found = false;
            db_.exec(sql, [&](sqlite3_stmt* stmt) {
                found = true;
                rec.id = sqlite3_column_int(stmt, 0);
                if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                    rec.parent_id = sqlite3_column_int(stmt, 1);
                }
                rec.period = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                rec.period_identifier = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                rec.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                rec.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                rec.target_val = sqlite3_column_double(stmt, 6);
                rec.current_val = sqlite3_column_double(stmt, 7);
                rec.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                rec.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                rec.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            }, id);
            
            if (!found) {
                rec.id = 0;
            }
            return rec;
        }

        int add_objective(const objective_record& rec) {
            std::string sql = "INSERT INTO objective (parent_id, period, period_identifier, title, type, target_val, current_val, status, created_at, updated_at) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now'))";
            
            std::optional<int> parent = rec.parent_id;
            
            if (parent.has_value()) {
                db_.exec(sql, {}, *parent, rec.period, rec.period_identifier, rec.title, rec.type, rec.target_val, rec.current_val, rec.status);
            } else {
                db_.exec(sql, {}, std::nullopt, rec.period, rec.period_identifier, rec.title, rec.type, rec.target_val, rec.current_val, rec.status);
            }

            int last_id = 0;
            db_.exec("SELECT last_insert_rowid()", [&](sqlite3_stmt* stmt) {
                last_id = sqlite3_column_int(stmt, 0);
            });
            return last_id;
        }

        void update_objective(const objective_record& rec) {
            std::string sql = "UPDATE objective SET parent_id = ?, title = ?, type = ?, target_val = ?, current_val = ?, status = ?, updated_at = datetime('now') "
                              "WHERE id = ?";
            
            std::optional<int> parent = rec.parent_id;
            if (parent.has_value()) {
                db_.exec(sql, {}, *parent, rec.title, rec.type, rec.target_val, rec.current_val, rec.status, rec.id);
            } else {
                db_.exec(sql, {}, std::nullopt, rec.title, rec.type, rec.target_val, rec.current_val, rec.status, rec.id);
            }
        }

        void delete_objective(int id) {
            // Delete children recursively (simplified cascade deletion)
            std::vector<int> child_ids;
            db_.exec("SELECT id FROM objective WHERE parent_id = ?", [&](sqlite3_stmt* stmt) {
                child_ids.push_back(sqlite3_column_int(stmt, 0));
            }, id);
            
            for (int child : child_ids) {
                delete_objective(child);
            }

            db_.exec("DELETE FROM objective WHERE id = ?", {}, id);
        }

        // Ledger States for Closing Days
        bool is_day_closed(const std::string& date) {
            std::string sql = "SELECT closed FROM ledger WHERE date = ?";
            int closed = 0;
            bool found = false;
            db_.exec(sql, [&](sqlite3_stmt* stmt) {
                found = true;
                closed = sqlite3_column_int(stmt, 0);
            }, date);
            return found && (closed != 0);
        }

        bool has_ledger_entry(const std::string& date) {
            std::string sql = "SELECT count(*) FROM ledger WHERE date = ?";
            int count = 0;
            db_.exec(sql, [&](sqlite3_stmt* stmt) {
                count = sqlite3_column_int(stmt, 0);
            }, date);
            return count > 0;
        }

        void initialize_day_ledger(const std::string& date) {
            if (!has_ledger_entry(date)) {
                db_.exec("INSERT INTO ledger (date, closed, closed_at) VALUES (?, 0, NULL)", {}, date);
            }
        }

        std::string get_unclosed_day_before(const std::string& current_date) {
            std::string sql = "SELECT date FROM ledger WHERE date < ? AND closed = 0 ORDER BY date DESC LIMIT 1";
            std::string unclosed_date = "";
            db_.exec(sql, [&](sqlite3_stmt* stmt) {
                unclosed_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            }, current_date);
            return unclosed_date;
        }

        void quick_zero_and_archive_day(const std::string& date) {
            // Update all active objectives of that day to failed/completed depending on values
            auto day_items = get_objectives("daily", date);
            for (auto& item : day_items) {
                bool is_success = false;
                if (item.type == "binary") {
                    is_success = (item.current_val >= 1.0);
                } else if (item.type == "volumetric") {
                    is_success = (item.current_val >= item.target_val);
                } else if (item.type == "constraint") {
                    is_success = (item.current_val <= item.target_val);
                }

                item.status = is_success ? "completed" : "failed";
                update_objective(item);
            }
            
            // Mark day as closed in ledger
            db_.exec("INSERT OR REPLACE INTO ledger (date, closed, closed_at) VALUES (?, 1, datetime('now'))", {}, date);
        }

        void commit_day_objectives(const std::string& date) {
            db_.exec("UPDATE objective SET status = 'committed' WHERE period = 'daily' AND period_identifier = ? AND status = 'pending'", {}, date);
            initialize_day_ledger(date);
        }

        void close_day(const std::string& date, const std::vector<std::pair<int, std::string>>& rollovers) {
            // 1. Update status of items on that day based on actual progress
            auto day_items = get_objectives("daily", date);
            for (auto& item : day_items) {
                bool is_success = false;
                if (item.type == "binary") {
                    is_success = (item.current_val >= 1.0);
                } else if (item.type == "volumetric") {
                    is_success = (item.current_val >= item.target_val);
                } else if (item.type == "constraint") {
                    is_success = (item.current_val <= item.target_val);
                }

                std::string final_status = is_success ? "completed" : "failed";
                
                // Find custom rollover option if provided and item is incomplete
                if (!is_success) {
                    for (const auto& [item_id, option] : rollovers) {
                        if (item_id == item.id) {
                            if (option == "push") {
                                // Create new pending objective for tomorrow
                                auto tomorrow_ctx = get_tomorrow_date_context();
                                objective_record tomorrow_item;
                                tomorrow_item.parent_id = item.parent_id;
                                tomorrow_item.period = "daily";
                                tomorrow_item.period_identifier = tomorrow_ctx.date;
                                tomorrow_item.title = item.title;
                                tomorrow_item.type = item.type;
                                tomorrow_item.target_val = item.target_val;
                                tomorrow_item.current_val = 0.0;
                                tomorrow_item.status = "pending";
                                add_objective(tomorrow_item);
                                
                                final_status = "dropped"; // Dropped on this day, rolled over to next
                            } else {
                                final_status = "dropped";
                            }
                            break;
                        }
                    }
                }

                item.status = final_status;
                update_objective(item);
            }

            // 2. Mark ledger as closed
            db_.exec("INSERT OR REPLACE INTO ledger (date, closed, closed_at) VALUES (?, 1, datetime('now'))", {}, date);
        }

    private:
        hosting::db::sqlite db_;

        void ensure_schema() {
            db_.ensure_table("objective",
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "parent_id INTEGER NULLABLE, "
                "period TEXT NOT NULL, "
                "period_identifier TEXT NOT NULL, "
                "title TEXT NOT NULL, "
                "type TEXT NOT NULL, "
                "target_val REAL NOT NULL, "
                "current_val REAL NOT NULL, "
                "status TEXT NOT NULL, "
                "created_at TEXT NOT NULL, "
                "updated_at TEXT NOT NULL, "
                "FOREIGN KEY(parent_id) REFERENCES objective(id) ON DELETE SET NULL"
            );

            db_.ensure_table("ledger",
                "date TEXT PRIMARY KEY, "
                "closed INTEGER NOT NULL, "
                "closed_at TEXT NULLABLE"
            );

            db_.exec("CREATE INDEX IF NOT EXISTS idx_objective_period_identifier ON objective(period, period_identifier)");
            db_.exec("CREATE INDEX IF NOT EXISTS idx_objective_parent ON objective(parent_id)");
        }
    };
}
