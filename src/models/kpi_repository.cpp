#include "kpi_repository.hpp"
#include "../helpers/glaze_include.hpp"
#include <format>
#include <chrono>
#include <algorithm>

namespace rouen::models {

kpi_repository::kpi_repository(const std::string& db_path) 
    : db_path_(db_path) {
    KPI_INFO_FMT("Initializing KPI repository with database: {}", db_path);
    
    try {
        db_ = std::make_unique<hosting::db::sqlite>(db_path);
        initialize_database();
        KPI_INFO("KPI repository initialized successfully");
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Failed to initialize KPI repository: {}", e.what());
        throw;
    }
}

kpi_repository::~kpi_repository() {
    KPI_INFO("KPI repository destructor called");
}

void kpi_repository::initialize_database() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    create_tables();
}

void kpi_repository::create_tables() {
    KPI_DEBUG("Creating KPI database tables");
    
    // Create categories table
    db_->ensure_table("kpi_categories", 
        "id TEXT PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "color TEXT, "
        "icon TEXT, "
        "sort_order INTEGER DEFAULT 0, "
        "created_at TEXT DEFAULT (datetime('now')), "
        "updated_at TEXT DEFAULT (datetime('now'))"
    );
    
    // Create KPIs table
    db_->ensure_table("kpis", 
        "id TEXT PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "value_type INTEGER NOT NULL, "
        "category_id TEXT, "
        "parent_id TEXT, "
        "current_value REAL DEFAULT 0, "
        "unit TEXT, "
        "last_updated TEXT, "
        "history TEXT, "  // JSON serialized kpi_data_point array
        "target TEXT, "   // JSON serialized kpi_target
        "aggregation_type INTEGER DEFAULT 0, "
        "child_ids TEXT, "  // JSON serialized string array
        "weights TEXT, "    // JSON serialized string->double map
        "owner TEXT, "
        "reporting_period INTEGER DEFAULT 0, "
        "is_active INTEGER DEFAULT 1, "
        "sort_order INTEGER DEFAULT 0, "
        "tags TEXT, "       // JSON serialized string->string map
        "created_at TEXT DEFAULT (datetime('now')), "
        "updated_at TEXT DEFAULT (datetime('now')), "
        "FOREIGN KEY(category_id) REFERENCES kpi_categories(id) ON DELETE SET NULL, "
        "FOREIGN KEY(parent_id) REFERENCES kpis(id) ON DELETE SET NULL"
    );
    
    // Create dashboards table
    db_->ensure_table("kpi_dashboards", 
        "id TEXT PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "kpi_ids TEXT, "    // JSON serialized string array
        "layout TEXT, "     // JSON layout configuration
        "owner TEXT, "
        "is_default INTEGER DEFAULT 0, "
        "created_at TEXT DEFAULT (datetime('now')), "
        "updated_at TEXT DEFAULT (datetime('now'))"
    );
    
    // Create reports table
    db_->ensure_table("kpi_reports", 
        "id TEXT PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "kpi_ids TEXT, "       // JSON serialized string array
        "category_ids TEXT, "  // JSON serialized string array
        "period INTEGER NOT NULL, "
        "start_date TEXT, "
        "end_date TEXT, "
        "template_config TEXT, "  // JSON configuration
        "auto_generate INTEGER DEFAULT 0, "
        "schedule TEXT, "
        "created_at TEXT DEFAULT (datetime('now')), "
        "updated_at TEXT DEFAULT (datetime('now'))"
    );
    
    // Create indexes for better performance
    try {
        db_->exec("CREATE INDEX IF NOT EXISTS idx_kpis_category ON kpis(category_id)");
        db_->exec("CREATE INDEX IF NOT EXISTS idx_kpis_parent ON kpis(parent_id)");
        db_->exec("CREATE INDEX IF NOT EXISTS idx_kpis_active ON kpis(is_active)");
        db_->exec("CREATE INDEX IF NOT EXISTS idx_kpis_last_updated ON kpis(last_updated)");
        KPI_DEBUG("KPI database indexes created successfully");
    } catch (const std::exception& e) {
        KPI_WARN_FMT("Warning creating indexes: {}", e.what());
    }
}

