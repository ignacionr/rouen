#pragma once

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "../../helpers/sqlite.hpp"
#include "plan.hpp"

namespace media::travel {
    class sqliterepo {
    public:
        sqliterepo(const std::string &path) : db_{path} {
            // Create tables if they don't exist
            db_.ensure_table("travel_plan", 
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "title TEXT, "
                "description TEXT, "
                "created TEXT, "
                "start_date TEXT, "
                "end_date TEXT, "
                "status TEXT, "
                "total_budget REAL");
            
            db_.ensure_table("travel_destination", 
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "plan_id INTEGER, "
                "name TEXT, "
                "location TEXT, "
                "notes TEXT, "
                "arrival TEXT, "
                "departure TEXT, "
                "accommodation TEXT, "
                "budget REAL, "
                "completed INTEGER, "
                "FOREIGN KEY(plan_id) REFERENCES travel_plan(id) ON DELETE CASCADE");
        }

        // Convert time_point to SQLite-compatible string
        std::string time_point_to_string(const std::chrono::system_clock::time_point& tp) {
            auto time = std::chrono::system_clock::to_time_t(tp);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            return ss.str();
        }

        // Convert SQLite-stored string to time_point
        std::chrono::system_clock::time_point string_to_time_point(const std::string& str) {
            std::tm tm = {};
            std::stringstream ss(str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        // Create or update a travel plan
        long long upsert_plan(plan& p) {
            if (p.id == -1) {
                // New plan
                std::string sql = "INSERT INTO travel_plan "
                                "(title, description, created, start_date, end_date, status, total_budget) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?)";
                
                db_.exec(sql, {}, 
                        p.title, 
                        p.description, 
                        time_point_to_string(p.created.time_since_epoch().count() > 0 ? p.created : std::chrono::system_clock::now()),
                        time_point_to_string(p.start_date), 
                        time_point_to_string(p.end_date),
                        plan::status_to_string(p.current_status),
                        p.total_budget);
                
                // Get the last inserted ID
                sql = "SELECT last_insert_rowid()";
                db_.exec(sql, [&p](sqlite3_stmt *stmt) {
                    p.id = sqlite3_column_int64(stmt, 0);
                });
            } else {
                // Update existing plan
                std::string sql = "UPDATE travel_plan SET "
                                "title = ?, description = ?, start_date = ?, "
                                "end_date = ?, status = ?, total_budget = ? "
                                "WHERE id = ?";
                
                db_.exec(sql, {}, 
                        p.title, 
                        p.description, 
                        time_point_to_string(p.start_date), 
                        time_point_to_string(p.end_date),
                        plan::status_to_string(p.current_status),
                        p.total_budget,
                        p.id);
            }
            
            // Now handle destinations
            // First delete all existing destinations for this plan
            if (p.id != -1) {
                std::string sql = "DELETE FROM travel_destination WHERE plan_id = ?";
                db_.exec(sql, {}, p.id);
            }
            
            // Insert all destinations
            for (auto& dest : p.destinations) {
                std::string sql = "INSERT INTO travel_destination "
                                "(plan_id, name, location, notes, arrival, departure, "
                                "accommodation, budget, completed) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
                
                db_.exec(sql, {}, 
                        p.id, 
                        dest.name, 
                        dest.location, 
                        dest.notes,
                        time_point_to_string(dest.arrival), 
                        time_point_to_string(dest.departure),
                        dest.accommodation,
                        dest.budget,
                        dest.completed ? 1 : 0);
            }
            
            return p.id;
        }

        // Delete a travel plan and all its destinations
        void delete_plan(long long id) {
            // SQLite will cascade delete the destinations
            std::string sql = "DELETE FROM travel_plan WHERE id = ?";
            db_.exec(sql, {}, id);
        }

        // Get a single travel plan by ID
        bool get_plan(long long id, plan& p) {
            bool found = false;
            
            // First get the plan details
            std::string sql = "SELECT id, title, description, created, "
                            "start_date, end_date, status, total_budget "
                            "FROM travel_plan WHERE id = ?";
            
            db_.exec(sql, [&p, &found, this](sqlite3_stmt *stmt) {
                p.id = sqlite3_column_int64(stmt, 0);
                p.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                p.created = string_to_time_point(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
                p.start_date = string_to_time_point(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
                p.end_date = string_to_time_point(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
                
                auto status_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                auto status_opt = plan::string_to_status(status_str);
                p.current_status = status_opt.value_or(plan::status::planning);
                
                p.total_budget = sqlite3_column_double(stmt, 7);
                found = true;
            }, id);
            
            if (!found) {
                return false;
            }
            
            // Now get all destinations for this plan
            sql = "SELECT name, location, notes, arrival, departure, "
                "accommodation, budget, completed FROM travel_destination "
                "WHERE plan_id = ? ORDER BY arrival";
            
            p.destinations.clear();
            db_.exec(sql, [&p, this](sqlite3_stmt *stmt) {
                destination dest;
                dest.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                dest.location = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                dest.notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                dest.arrival = string_to_time_point(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
                dest.departure = string_to_time_point(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
                dest.accommodation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                dest.budget = sqlite3_column_double(stmt, 6);
                dest.completed = sqlite3_column_int(stmt, 7) != 0;
                p.destinations.push_back(dest);
            }, id);
            
            return true;
        }

        // Scan all travel plans
        void scan_plans(auto sink) {
            std::string sql = "SELECT id, title, start_date, end_date, status FROM travel_plan ORDER BY start_date DESC";
            
            db_.exec(sql, [sink](sqlite3_stmt *stmt) {
                long long id = sqlite3_column_int64(stmt, 0);
                const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                const char* end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                
                sink(id, title, start_date, end_date, status);
            });
        }

    private:
        hosting::db::sqlite db_;
    };
}