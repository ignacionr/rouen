#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>

#include "../helpers/sqlite.hpp"
#include "kpi_model.hpp"

namespace rouen::models {

class kpi_repository {
public:
    kpi_repository(const std::string& db_path);
    ~kpi_repository();
    
    // Category operations
    std::vector<kpi_category> get_categories();
    bool create_category(const kpi_category& category);
    bool update_category(const kpi_category& category);
    bool delete_category(const std::string& category_id);
    
    // KPI operations
    std::vector<kpi> get_kpis(const std::string& category_id = "");
    std::optional<kpi> get_kpi(const std::string& kpi_id);
    std::vector<kpi> get_kpi_hierarchy(const std::string& parent_id = "");
    bool create_kpi(const kpi& new_kpi);
    bool update_kpi(const kpi& updated_kpi);
    bool delete_kpi(const std::string& kpi_id);
    
    // KPI data operations
    bool update_kpi_value(const std::string& kpi_id, double value, const std::string& note = "");
    bool add_kpi_data_point(const std::string& kpi_id, const kpi_data_point& data_point);
    std::vector<kpi_data_point> get_kpi_history(const std::string& kpi_id, const std::string& start_date = "", const std::string& end_date = "");
    
    // Dashboard operations
    std::vector<kpi_dashboard> get_dashboards();
    std::optional<kpi_dashboard> get_dashboard(const std::string& dashboard_id);
    bool create_dashboard(const kpi_dashboard& dashboard);
    bool update_dashboard(const kpi_dashboard& dashboard);
    bool delete_dashboard(const std::string& dashboard_id);
    
    // Report operations
    std::vector<kpi_report> get_reports();
    std::optional<kpi_report> get_report(const std::string& report_id);
    bool create_report(const kpi_report& report);
    bool update_report(const kpi_report& report);
    bool delete_report(const std::string& report_id);
    
    // Utility operations
    std::vector<kpi> get_kpis_missing_targets();
    std::vector<kpi> get_kpis_needing_update(int days_threshold = 7);
    bool execute_test_query();

private:
    void initialize_database();
    void create_tables();
    
    // Helper methods for serialization
    std::string serialize_kpi_data_points(const std::vector<kpi_data_point>& data_points);
    std::vector<kpi_data_point> deserialize_kpi_data_points(const std::string& json_data);
    std::string serialize_string_vector(const std::vector<std::string>& values);
    std::vector<std::string> deserialize_string_vector(const std::string& json_data);
    std::string serialize_string_map(const std::map<std::string, std::string>& values);
    std::map<std::string, std::string> deserialize_string_map(const std::string& json_data);
    std::string serialize_weights_map(const std::map<std::string, double>& weights);
    std::map<std::string, double> deserialize_weights_map(const std::string& json_data);
    std::string serialize_target(const std::optional<kpi_target>& target);
    std::optional<kpi_target> deserialize_target(const std::string& json_data);
    
    // KPI construction helper
    kpi construct_kpi_from_stmt(sqlite3_stmt* stmt);
    
    std::unique_ptr<hosting::db::sqlite> db_;
    mutable std::mutex db_mutex_;
    std::string db_path_;
};

} // namespace rouen::models