// Category operations
std::vector<kpi_category> kpi_repository::get_categories() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi_category> categories;
    
    try {
        std::string sql = "SELECT id, name, description, color, icon, sort_order "
                          "FROM kpi_categories ORDER BY sort_order, name";
        
        db_->exec(sql, [&categories](sqlite3_stmt* stmt) {
            kpi_category category;
            category.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            category.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            category.description = desc ? desc : "";
            
            const char* color = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            category.color = color ? color : "#007ACC";
            
            const char* icon = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            category.icon = icon ? icon : "chart-line";
            
            category.sort_order = sqlite3_column_int(stmt, 5);
            
            categories.push_back(category);
        });
        
        KPI_DEBUG_FMT("Retrieved {} categories", categories.size());
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving categories: {}", e.what());
    }
    
    return categories;
}

bool kpi_repository::create_category(const kpi_category& category) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "INSERT INTO kpi_categories "
                          "(id, name, description, color, icon, sort_order) "
                          "VALUES (?, ?, ?, ?, ?, ?)";
        
        db_->exec(sql, {}, 
            category.id,
            category.name,
            category.description,
            category.color,
            category.icon,
            category.sort_order);
        
        KPI_INFO_FMT("Created category: {}", category.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error creating category: {}", e.what());
        return false;
    }
}

bool kpi_repository::update_category(const kpi_category& category) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "UPDATE kpi_categories SET "
                          "name = ?, description = ?, color = ?, icon = ?, "
                          "sort_order = ?, updated_at = datetime('now') "
                          "WHERE id = ?";
        
        db_->exec(sql, {}, 
            category.name,
            category.description,
            category.color,
            category.icon,
            category.sort_order,
            category.id);
        
        KPI_INFO_FMT("Updated category: {}", category.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating category: {}", e.what());
        return false;
    }
}

bool kpi_repository::delete_category(const std::string& category_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "DELETE FROM kpi_categories WHERE id = ?";
        db_->exec(sql, {}, category_id);
        
        KPI_INFO_FMT("Deleted category: {}", category_id);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deleting category: {}", e.what());
        return false;
    }
}

// KPI operations
std::vector<kpi> kpi_repository::get_kpis(const std::string& category_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi> kpis;
    
    try {
        std::string sql;
        if (category_id.empty()) {
            sql = "SELECT * FROM kpis WHERE is_active = 1 ORDER BY sort_order, name";
        } else {
            sql = "SELECT * FROM kpis WHERE category_id = ? AND is_active = 1 ORDER BY sort_order, name";
        }
        
        if (category_id.empty()) {
            db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
                kpis.push_back(construct_kpi_from_stmt(stmt));
            });
        } else {
            db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
                kpis.push_back(construct_kpi_from_stmt(stmt));
            }, category_id);
        }
        
        KPI_DEBUG_FMT("Retrieved {} KPIs for category: {}", kpis.size(), category_id);
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving KPIs: {}", e.what());
    }
    
    return kpis;
}

std::optional<kpi> kpi_repository::get_kpi(const std::string& kpi_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::optional<kpi> result;
    
    try {
        std::string sql = "SELECT * FROM kpis WHERE id = ?";
        
        db_->exec(sql, [this, &result](sqlite3_stmt* stmt) {
            result = construct_kpi_from_stmt(stmt);
        }, kpi_id);
        
        if (result) {
            KPI_DEBUG_FMT("Retrieved KPI: {}", kpi_id);
        } else {
            KPI_DEBUG_FMT("KPI not found: {}", kpi_id);
        }
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving KPI: {}", e.what());
    }
    
    return result;
}

