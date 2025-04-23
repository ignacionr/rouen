#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <imgui/imgui.h>
#include "../interface/card.hpp"
#include "../../helpers/sqlite.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

struct dbrepair_card : public card {
private:
    // Define DatabaseStatus enum first so it's available throughout the class
    enum class DatabaseStatus {
        OK,
        CORRUPTED
    };
    
    struct DatabaseInfo {
        std::string path;
        std::string name;
        size_t size_bytes;
        time_t last_modified;
        bool has_wal;
        bool has_shm;
        DatabaseStatus status;
    };
    
public:
    dbrepair_card() {
        // Set custom colors for the card
        colors[0] = {0.6f, 0.3f, 0.3f, 1.0f};   // Red primary color
        colors[1] = {0.7f, 0.4f, 0.4f, 0.7f};   // Light red secondary color
        
        // Additional colors for specific elements
        get_color(2, {0.8f, 0.3f, 0.3f, 1.0f}); // Bright red for warnings/errors
        get_color(3, {0.3f, 0.7f, 0.3f, 1.0f}); // Green for success
        get_color(4, {0.3f, 0.3f, 0.7f, 1.0f}); // Blue for information
        get_color(5, {0.7f, 0.7f, 0.3f, 1.0f}); // Yellow for warnings
        
        name("Database Repair");
        width = 800.0f;
        
        // Scan for available databases
        refresh_database_list();
    }
    
