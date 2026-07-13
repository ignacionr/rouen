#pragma once

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/glaze_include.hpp"
#include "plan.hpp"

namespace media::travel {
    struct destination_dto {
        std::string name;
        std::string location;
        std::string notes;
        std::string arrival;
        std::string departure;
        std::string accommodation;
        double budget{0.0};
        bool completed{false};

        struct glaze {
            using T = destination_dto;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "location", &T::location,
                "notes", &T::notes,
                "arrival", &T::arrival,
                "departure", &T::departure,
                "accommodation", &T::accommodation,
                "budget", &T::budget,
                "completed", &T::completed
            );
        };
    };

    struct plan_dto {
        std::string title;
        std::string description;
        std::vector<destination_dto> destinations;
        std::string created;
        std::string start_date;
        std::string end_date;
        double total_budget{0.0};
        std::string status;

        struct glaze {
            using T = plan_dto;
            static constexpr auto value = glz::object(
                "title", &T::title,
                "description", &T::description,
                "destinations", &T::destinations,
                "created", &T::created,
                "start_date", &T::start_date,
                "end_date", &T::end_date,
                "total_budget", &T::total_budget,
                "status", &T::status
            );
        };
    };

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

        // Test query to validate SQLite connection
        void execute_test_query() {
            DB_INFO("SQLiteRepo: Executing test query");
            std::string sql = "SELECT 1";
            db_.exec(sql, [](sqlite3_stmt *stmt) {
                DB_INFO_FMT("SQLiteRepo: Test query result: {}", sqlite3_column_int(stmt, 0));
            });
            DB_INFO("SQLiteRepo: Test query completed successfully");
        }

        // Export all plans to a directory of JSON files
        void export_to_directory(const std::filesystem::path& directory) {
            std::filesystem::create_directories(directory);

            // Clean up existing files in the sync folder first to handle deletions
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::filesystem::remove(entry.path());
                }
            }

            std::vector<long long> plan_ids;
            scan_plans([&plan_ids](long long id, const char* /*title*/, const char* /*start*/, const char* /*end*/, const char* /*status*/) {
                plan_ids.push_back(id);
            });

            for (long long id : plan_ids) {
                plan p;
                if (get_plan(id, p)) {
                    plan_dto dto;
                    dto.title = p.title;
                    dto.description = p.description;
                    dto.created = time_point_to_string(p.created);
                    dto.start_date = time_point_to_string(p.start_date);
                    dto.end_date = time_point_to_string(p.end_date);
                    dto.total_budget = p.total_budget;
                    dto.status = plan::status_to_string(p.current_status);

                    for (const auto& dest : p.destinations) {
                        destination_dto d_dto;
                        d_dto.name = dest.name;
                        d_dto.location = dest.location;
                        d_dto.notes = dest.notes;
                        d_dto.arrival = time_point_to_string(dest.arrival);
                        d_dto.departure = time_point_to_string(dest.departure);
                        d_dto.accommodation = dest.accommodation;
                        d_dto.budget = dest.budget;
                        d_dto.completed = dest.completed;
                        dto.destinations.push_back(d_dto);
                    }

                    // Slugify the title to make a clean filename
                    std::string filename = "";
                    for (char c : p.title) {
                        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                            filename += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        } else if (c == ' ' || c == '-' || c == '_') {
                            if (!filename.empty() && filename.back() != '_') {
                                filename += '_';
                            }
                        }
                    }
                    if (filename.empty()) {
                        filename = std::format("plan_{}", id);
                    }
                    filename += ".json";

                    auto path = directory / filename;
                    std::string json_content = glz::write<glz::opts{.prettify = true}>(dto).value_or("");
                    if (!json_content.empty()) {
                        std::ofstream f(path);
                        if (f) {
                            f << json_content;
                        }
                    }
                }
            }
        }

        // Import plans from a directory of JSON files
        void import_from_directory(const std::filesystem::path& directory) {
            if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
                return;
            }

            // Find all plans in the local database to check for existence
            std::map<std::string, long long> existing_plans; // title -> id
            scan_plans([&existing_plans](long long id, const char* title, const char* /*start*/, const char* /*end*/, const char* /*status*/) {
                if (title) {
                    existing_plans[title] = id;
                }
            });

            // Set of imported plan titles to handle deletions
            std::vector<std::string> imported_titles;

            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                    continue;
                }

                std::ifstream f(entry.path());
                if (!f) continue;

                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                plan_dto dto;
                auto err = glz::read_json(dto, content);
                if (err) {
                    continue;
                }

                imported_titles.push_back(dto.title);

                // Convert DTO back to core plan model
                plan p;
                p.title = dto.title;
                p.description = dto.description;
                p.created = string_to_time_point(dto.created);
                p.start_date = string_to_time_point(dto.start_date);
                p.end_date = string_to_time_point(dto.end_date);
                p.total_budget = dto.total_budget;
                
                auto status_opt = plan::string_to_status(dto.status);
                p.current_status = status_opt.value_or(plan::status::planning);

                for (const auto& d_dto : dto.destinations) {
                    destination dest;
                    dest.name = d_dto.name;
                    dest.location = d_dto.location;
                    dest.notes = d_dto.notes;
                    dest.arrival = string_to_time_point(d_dto.arrival);
                    dest.departure = string_to_time_point(d_dto.departure);
                    dest.accommodation = d_dto.accommodation;
                    dest.budget = d_dto.budget;
                    dest.completed = d_dto.completed;
                    p.destinations.push_back(dest);
                }

                // Check if this plan already exists
                auto it = existing_plans.find(p.title);
                if (it != existing_plans.end()) {
                    p.id = it->second;
                } else {
                    p.id = -1;
                }

                upsert_plan(p);
            }

            // Sync deletion: if a plan is in existing_plans but not in imported_titles, it was deleted on another device
            for (const auto& [title, id] : existing_plans) {
                if (std::find(imported_titles.begin(), imported_titles.end(), title) == imported_titles.end()) {
                    delete_plan(id);
                }
            }
        }

    private:
        hosting::db::sqlite db_;
    };
}