std::vector<kpi> kpi_repository::get_kpi_hierarchy(const std::string& parent_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi> kpis;
    
    try {
        std::string sql;
        if (parent_id.empty()) {
            sql = "SELECT * FROM kpis WHERE (parent_id IS NULL OR parent_id = '') AND is_active = 1 ORDER BY sort_order, name";
        } else {
            sql = "SELECT * FROM kpis WHERE parent_id = ? AND is_active = 1 ORDER BY sort_order, name";
        }
        
        if (parent_id.empty()) {
            db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
                kpis.push_back(construct_kpi_from_stmt(stmt));
            });
        } else {
            db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
                kpis.push_back(construct_kpi_from_stmt(stmt));
            }, parent_id);
        }
        
        KPI_DEBUG_FMT("Retrieved {} child KPIs for parent: {}", kpis.size(), parent_id);
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving KPI hierarchy: {}", e.what());
    }
    
    return kpis;
}

bool kpi_repository::create_kpi(const kpi& new_kpi) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "INSERT INTO kpis "
                          "(id, name, description, value_type, category_id, parent_id, "
                          "current_value, unit, last_updated, history, target, "
                          "aggregation_type, child_ids, weights, owner, reporting_period, "
                          "is_active, sort_order, tags) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        
        db_->exec(sql, {}, 
            new_kpi.id,
            new_kpi.name,
            new_kpi.description,
            static_cast<int>(new_kpi.value_type),
            new_kpi.category_id,
            new_kpi.parent_id,
            new_kpi.current_value,
            new_kpi.unit,
            new_kpi.last_updated,
            serialize_kpi_data_points(new_kpi.history),
            serialize_target(new_kpi.target),
            static_cast<int>(new_kpi.aggregation_type),
            serialize_string_vector(new_kpi.child_ids),
            serialize_weights_map(new_kpi.weights),
            new_kpi.owner,
            static_cast<int>(new_kpi.reporting_period),
            new_kpi.is_active ? 1 : 0,
            new_kpi.sort_order,
            serialize_string_map(new_kpi.tags));
        
        KPI_INFO_FMT("Created KPI: {}", new_kpi.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error creating KPI: {}", e.what());
        return false;
    }
}

bool kpi_repository::update_kpi(const kpi& updated_kpi) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "UPDATE kpis SET "
                          "name = ?, description = ?, value_type = ?, category_id = ?, "
                          "parent_id = ?, current_value = ?, unit = ?, last_updated = ?, "
                          "history = ?, target = ?, aggregation_type = ?, child_ids = ?, "
                          "weights = ?, owner = ?, reporting_period = ?, is_active = ?, "
                          "sort_order = ?, tags = ?, updated_at = datetime('now') "
                          "WHERE id = ?";
        
        db_->exec(sql, {}, 
            updated_kpi.name,
            updated_kpi.description,
            static_cast<int>(updated_kpi.value_type),
            updated_kpi.category_id,
            updated_kpi.parent_id,
            updated_kpi.current_value,
            updated_kpi.unit,
            updated_kpi.last_updated,
            serialize_kpi_data_points(updated_kpi.history),
            serialize_target(updated_kpi.target),
            static_cast<int>(updated_kpi.aggregation_type),
            serialize_string_vector(updated_kpi.child_ids),
            serialize_weights_map(updated_kpi.weights),
            updated_kpi.owner,
            static_cast<int>(updated_kpi.reporting_period),
            updated_kpi.is_active ? 1 : 0,
            updated_kpi.sort_order,
            serialize_string_map(updated_kpi.tags),
            updated_kpi.id);
        
        KPI_INFO_FMT("Updated KPI: {}", updated_kpi.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating KPI: {}", e.what());
        return false;
    }
}

bool kpi_repository::delete_kpi(const std::string& kpi_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "DELETE FROM kpis WHERE id = ?";
        db_->exec(sql, {}, kpi_id);
        
        KPI_INFO_FMT("Deleted KPI: {}", kpi_id);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deleting KPI: {}", e.what());
        return false;
    }
}