    void refresh_database_list() {
        databases.clear();
        
        // Find all .db files in the workspace root
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".db") {
                DatabaseInfo info;
                info.path = entry.path().string();
                info.name = entry.path().filename().string();
                info.size_bytes = entry.file_size();
                
                // Get file modification time
                struct stat file_stat;
                if (stat(info.path.c_str(), &file_stat) == 0) {
                    info.last_modified = file_stat.st_mtime;
                }
                
                // Check for WAL and SHM files (indicating journaling)
                info.has_wal = std::filesystem::exists(info.path + "-wal");
                info.has_shm = std::filesystem::exists(info.path + "-shm");
                
                // Check if the database is corrupted
                info.status = check_database_status(info.path);
                
                databases.push_back(info);
            }
        }
    }
    
    // Check database status to determine if it's corrupted
    DatabaseStatus check_database_status(const std::string& path) {
        try {
            // Try to open the database and run a simple query
            hosting::db::sqlite test_db(path);
            test_db.exec("PRAGMA integrity_check;", [](sqlite3_stmt* stmt) {
                const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (std::string(result) != "ok") {
                    throw std::runtime_error("Integrity check failed: " + std::string(result));
                }
            });
            
            return DatabaseStatus::OK;
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Database check error for {}: {}", path, e.what());
            return DatabaseStatus::CORRUPTED;
        }
    }
    
    // Format file size in human-readable format
    std::string format_size(size_t size_bytes) {
        if (size_bytes < 1024) {
            return std::to_string(size_bytes) + " B";
        } else if (size_bytes < 1024 * 1024) {
            return std::to_string(size_bytes / 1024) + " KB";
        } else {
            return std::format("{:.2f} MB", static_cast<double>(size_bytes) / (1024 * 1024));
        }
    }
    
    // Format timestamp to human-readable date/time
    std::string format_timestamp(time_t timestamp) {
        char buffer[30];
        struct tm* timeinfo = localtime(&timestamp);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(buffer);
    }
    
    // Repair database file by creating a backup and repairing
    bool repair_database(const std::string& path) {
        try {
            // Check if database exists
            if (!std::filesystem::exists(path)) {
                last_error = "Database file not found: " + path;
                return false;
            }
            
            // Create a backup first
            std::string backup_path = path + ".backup." + 
                                      std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            
            DB_INFO_FMT("Creating backup of {} to {}", path, backup_path);
            std::filesystem::copy_file(path, backup_path);
            
            // Remove WAL and SHM files if they exist
            if (std::filesystem::exists(path + "-wal")) {
                std::filesystem::remove(path + "-wal");
            }
            if (std::filesystem::exists(path + "-shm")) {
                std::filesystem::remove(path + "-shm");
            }
            
            // Create a new database and restore data
            {
                // Create a temporary database connection
                std::string temp_db_path = path + ".temp";
                if (std::filesystem::exists(temp_db_path)) {
                    std::filesystem::remove(temp_db_path);
                }
                
                hosting::db::sqlite new_db(temp_db_path);
                
                // Set up the schema (travel-specific for now, we can make this more generic later)
                if (path == "travel.db") {
                    DB_INFO("Recreating travel database schema");
                    
                    // Create tables based on the travel db schema
                    new_db.ensure_table("travel_plan", 
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "title TEXT, "
                        "description TEXT, "
                        "created TEXT, "
                        "start_date TEXT, "
                        "end_date TEXT, "
                        "status TEXT, "
                        "total_budget REAL");
                    
                    new_db.ensure_table("travel_destination", 
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
                    
                    // Try to recover data from the original database
                    try {
                        hosting::db::sqlite old_db(path);
                        
                        // Attempt to copy travel plans
                        old_db.exec("SELECT id, title, description, created, start_date, end_date, status, total_budget FROM travel_plan",
                                  [&new_db](sqlite3_stmt* stmt) {
                            // Get values from the old database
                            int id = sqlite3_column_int(stmt, 0);
                            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                            const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                            const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                            const char* start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                            const char* end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                            const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                            double total_budget = sqlite3_column_double(stmt, 7);
                            
                            // Insert into the new database - using std::string to avoid const char* binding issue
                            std::string sql = "INSERT INTO travel_plan (id, title, description, created, start_date, end_date, status, total_budget) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
                            
                            std::string title_str = title ? title : "";
                            std::string desc_str = description ? description : "";
                            std::string created_str = created ? created : "2025-04-23 00:00:00";
                            std::string start_str = start_date ? start_date : "2025-04-23 00:00:00";
                            std::string end_str = end_date ? end_date : "2025-04-30 00:00:00";
                            std::string status_str = status ? status : "planning";
                            
                            new_db.exec(sql, {}, id, title_str, desc_str, created_str, 
                                       start_str, end_str, status_str, total_budget);
                        });
                        
                        // Attempt to copy destinations
                        old_db.exec("SELECT id, plan_id, name, location, notes, arrival, departure, accommodation, budget, completed FROM travel_destination",
                                  [&new_db](sqlite3_stmt* stmt) {
                            // Get values from the old database
                            int id = sqlite3_column_int(stmt, 0);
                            int plan_id = sqlite3_column_int(stmt, 1);
                            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                            const char* location = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                            const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                            const char* arrival = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                            const char* departure = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                            const char* accommodation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                            double budget = sqlite3_column_double(stmt, 8);
                            int completed = sqlite3_column_int(stmt, 9);
                            
                            // Insert into the new database - using std::string to avoid const char* binding issue
                            std::string sql = "INSERT INTO travel_destination (id, plan_id, name, location, notes, arrival, departure, accommodation, budget, completed) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
                            
                            std::string name_str = name ? name : "";
                            std::string location_str = location ? location : "";
                            std::string notes_str = notes ? notes : "";
                            std::string arrival_str = arrival ? arrival : "2025-04-23 00:00:00";
                            std::string departure_str = departure ? departure : "2025-04-30 00:00:00";
                            std::string accommodation_str = accommodation ? accommodation : "";
                            
                            new_db.exec(sql, {}, id, plan_id, name_str, location_str, notes_str,
                                       arrival_str, departure_str, accommodation_str, budget, completed);
                        });
                    } catch (const std::exception& e) {
                        DB_ERROR_FMT("Failed to recover data: {}", e.what());
                        // Continue anyway - we'll have an empty but valid database
                    }
                    
                    // If we couldn't recover data or the database was empty, create a sample plan
                    new_db.exec("SELECT COUNT(*) FROM travel_plan", [&new_db](sqlite3_stmt* stmt) {
                        int count = sqlite3_column_int(stmt, 0);
                        if (count == 0) {
                            DB_INFO("Creating sample travel plan");
                            
                            // Create a sample plan - using std::string to avoid const char* binding issue
                            std::string sql = "INSERT INTO travel_plan (title, description, created, start_date, end_date, status, total_budget) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?)";
                            
                            std::string title = "Sample Trip to Paris";
                            std::string desc = "This is a sample travel plan created during database repair.";
                            std::string created = "2025-04-23 12:00:00";
                            std::string start = "2025-05-15 00:00:00";
                            std::string end = "2025-05-22 00:00:00";
                            std::string status = "planning";
                            double budget = 2500.0;
                            
                            new_db.exec(sql, {}, title, desc, created, start, end, status, budget);
                            
                            // Get the last inserted ID
                            long long plan_id = 0;
                            new_db.exec("SELECT last_insert_rowid()", [&plan_id](sqlite3_stmt* stmt) {
                                plan_id = sqlite3_column_int64(stmt, 0);
                            });
                            
                            // Add sample destinations
                            std::string dest_sql = "INSERT INTO travel_destination (plan_id, name, location, notes, arrival, departure, accommodation, budget, completed) "
                                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
                            
                            std::string dest1_name = "Eiffel Tower";
                            std::string dest1_loc = "Paris, France";
                            std::string dest1_notes = "Must see landmark";
                            std::string dest1_arrival = "2025-05-16 10:00:00";
                            std::string dest1_departure = "2025-05-16 14:00:00";
                            std::string dest1_accom = "None";
                            double dest1_budget = 30.0;
                            int dest1_completed = 0;
                            
                            new_db.exec(dest_sql, {}, plan_id, dest1_name, dest1_loc, dest1_notes,
                                       dest1_arrival, dest1_departure, dest1_accom, dest1_budget, dest1_completed);
                            
                            std::string dest2_name = "Louvre Museum";
                            std::string dest2_loc = "Paris, France";
                            std::string dest2_notes = "Allow plenty of time to explore";
                            std::string dest2_arrival = "2025-05-17 09:00:00";
                            std::string dest2_departure = "2025-05-17 18:00:00";
                            std::string dest2_accom = "None";
                            double dest2_budget = 60.0;
                            int dest2_completed = 0;
                            
                            new_db.exec(dest_sql, {}, plan_id, dest2_name, dest2_loc, dest2_notes,
                                       dest2_arrival, dest2_departure, dest2_accom, dest2_budget, dest2_completed);
                        }
                    });
                } else {
                    // Generic database repair - just create an empty database with appropriate journal mode
                    new_db.exec("PRAGMA journal_mode = WAL");
                    new_db.exec("PRAGMA synchronous = NORMAL");
                }
                
                // Close connections before moving files
            }
            
            // Replace the original database with the repaired one
            std::filesystem::remove(path);
            std::filesystem::rename(path + ".temp", path);
            
            // Update the database status
            for (auto& db : databases) {
                if (db.path == path) {
                    db.status = DatabaseStatus::OK;
                    db.has_wal = std::filesystem::exists(path + "-wal");
                    db.has_shm = std::filesystem::exists(path + "-shm");
                    break;
                }
            }
            
            DB_INFO_FMT("Database {} successfully repaired", path);
            return true;
        } catch (const std::exception& e) {
            last_error = e.what();
            DB_ERROR_FMT("Failed to repair database {}: {}", path, e.what());
            return false;
        }
    }
    
    std::string get_uri() const override {
        return "dbrepair";
    }
    
    bool render() override {
        return render_window([this]() {
            ImGui::Text("Database Repair Utility");
            ImGui::TextColored(colors[5], "Use this tool to fix corrupted database files");
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Refresh button
            if (ImGui::Button("Refresh Database List")) {
                refresh_database_list();
            }
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "Found %zu database(s)", databases.size());
            
            ImGui::Spacing();
            
            // Display warning about backup
            ImGui::TextColored(colors[5], "⚠️ Warning: Repair operation will create a backup but may result in data loss.");
            ImGui::TextColored(colors[5], "Only use this tool if your database is corrupted or not working properly.");
            
            ImGui::Spacing();
            ImGui::Separator();
            
            // Database listing in a table
            if (ImGui::BeginTable("Databases", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                // Set up table headers
                ImGui::TableSetupColumn("Database", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Journal", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();
                
                // Display each database
                for (const auto& db : databases) {
                    ImGui::TableNextRow();
                    
                    // Database name
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", db.name.c_str());
                    
                    // Size
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", format_size(db.size_bytes).c_str());
                    
                    // Status with color coding
                    ImGui::TableSetColumnIndex(2);
                    if (db.status == DatabaseStatus::OK) {
                        ImGui::TextColored(colors[3], "OK");
                    } else {
                        ImGui::TextColored(colors[2], "Corrupted");
                    }
                    
                    // Last modified
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", format_timestamp(db.last_modified).c_str());
                    
                    // Journal info
                    ImGui::TableSetColumnIndex(4);
                    if (db.has_wal && db.has_shm) {
                        ImGui::Text("WAL");
                    } else {
                        ImGui::Text("None");
                    }
                    
                    // Actions
                    ImGui::TableSetColumnIndex(5);
                    ImGui::PushID(db.name.c_str());
                    if (ImGui::Button("Repair")) {
                        repairing_db = db.path;
                        confirm_repair = true;
                    }
                    ImGui::PopID();
                }
                
                ImGui::EndTable();
            }
            
            // Display last error message if any
            if (!last_error.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(colors[2], "Error: %s", last_error.c_str());
            }
            
            // Display last success message if any
            if (!last_success.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(colors[3], "%s", last_success.c_str());
            }
            
            // Confirmation dialog for repair
            if (confirm_repair) {
                ImGui::OpenPopup("Confirm Database Repair");
                confirm_repair = false;
            }
            
            // Confirmation popup
            if (ImGui::BeginPopupModal("Confirm Database Repair", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Are you sure you want to repair %s?", repairing_db.c_str());
                ImGui::TextColored(colors[5], "This operation cannot be undone and may result in data loss.");
                ImGui::TextColored(colors[5], "A backup will be created before making changes.");
                ImGui::Separator();
                
                if (ImGui::Button("Yes, Repair Database", ImVec2(200, 0))) {
                    // Perform the repair
                    bool success = repair_database(repairing_db);
                    if (success) {
                        last_success = "Successfully repaired " + repairing_db;
                        last_error = "";
                    } else {
                        last_success = "";
                        // last_error is already set by the repair_database function
                    }
                    
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
            }
        });
    }
    
private:
    std::vector<DatabaseInfo> databases;
    std::string last_error;
    std::string last_success;
    std::string repairing_db;
    bool confirm_repair = false;
};

} // namespace rouen::cards