// KPI data operations
bool kpi_repository::update_kpi_value(const std::string& kpi_id, double value, const std::string& note) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        auto timestamp = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        auto timestamp_str = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(timestamp));
        
        // First, get current history
        std::vector<kpi_data_point> history;
        std::string sql = "SELECT history FROM kpis WHERE id = ?";
        
        db_->exec(sql, [this, &history](sqlite3_stmt* stmt) {
            const char* history_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (history_json && *history_json) {
                history = deserialize_kpi_data_points(history_json);
            }
        }, kpi_id);
        
        // Add new data point
        kpi_data_point new_point;
        new_point.timestamp = timestamp_str;
        new_point.value = value;
        new_point.note = note;
        new_point.source = "manual";
        
        history.push_back(new_point);
        
        // Keep only last 1000 data points to avoid bloat
        if (history.size() > 1000) {
            history.erase(history.begin(), history.begin() + (history.size() - 1000));
        }
        
        // Update KPI with new value and history
        sql = "UPDATE kpis SET current_value = ?, last_updated = ?, history = ?, "
              "updated_at = datetime('now') WHERE id = ?";
        
        db_->exec(sql, {}, 
            value,
            timestamp_str,
            serialize_kpi_data_points(history),
            kpi_id);
        
        KPI_INFO_FMT("Updated KPI value: {} = {}", kpi_id, value);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating KPI value: {}", e.what());
        return false;
    }
}

bool kpi_repository::add_kpi_data_point(const std::string& kpi_id, const kpi_data_point& data_point) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        // Get current history
        std::vector<kpi_data_point> history;
        std::string sql = "SELECT history FROM kpis WHERE id = ?";
        
        db_->exec(sql, [this, &history](sqlite3_stmt* stmt) {
            const char* history_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (history_json && *history_json) {
                history = deserialize_kpi_data_points(history_json);
            }
        }, kpi_id);
        
        // Add new data point
        history.push_back(data_point);
        
        // Keep only last 1000 data points
        if (history.size() > 1000) {
            history.erase(history.begin(), history.begin() + (history.size() - 1000));
        }
        
        // Update history and possibly current value if this is the latest
        sql = "UPDATE kpis SET history = ?, updated_at = datetime('now') WHERE id = ?";
        db_->exec(sql, {}, serialize_kpi_data_points(history), kpi_id);
        
        KPI_INFO_FMT("Added data point to KPI: {}", kpi_id);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error adding KPI data point: {}", e.what());
        return false;
    }
}

std::vector<kpi_data_point> kpi_repository::get_kpi_history(const std::string& kpi_id, const std::string& start_date, const std::string& end_date) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi_data_point> history;
    
    try {
        std::string sql = "SELECT history FROM kpis WHERE id = ?";
        
        db_->exec(sql, [this, &history](sqlite3_stmt* stmt) {
            const char* history_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (history_json && *history_json) {
                history = deserialize_kpi_data_points(history_json);
            }
        }, kpi_id);
        
        // Filter by date range if specified
        if (!start_date.empty() || !end_date.empty()) {
            history.erase(
                std::remove_if(history.begin(), history.end(), [&](const kpi_data_point& dp) {
                    if (!start_date.empty() && dp.timestamp < start_date) return true;
                    if (!end_date.empty() && dp.timestamp > end_date) return true;
                    return false;
                }),
                history.end()
            );
        }
        
        KPI_DEBUG_FMT("Retrieved {} historical data points for KPI: {}", history.size(), kpi_id);
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving KPI history: {}", e.what());
    }
    
    return history;
}

// Dashboard operations
std::vector<kpi_dashboard> kpi_repository::get_dashboards() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi_dashboard> dashboards;
    
    try {
        std::string sql = "SELECT * FROM kpi_dashboards ORDER BY is_default DESC, name";
        
        db_->exec(sql, [this, &dashboards](sqlite3_stmt* stmt) {
            kpi_dashboard dashboard;
            dashboard.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            dashboard.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            dashboard.description = desc ? desc : "";
            
            const char* kpi_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (kpi_ids_json && *kpi_ids_json) {
                dashboard.kpi_ids = deserialize_string_vector(kpi_ids_json);
            }
            
            const char* layout = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            dashboard.layout = layout ? layout : "";
            
            const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            dashboard.owner = owner ? owner : "";
            
            dashboard.is_default = sqlite3_column_int(stmt, 6) != 0;
            
            const char* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            dashboard.created_at = created_at ? created_at : "";
            
            const char* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            dashboard.updated_at = updated_at ? updated_at : "";
            
            dashboards.push_back(dashboard);
        });
        
        KPI_DEBUG_FMT("Retrieved {} dashboards", dashboards.size());
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving dashboards: {}", e.what());
    }
    
    return dashboards;
}

std::optional<kpi_dashboard> kpi_repository::get_dashboard(const std::string& dashboard_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::optional<kpi_dashboard> result;
    
    try {
        std::string sql = "SELECT * FROM kpi_dashboards WHERE id = ?";
        
        db_->exec(sql, [this, &result](sqlite3_stmt* stmt) {
            kpi_dashboard dashboard;
            dashboard.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            dashboard.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            dashboard.description = desc ? desc : "";
            
            const char* kpi_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (kpi_ids_json && *kpi_ids_json) {
                dashboard.kpi_ids = deserialize_string_vector(kpi_ids_json);
            }
            
            const char* layout = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            dashboard.layout = layout ? layout : "";
            
            const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            dashboard.owner = owner ? owner : "";
            
            dashboard.is_default = sqlite3_column_int(stmt, 6) != 0;
            
            const char* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            dashboard.created_at = created_at ? created_at : "";
            
            const char* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            dashboard.updated_at = updated_at ? updated_at : "";
            
            result = dashboard;
        }, dashboard_id);
        
        if (result) {
            KPI_DEBUG_FMT("Retrieved dashboard: {}", dashboard_id);
        }
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving dashboard: {}", e.what());
    }
    
    return result;
}

bool kpi_repository::create_dashboard(const kpi_dashboard& dashboard) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "INSERT INTO kpi_dashboards "
                          "(id, name, description, kpi_ids, layout, owner, is_default) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?)";
        
        db_->exec(sql, {}, 
            dashboard.id,
            dashboard.name,
            dashboard.description,
            serialize_string_vector(dashboard.kpi_ids),
            dashboard.layout,
            dashboard.owner,
            dashboard.is_default ? 1 : 0);
        
        KPI_INFO_FMT("Created dashboard: {}", dashboard.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error creating dashboard: {}", e.what());
        return false;
    }
}

bool kpi_repository::update_dashboard(const kpi_dashboard& dashboard) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "UPDATE kpi_dashboards SET "
                          "name = ?, description = ?, kpi_ids = ?, layout = ?, "
                          "owner = ?, is_default = ?, updated_at = datetime('now') "
                          "WHERE id = ?";
        
        db_->exec(sql, {}, 
            dashboard.name,
            dashboard.description,
            serialize_string_vector(dashboard.kpi_ids),
            dashboard.layout,
            dashboard.owner,
            dashboard.is_default ? 1 : 0,
            dashboard.id);
        
        KPI_INFO_FMT("Updated dashboard: {}", dashboard.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating dashboard: {}", e.what());
        return false;
    }
}

bool kpi_repository::delete_dashboard(const std::string& dashboard_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "DELETE FROM kpi_dashboards WHERE id = ?";
        db_->exec(sql, {}, dashboard_id);
        
        KPI_INFO_FMT("Deleted dashboard: {}", dashboard_id);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deleting dashboard: {}", e.what());
        return false;
    }
}

// Report operations
std::vector<kpi_report> kpi_repository::get_reports() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi_report> reports;
    
    try {
        std::string sql = "SELECT * FROM kpi_reports ORDER BY name";
        
        db_->exec(sql, [this, &reports](sqlite3_stmt* stmt) {
            kpi_report report;
            report.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            report.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            report.description = desc ? desc : "";
            
            const char* kpi_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (kpi_ids_json && *kpi_ids_json) {
                report.kpi_ids = deserialize_string_vector(kpi_ids_json);
            }
            
            const char* category_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (category_ids_json && *category_ids_json) {
                report.category_ids = deserialize_string_vector(category_ids_json);
            }
            
            report.period = static_cast<kpi_period>(sqlite3_column_int(stmt, 5));
            
            const char* start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            report.start_date = start_date ? start_date : "";
            
            const char* end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            report.end_date = end_date ? end_date : "";
            
            const char* template_config = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            report.template_config = template_config ? template_config : "";
            
            report.auto_generate = sqlite3_column_int(stmt, 9) != 0;
            
            const char* schedule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            report.schedule = schedule ? schedule : "";
            
            reports.push_back(report);
        });
        
        KPI_DEBUG_FMT("Retrieved {} reports", reports.size());
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving reports: {}", e.what());
    }
    
    return reports;
}

std::optional<kpi_report> kpi_repository::get_report(const std::string& report_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::optional<kpi_report> result;
    
    try {
        std::string sql = "SELECT * FROM kpi_reports WHERE id = ?";
        
        db_->exec(sql, [this, &result](sqlite3_stmt* stmt) {
            kpi_report report;
            report.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            report.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            report.description = desc ? desc : "";
            
            const char* kpi_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (kpi_ids_json && *kpi_ids_json) {
                report.kpi_ids = deserialize_string_vector(kpi_ids_json);
            }
            
            const char* category_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (category_ids_json && *category_ids_json) {
                report.category_ids = deserialize_string_vector(category_ids_json);
            }
            
            report.period = static_cast<kpi_period>(sqlite3_column_int(stmt, 5));
            
            const char* start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            report.start_date = start_date ? start_date : "";
            
            const char* end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            report.end_date = end_date ? end_date : "";
            
            const char* template_config = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            report.template_config = template_config ? template_config : "";
            
            report.auto_generate = sqlite3_column_int(stmt, 9) != 0;
            
            const char* schedule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            report.schedule = schedule ? schedule : "";
            
            result = report;
        }, report_id);
        
        if (result) {
            KPI_DEBUG_FMT("Retrieved report: {}", report_id);
        }
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error retrieving report: {}", e.what());
    }
    
    return result;
}

bool kpi_repository::create_report(const kpi_report& report) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "INSERT INTO kpi_reports "
                          "(id, name, description, kpi_ids, category_ids, period, "
                          "start_date, end_date, template_config, auto_generate, schedule) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        
        db_->exec(sql, {}, 
            report.id,
            report.name,
            report.description,
            serialize_string_vector(report.kpi_ids),
            serialize_string_vector(report.category_ids),
            static_cast<int>(report.period),
            report.start_date,
            report.end_date,
            report.template_config,
            report.auto_generate ? 1 : 0,
            report.schedule);
        
        KPI_INFO_FMT("Created report: {}", report.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error creating report: {}", e.what());
        return false;
    }
}

bool kpi_repository::update_report(const kpi_report& report) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "UPDATE kpi_reports SET "
                          "name = ?, description = ?, kpi_ids = ?, category_ids = ?, "
                          "period = ?, start_date = ?, end_date = ?, template_config = ?, "
                          "auto_generate = ?, schedule = ?, updated_at = datetime('now') "
                          "WHERE id = ?";
        
        db_->exec(sql, {}, 
            report.name,
            report.description,
            serialize_string_vector(report.kpi_ids),
            serialize_string_vector(report.category_ids),
            static_cast<int>(report.period),
            report.start_date,
            report.end_date,
            report.template_config,
            report.auto_generate ? 1 : 0,
            report.schedule,
            report.id);
        
        KPI_INFO_FMT("Updated report: {}", report.name);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating report: {}", e.what());
        return false;
    }
}

bool kpi_repository::delete_report(const std::string& report_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "DELETE FROM kpi_reports WHERE id = ?";
        db_->exec(sql, {}, report_id);
        
        KPI_INFO_FMT("Deleted report: {}", report_id);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deleting report: {}", e.what());
        return false;
    }
}

// Utility operations
std::vector<kpi> kpi_repository::get_kpis_missing_targets() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi> kpis;
    
    try {
        std::string sql = "SELECT * FROM kpis WHERE is_active = 1 AND (target IS NULL OR target = '') ORDER BY name";
        
        db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
            kpis.push_back(construct_kpi_from_stmt(stmt));
        });
        
        KPI_DEBUG_FMT("Found {} KPIs missing targets", kpis.size());
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error getting KPIs missing targets: {}", e.what());
    }
    
    return kpis;
}

std::vector<kpi> kpi_repository::get_kpis_needing_update(int days_threshold) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<kpi> kpis;
    
    try {
        auto now = std::chrono::system_clock::now();
        auto threshold = now - std::chrono::hours(24 * days_threshold);
        auto threshold_str = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(threshold));
        
        std::string sql = "SELECT * FROM kpis WHERE is_active = 1 AND "
                          "(last_updated IS NULL OR last_updated < ?) ORDER BY last_updated";
        
        db_->exec(sql, [this, &kpis](sqlite3_stmt* stmt) {
            kpis.push_back(construct_kpi_from_stmt(stmt));
        }, threshold_str);
        
        KPI_DEBUG_FMT("Found {} KPIs needing update (>{} days)", kpis.size(), days_threshold);
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error getting KPIs needing update: {}", e.what());
    }
    
    return kpis;
}

bool kpi_repository::execute_test_query() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    try {
        std::string sql = "SELECT COUNT(*) FROM kpis";
        int count = 0;
        
        db_->exec(sql, [&count](sqlite3_stmt* stmt) {
            count = sqlite3_column_int(stmt, 0);
        });
        
        KPI_INFO_FMT("Test query successful: {} KPIs in database", count);
        return true;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Test query failed: {}", e.what());
        return false;
    }
}

// Helper methods for serialization
std::string kpi_repository::serialize_kpi_data_points(const std::vector<kpi_data_point>& data_points) {
    try {
        std::string json;
        auto result = glz::write_json(data_points, json);
        if (result) {
            KPI_WARN("Failed to serialize KPI data points");
            return "[]";
        }
        return json;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error serializing KPI data points: {}", e.what());
        return "[]";
    }
}

std::vector<kpi_data_point> kpi_repository::deserialize_kpi_data_points(const std::string& json_data) {
    try {
        std::vector<kpi_data_point> data_points;
        auto result = glz::read_json(data_points, json_data);
        if (result) {
            KPI_WARN("Failed to deserialize KPI data points");
            return {};
        }
        return data_points;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deserializing KPI data points: {}", e.what());
        return {};
    }
}

std::string kpi_repository::serialize_string_vector(const std::vector<std::string>& values) {
    try {
        std::string json;
        auto result = glz::write_json(values, json);
        if (result) {
            KPI_WARN("Failed to serialize string vector");
            return "[]";
        }
        return json;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error serializing string vector: {}", e.what());
        return "[]";
    }
}

std::vector<std::string> kpi_repository::deserialize_string_vector(const std::string& json_data) {
    try {
        std::vector<std::string> values;
        auto result = glz::read_json(values, json_data);
        if (result) {
            KPI_WARN("Failed to deserialize string vector");
            return {};
        }
        return values;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deserializing string vector: {}", e.what());
        return {};
    }
}

std::string kpi_repository::serialize_string_map(const std::map<std::string, std::string>& values) {
    try {
        std::string json;
        auto result = glz::write_json(values, json);
        if (result) {
            KPI_WARN("Failed to serialize string map");
            return "{}";
        }
        return json;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error serializing string map: {}", e.what());
        return "{}";
    }
}

std::map<std::string, std::string> kpi_repository::deserialize_string_map(const std::string& json_data) {
    try {
        std::map<std::string, std::string> values;
        auto result = glz::read_json(values, json_data);
        if (result) {
            KPI_WARN("Failed to deserialize string map");
            return {};
        }
        return values;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deserializing string map: {}", e.what());
        return {};
    }
}

std::string kpi_repository::serialize_weights_map(const std::map<std::string, double>& weights) {
    try {
        std::string json;
        auto result = glz::write_json(weights, json);
        if (result) {
            KPI_WARN("Failed to serialize weights map");
            return "{}";
        }
        return json;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error serializing weights map: {}", e.what());
        return "{}";
    }
}

std::map<std::string, double> kpi_repository::deserialize_weights_map(const std::string& json_data) {
    try {
        std::map<std::string, double> weights;
        auto result = glz::read_json(weights, json_data);
        if (result) {
            KPI_WARN("Failed to deserialize weights map");
            return {};
        }
        return weights;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deserializing weights map: {}", e.what());
        return {};
    }
}

std::string kpi_repository::serialize_target(const std::optional<kpi_target>& target) {
    try {
        if (!target) {
            return "";
        }
        std::string json;
        auto result = glz::write_json(*target, json);
        if (result) {
            KPI_WARN("Failed to serialize KPI target");
            return "";
        }
        return json;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error serializing KPI target: {}", e.what());
        return "";
    }
}

std::optional<kpi_target> kpi_repository::deserialize_target(const std::string& json_data) {
    try {
        if (json_data.empty()) {
            return std::nullopt;
        }
        kpi_target target;
        auto result = glz::read_json(target, json_data);
        if (result) {
            KPI_WARN("Failed to deserialize KPI target");
            return std::nullopt;
        }
        return target;
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error deserializing KPI target: {}", e.what());
        return std::nullopt;
    }
}

kpi kpi_repository::construct_kpi_from_stmt(sqlite3_stmt* stmt) {
    kpi k;
    
    k.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    k.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    
    const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    k.description = desc ? desc : "";
    
    k.value_type = static_cast<kpi_value_type>(sqlite3_column_int(stmt, 3));
    
    const char* category_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    k.category_id = category_id ? category_id : "";
    
    const char* parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    k.parent_id = parent_id ? parent_id : "";
    
    k.current_value = sqlite3_column_double(stmt, 6);
    
    const char* unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    k.unit = unit ? unit : "";
    
    const char* last_updated = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    k.last_updated = last_updated ? last_updated : "";
    
    const char* history_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    if (history_json && *history_json) {
        k.history = deserialize_kpi_data_points(history_json);
    }
    
    const char* target_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    if (target_json && *target_json) {
        k.target = deserialize_target(target_json);
    }
    
    k.aggregation_type = static_cast<kpi_aggregation>(sqlite3_column_int(stmt, 11));
    
    const char* child_ids_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    if (child_ids_json && *child_ids_json) {
        k.child_ids = deserialize_string_vector(child_ids_json);
    }
    
    const char* weights_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
    if (weights_json && *weights_json) {
        k.weights = deserialize_weights_map(weights_json);
    }
    
    const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
    k.owner = owner ? owner : "";
    
    k.reporting_period = static_cast<kpi_period>(sqlite3_column_int(stmt, 15));
    k.is_active = sqlite3_column_int(stmt, 16) != 0;
    k.sort_order = sqlite3_column_int(stmt, 17);
    
    const char* tags_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18));
    if (tags_json && *tags_json) {
        k.tags = deserialize_string_map(tags_json);
    }
    
    // Initialize calculated fields to defaults
    k.trend = kpi_trend::unknown;
    k.target_status = kpi_target_status::no_target;
    k.trend_percentage = 0.0;
    
    return k;
}

} // namespace rouen::models
